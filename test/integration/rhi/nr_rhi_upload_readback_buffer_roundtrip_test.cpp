import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeUploadOwnershipPlan(const nr::rhi::Device &device)
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
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
            }),
    };
}

void acquireUploadedBufferOnGraphics(nr::rhi::Device &device,
                                     const nr::rhi::ops::BufferUploadTicket &ticket)
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
    batch.addWait(device.uploadReadback().uploadTimelineSemaphore(),
                  vk::PipelineStageFlagBits2::eAllCommands,
                  ticket.signalValue);
    batch.addCommandBuffer(commandBuffer);

    device.queueManager.graphics().submit(batch);
    device.queueManager.graphics().waitIdle();
}

const nr::test::CaseRegistrar roundtripCase{
    "rhi upload/readback buffer roundtrip preserves bytes across transfer and graphics queues",
    [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_upload_readback_buffer_roundtrip_test", "NewbieRenderer");

        auto payload = std::array<std::uint32_t, 8>{
            0x10203040u,
            0x55667788u,
            0x90ABCDEFu,
            0x00000000u,
            0xFFFFFFFFu,
            0x12345678u,
            0xCAFEBABEu,
            0x0BADF00Du,
        };
        auto payloadBytes = std::as_bytes(std::span<const std::uint32_t>{payload.data(), payload.size()});

        auto bufferInfo = vk::BufferCreateInfo{};
        bufferInfo.size = payloadBytes.size_bytes();
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        auto buffer = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::GpuOnly, "upload_readback_roundtrip_buffer");
        nr::test::require(buffer.valid(), "roundtrip destination buffer should be valid");

        auto &uploadReadback = device.uploadReadback();
        auto ticket = uploadReadback.uploadBuffer(payloadBytes, buffer, 0, makeUploadOwnershipPlan(device));
        nr::test::require(ticket.valid(), "upload ticket should be valid");

        acquireUploadedBufferOnGraphics(device, ticket);

        auto readbackTicket = uploadReadback.readbackBuffer(
            buffer,
            0,
            payloadBytes.size_bytes(),
            nr::rhi::QueueRole::Graphics,
            nr::rhi::ops::ReadbackSyncPlan{
                .preCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                },
                .postCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                },
            });

        auto readback = uploadReadback.readbackBytes(readbackTicket);
        nr::test::requireEqual(readback.size(), payloadBytes.size_bytes());
        nr::test::require(std::ranges::equal(readback, payloadBytes), "readback bytes should match uploaded payload");

        device.waitIdle();
    }};
} // namespace
