import std;
import dependency.vulkan;
import nr.load;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeEnvironmentUploadPlan(
    const nr::rhi::Device& device)
{
    auto const transferFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsFamily = device.queueManager.graphics().queueFamilyIndex();
    return nr::rhi::ops::BufferUploadOwnershipPlan{
        .releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
            transferFamily,
            graphicsFamily,
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderSampledRead,
            }),
    };
}

void acquireEnvironmentOnGraphics(
    nr::rhi::Device& device,
    const nr::rhi::ops::ImageUploadTicket& ticket)
{
    auto& uploadReadback = device.uploadReadback();
    auto commandPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto commandBuffers = commandPool.allocatePrimary(1);
    auto const& commandBuffer = commandBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(
        commandBuffer,
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    uploadReadback.recordImageAcquireBarrier(commandBuffer, ticket);
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto batch = nr::rhi::CommandBatch{};
    batch.addWait(
        uploadReadback.uploadTimelineSemaphore(),
        vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        ticket.signalValue);
    batch.addCommandBuffer(commandBuffer);
    device.queueManager.graphics().submit(std::move(batch));
    device.queueManager.graphics().waitIdle();
}

[[nodiscard]] std::vector<std::byte> readbackEnvironmentTexel(
    nr::rhi::Device& device,
    const nr::rhi::Image& image,
    std::uint32_t x,
    std::uint32_t y)
{
    auto region = vk::BufferImageCopy{};
    region.imageSubresource = vk::ImageSubresourceLayers{
        vk::ImageAspectFlagBits::eColor,
        0u,
        0u,
        1u,
    };
    region.imageOffset.x = static_cast<std::int32_t>(x);
    region.imageOffset.y = static_cast<std::int32_t>(y);
    region.imageExtent = vk::Extent3D{1u, 1u, 1u};

    auto& uploadReadback = device.uploadReadback();
    auto ticket = uploadReadback.readbackImage(
        image,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        nr::rhi::QueueRole::Graphics,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderSampledRead,
            },
            .postCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderSampledRead,
            },
        },
        region);
    return uploadReadback.readbackBytes(ticket);
}

const nr::test::CaseRegistrar defaultEnvironmentUploadCase{
    "rhi uploads the default 8K RGBA16F environment through the 128 MiB ring",
    [] {
        auto const sourcePath = std::filesystem::path{std::string{nr::projectRoot}} /
                                "assets" /
                                "envMap" /
                                "studio_small_09_8k.exr";
        auto loadResult = nr::load::loadExrEnvironmentMap(nr::load::ExrEnvironmentLoadRequest{
            .sourcePath = sourcePath,
        });
        nr::test::require(loadResult.has_value(), "default studio OpenEXR environment should load");

        auto& texture = loadResult->radiance;
        nr::test::requireEqual(texture.width, 8192u);
        nr::test::requireEqual(texture.height, 4096u);
        nr::test::require(
            texture.format == vk::Format::eR16G16B16A16Sfloat,
            "default environment CPU payload should be RGBA16F");
        nr::test::requireEqual(
            texture.levels.front().bytes.size(),
            std::size_t{256u * 1024u * 1024u});

        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_environment_map_upload_test", "NewbieRenderer");

        auto imageInfo = nr::rhi::makeImageCreateInfo(
            texture.format,
            vk::Extent2D{texture.width, texture.height},
            vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eSampled);
        auto image = device.resourceFactory.createImage(
            imageInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            "test.default_environment_map");
        nr::test::require(image.valid(), "default environment GPU image should be valid");

        auto& uploadReadback = device.uploadReadback();
        auto uploadTicket = uploadReadback.uploadImage(
            texture.levels.front().bytes,
            image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            makeEnvironmentUploadPlan(device));
        nr::test::require(uploadTicket.valid(), "default environment upload ticket should be valid");
        nr::test::require(
            uploadTicket.signalValue >= 2u,
            "256 MiB environment should require at least two submissions through the 128 MiB ring");
        acquireEnvironmentOnGraphics(device, uploadTicket);

        constexpr auto bytesPerTexel = std::size_t{4u * sizeof(std::uint16_t)};
        auto const probes = std::array{
            std::pair{0u, 0u},
            std::pair{texture.width - 1u, 2047u},
            std::pair{0u, 2048u},
            std::pair{texture.width - 1u, texture.height - 1u},
        };
        std::ranges::for_each(probes, [&](auto coordinate) {
            auto const [x, y] = coordinate;
            auto readback = readbackEnvironmentTexel(device, image, x, y);
            auto const sourceTexelIndex =
                static_cast<std::size_t>(y) * texture.width + x;
            auto const sourceOffset = sourceTexelIndex * bytesPerTexel;
            auto const expected = std::span<const std::byte>{texture.levels.front().bytes}.subspan(
                sourceOffset,
                bytesPerTexel);
            nr::test::requireEqual(readback.size(), bytesPerTexel);
            nr::test::require(
                std::ranges::equal(readback, expected),
                std::format(
                    "environment texel ({}, {}) should survive its upload-ring chunk exactly",
                    x,
                    y));
        });

        device.waitIdle();
    }};
} // namespace
