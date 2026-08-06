import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeUploadOwnershipPlan(const nr::rhi::Device &device)
{
    auto const transferFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsFamily = device.queueManager.graphics().queueFamilyIndex();

    return nr::rhi::ops::BufferUploadOwnershipPlan{
        .releaseToDestination =
            nr::rhi::ops::makeQueueOwnershipTransfer(transferFamily, graphicsFamily,
                                                     nr::rhi::ops::QueueAccessScope{
                                                         .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                         .access = vk::AccessFlagBits2::eTransferWrite,
                                                     },
                                                     nr::rhi::ops::QueueAccessScope{
                                                         .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                         .access = vk::AccessFlagBits2::eTransferRead,
                                                     }),
    };
}

void acquireUploadedBufferOnGraphics(nr::rhi::Device &device, const nr::rhi::ops::BufferUploadTicket &ticket)
{
    auto commandPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto commandBuffers = commandPool.allocatePrimary(1);
    auto const &commandBuffer = commandBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    device.uploadReadback().recordAcquireBarrier(commandBuffer, ticket);
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto batch = nr::rhi::CommandBatch{};
    batch.addWait(device.uploadReadback().uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands,
                  ticket.signalValue);
    batch.addCommandBuffer(commandBuffer);

    device.queueManager.graphics().submit(std::move(batch));
    device.queueManager.graphics().waitIdle();
}

void acquireUploadedImageOnGraphics(nr::rhi::Device &device, nr::rhi::ops::UploadReadbackContext &uploadReadback,
                                    const nr::rhi::ops::ImageUploadTicket &ticket)
{
    auto commandPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto commandBuffers = commandPool.allocatePrimary(1);
    auto const &commandBuffer = commandBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    uploadReadback.recordImageAcquireBarrier(commandBuffer, ticket);
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto batch = nr::rhi::CommandBatch{};
    batch.addWait(uploadReadback.uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands,
                  ticket.signalValue);
    batch.addCommandBuffer(commandBuffer);

    device.queueManager.graphics().submit(std::move(batch));
    device.queueManager.graphics().waitIdle();
}

const nr::test::CaseRegistrar roundtripCase{
    "rhi upload/readback buffer roundtrip preserves bytes across transfer and graphics queues", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_upload_readback_buffer_roundtrip_test", "NewbieRenderer");

        auto payload = std::array<std::uint32_t, 8>{
            0x10203040u, 0x55667788u, 0x90ABCDEFu, 0x00000000u, 0xFFFFFFFFu, 0x12345678u, 0xCAFEBABEu, 0x0BADF00Du,
        };
        auto payloadBytes = std::as_bytes(std::span<const std::uint32_t>{payload.data(), payload.size()});

        auto bufferInfo = vk::BufferCreateInfo{};
        bufferInfo.size = payloadBytes.size_bytes();
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        auto buffer = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                          "upload_readback_roundtrip_buffer");
        nr::test::require(buffer.valid(), "roundtrip destination buffer should be valid");

        auto &uploadReadback = device.uploadReadback();
        auto ticket = uploadReadback.uploadBuffer(payloadBytes, buffer, 0, makeUploadOwnershipPlan(device));
        nr::test::require(ticket.valid(), "upload ticket should be valid");

        acquireUploadedBufferOnGraphics(device, ticket);

        auto readbackTicket =
            uploadReadback.readbackBuffer(buffer, 0, payloadBytes.size_bytes(), nr::rhi::QueueRole::Graphics,
                                          nr::rhi::ops::ReadbackSyncPlan{
                                              .preCopy =
                                                  nr::rhi::ops::ReadbackSyncScope{
                                                      .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                      .access = vk::AccessFlagBits2::eTransferRead,
                                                  },
                                              .postCopy =
                                                  nr::rhi::ops::ReadbackSyncScope{
                                                      .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                      .access = vk::AccessFlagBits2::eTransferRead,
                                                  },
                                          });

        auto readback = uploadReadback.readbackBytes(readbackTicket);
        nr::test::requireEqual(readback.size(), payloadBytes.size_bytes());
        nr::test::require(std::ranges::equal(readback, payloadBytes), "readback bytes should match uploaded payload");

        device.waitIdle();
    }};

const nr::test::CaseRegistrar chunkedImageRoundtripCase{
    "rhi upload/readback image roundtrip preserves a payload larger than the upload ring", [] {
        constexpr auto width = std::uint32_t{16u};
        constexpr auto height = std::uint32_t{8u};
        constexpr auto bytesPerPixel = std::size_t{8u};

        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_upload_readback_chunked_image_roundtrip_test", "NewbieRenderer");

        auto uploadReadback = nr::rhi::ops::UploadReadbackContext{
            device.device, device.resourceFactory, device.queueManager, device.queueFamilyTransferPolicy(), 256u, 2048u,
        };
        nr::test::require(uploadReadback.valid(), "small-ring upload/readback context should be valid");

        auto payload = std::vector<std::byte>(static_cast<std::size_t>(width) * height * bytesPerPixel);
        auto nextByte = std::uint32_t{0u};
        std::ranges::generate(payload, [&nextByte] {
            auto const value = static_cast<std::uint8_t>((nextByte * 37u + 11u) & 0xffu);
            ++nextByte;
            return static_cast<std::byte>(value);
        });

        auto imageInfo = vk::ImageCreateInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.extent = vk::Extent3D{width, height, 1u};
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                          vk::ImageUsageFlagBits::eSampled;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        auto image = device.resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                        "chunked_upload_roundtrip_image");
        nr::test::require(image.valid(), "chunked upload destination image should be valid");

        auto ticket = uploadReadback.uploadImage(payload, image, vk::ImageLayout::eUndefined,
                                                 vk::ImageLayout::eTransferSrcOptimal, makeUploadOwnershipPlan(device));
        nr::test::require(ticket.valid(), "chunked image upload ticket should be valid");
        nr::test::require(
            ticket.signalValue >= 4u,
            "a 1024-byte RGBA16F image uploaded through a 256-byte ring should require at least four submits");

        acquireUploadedImageOnGraphics(device, uploadReadback, ticket);

        auto readbackTicket =
            uploadReadback.readbackImage(image, vk::ImageLayout::eTransferSrcOptimal, nr::rhi::QueueRole::Graphics,
                                         nr::rhi::ops::ReadbackSyncPlan{
                                             .preCopy =
                                                 nr::rhi::ops::ReadbackSyncScope{
                                                     .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                     .access = vk::AccessFlagBits2::eTransferRead,
                                                 },
                                             .postCopy =
                                                 nr::rhi::ops::ReadbackSyncScope{
                                                     .stages = vk::PipelineStageFlagBits2::eTransfer,
                                                     .access = vk::AccessFlagBits2::eTransferRead,
                                                 },
                                         });
        auto readback = uploadReadback.readbackBytes(readbackTicket);

        nr::test::requireEqual(readback.size(), payload.size());
        nr::test::require(std::ranges::equal(readback, payload),
                          "chunked image upload/readback bytes should match the source payload");

        device.waitIdle();
    }};

const nr::test::CaseRegistrar deferredReadbackConsumptionCase{
    "rhi readback ring preserves an unread result before reusing its full capacity", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_deferred_readback_consumption_test", "NewbieRenderer");

        constexpr auto payloadA = std::array<std::uint32_t, 8>{
            0x01020304u, 0x11121314u, 0x21222324u, 0x31323334u,
            0x41424344u, 0x51525354u, 0x61626364u, 0x71727374u,
        };
        constexpr auto payloadB = std::array<std::uint32_t, 8>{
            0x81828384u, 0x91929394u, 0xA1A2A3A4u, 0xB1B2B3B4u,
            0xC1C2C3C4u, 0xD1D2D3D4u, 0xE1E2E3E4u, 0xF1F2F3F4u,
        };
        constexpr auto payloadSize = vk::DeviceSize{sizeof(payloadA)};

        auto uploadReadback = nr::rhi::ops::UploadReadbackContext{
            device.device, device.resourceFactory, device.queueManager, device.queueFamilyTransferPolicy(),
            payloadSize, payloadSize,
        };

        auto bufferInfo = vk::BufferCreateInfo{};
        bufferInfo.size = payloadSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        auto bufferA = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::CpuToGpu,
                                                           "deferred_readback_source_a");
        auto bufferB = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::CpuToGpu,
                                                           "deferred_readback_source_b");
        bufferA.writeMappedAndFlush(std::span<const std::uint32_t>{payloadA});
        bufferB.writeMappedAndFlush(std::span<const std::uint32_t>{payloadB});

        auto const syncPlan = nr::rhi::ops::ReadbackSyncPlan{
            .preCopy =
                nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eHost,
                    .access = vk::AccessFlagBits2::eHostWrite,
                },
            .postCopy =
                nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eHost,
                    .access = vk::AccessFlagBits2::eHostRead,
                },
        };

        auto ticketA = uploadReadback.readbackBuffer(bufferA, 0u, payloadSize, nr::rhi::QueueRole::Graphics, syncPlan);
        auto ticketB = uploadReadback.readbackBuffer(bufferB, 0u, payloadSize, nr::rhi::QueueRole::Graphics, syncPlan);

        auto bytesB = uploadReadback.readbackBytes(ticketB);
        auto bytesA = uploadReadback.readbackBytes(ticketA);
        nr::test::require(std::ranges::equal(bytesB, std::as_bytes(std::span{payloadB})),
                          "the replacement readback must preserve its own bytes");
        nr::test::require(std::ranges::equal(bytesA, std::as_bytes(std::span{payloadA})),
                          "the first unread result must survive full-ring reuse and out-of-order consumption");

        device.waitIdle();
    }};
} // namespace
