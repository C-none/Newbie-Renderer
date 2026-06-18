import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
const nr::test::CaseRegistrar ownershipTransferCase{
    "rhi upload ownership plans validate same and cross queue handoff",
    [] {
        auto transfer = nr::rhi::ops::makeQueueOwnershipTransfer(
            2u,
            0u,
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eVertexAttributeInput,
                .access = vk::AccessFlagBits2::eVertexAttributeRead,
            });

        nr::test::require(transfer.valid(), "cross queue transfer should validate");
        nr::test::require(!transfer.hasWait(), "plain transfer should not carry a wait");

        auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{.releaseToDestination = transfer};
        nr::test::require(plan.valid(2u), "plan should validate against transfer queue family");
        nr::test::require(!plan.valid(1u), "plan should reject the wrong transfer queue family");

        auto sameQueue = nr::rhi::ops::QueueOwnershipTransfer{
            .srcQueueFamilyIndex = 2u,
            .dstQueueFamilyIndex = 2u,
            .release = nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            .acquire = nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
            },
        };
        auto sameQueuePlan = nr::rhi::ops::BufferUploadOwnershipPlan{.releaseToDestination = sameQueue};
        nr::test::require(sameQueuePlan.isSameQueueFamily(), "same queue plan should use the same-family path");
        nr::test::require(sameQueuePlan.valid(2u), "same queue plan should validate with matching transfer family");
    }};

const nr::test::CaseRegistrar accelerationStructureBarrierCase{
    "rhi acceleration-structure barriers carry precise sync scopes",
    [] {
        auto buildToTrace = nr::rhi::ops::makeAccelerationStructureBuildToTraceReadBarrier();
        nr::test::require(buildToTrace.srcStageMask == vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR);
        nr::test::require(buildToTrace.srcAccessMask == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(buildToTrace.dstStageMask == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
        nr::test::require(buildToTrace.dstAccessMask == vk::AccessFlagBits2::eAccelerationStructureReadKHR);

        auto copyToTrace = nr::rhi::ops::makeAccelerationStructureCopyToTraceReadBarrier();
        nr::test::require(copyToTrace.srcStageMask == vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR);
        nr::test::require(copyToTrace.srcAccessMask == vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
        nr::test::require(copyToTrace.dstStageMask == vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    }};

const nr::test::CaseRegistrar barrierBatchCase{
    "rhi barrier batch owns dependency-info backing arrays",
    [] {
        auto batch = nr::rhi::ops::BarrierBatch{};
        nr::test::require(batch.empty(), "new barrier batch should be empty");
        batch.add(vk::MemoryBarrier2{
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderRead,
        });
        nr::test::require(!batch.empty(), "barrier batch should become non-empty");

        auto packet = batch.buildDependencyInfo();
        nr::test::requireEqual(packet.memoryBarriers.size(), std::size_t{1});
        nr::test::requireEqual(packet.bufferBarriers.size(), std::size_t{0});
        nr::test::requireEqual(packet.imageBarriers.size(), std::size_t{0});
        nr::test::requireEqual(packet.dependencyInfo().memoryBarrierCount, 1u);
        nr::test::require(packet.dependencyInfo().pMemoryBarriers == packet.memoryBarriers.data(), "dependency info should point at packet-owned memory");

        batch.clear();
        nr::test::require(batch.empty(), "clear should empty all barrier arrays");
    }};

const nr::test::CaseRegistrar readbackSyncPlanCase{
    "rhi readback sync plan requires producer and host-visible scopes",
    [] {
        auto plan = nr::rhi::ops::ReadbackSyncPlan{};
        nr::test::require(!plan.valid(), "empty readback plan should be invalid");
        plan.preCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderWrite,
        };
        plan.postCopy = nr::rhi::ops::ReadbackSyncScope{
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderRead,
        };
        nr::test::require(plan.valid(), "filled readback plan should be valid");
    }};
} // namespace
