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

        auto buffer = nr::rhi::Buffer{};
        auto releaseBarrier = nr::rhi::ops::makeBufferOwnershipTransferBarrier<
            nr::rhi::ops::OwnershipBarrierPhase::Release>(buffer, transfer);
        nr::test::require(
            releaseBarrier.srcStageMask == vk::PipelineStageFlagBits2::eTransfer &&
                releaseBarrier.dstStageMask == vk::PipelineStageFlagBits2::eTransfer,
            "maintenance8 release should use the producer stage in both scopes");
        nr::test::require(releaseBarrier.srcAccessMask == vk::AccessFlagBits2::eTransferWrite);
        nr::test::require(releaseBarrier.dstAccessMask == vk::AccessFlags2{});

        auto acquireBarrier = nr::rhi::ops::makeBufferOwnershipTransferBarrier<
            nr::rhi::ops::OwnershipBarrierPhase::Acquire>(buffer, transfer);
        nr::test::require(
            acquireBarrier.srcStageMask == vk::PipelineStageFlagBits2::eVertexAttributeInput &&
                acquireBarrier.dstStageMask == vk::PipelineStageFlagBits2::eVertexAttributeInput,
            "maintenance8 acquire should use the consumer stage in both scopes");
        nr::test::require(acquireBarrier.srcAccessMask == vk::AccessFlags2{});
        nr::test::require(acquireBarrier.dstAccessMask == vk::AccessFlagBits2::eVertexAttributeRead);

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

const nr::test::CaseRegistrar maintenance9TransferPolicyCase{
    "rhi maintenance9 transfer policy identifies omittable queue ownership transfers",
    [] {
        auto policy = nr::rhi::ops::QueueFamilyTransferPolicy{
            .maintenance9 = true,
            .optimalImageTransferToQueueFamilies = std::vector<std::uint32_t>{1u << 2u},
        };

        nr::test::require(
            policy.canOmitBufferQueueFamilyTransfer(0u, 2u),
            "maintenance9 should allow buffer ownership transfer omission");
        nr::test::require(
            policy.canOmitImageQueueFamilyTransfer(
                0u,
                2u,
                vk::ImageTiling::eLinear,
                vk::ImageUsageFlagBits::eTransferDst),
            "maintenance9 should allow linear image ownership transfer omission");
        nr::test::require(
            policy.canOmitImageQueueFamilyTransfer(
                0u,
                2u,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled),
            "maintenance9 should allow eligible optimal sampled image ownership transfer omission");
        nr::test::require(
            !policy.canOmitImageQueueFamilyTransfer(
                0u,
                2u,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment),
            "attachment images should retain explicit ownership transfer");
        nr::test::require(
            !policy.canOmitImageQueueFamilyTransfer(
                0u,
                1u,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled),
            "optimal image omission should require the destination queue-family bit");

        auto disabledPolicy = nr::rhi::ops::QueueFamilyTransferPolicy{};
        nr::test::require(
            !disabledPolicy.canOmitBufferQueueFamilyTransfer(0u, 2u),
            "disabled maintenance9 policy should not omit buffer ownership transfer");
        nr::test::require(
            !disabledPolicy.canOmitImageQueueFamilyTransfer(
                0u,
                2u,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled),
            "unknown optimal image policy should not omit ownership transfer");

        auto image = nr::rhi::Image{};
        auto ticket = nr::rhi::ops::ImageUploadTicket{
            .image = std::cref(image),
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .signalValue = 1u,
        };
        nr::test::require(ticket.valid(), "image upload tickets without ownership remain valid under maintenance9");
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
        batch.add(vk::BufferMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderRead,
            0u,
            1u,
            vk::Buffer{},
            0u,
            16u,
        });
        nr::test::require(!batch.empty(), "barrier batch should become non-empty");

        auto packet = batch.buildDependencyInfo();
        nr::test::requireEqual(packet.memoryBarriers.size(), std::size_t{1});
        nr::test::requireEqual(packet.bufferBarriers.size(), std::size_t{1});
        nr::test::requireEqual(packet.imageBarriers.size(), std::size_t{0});
        nr::test::requireEqual(packet.dependencyInfo().memoryBarrierCount, 1u);
        nr::test::require(packet.dependencyInfo().pMemoryBarriers == packet.memoryBarriers.data(), "dependency info should point at packet-owned memory");
        nr::test::require(
            (packet.dependencyInfo().dependencyFlags &
             vk::DependencyFlagBits::eQueueFamilyOwnershipTransferUseAllStagesKHR) != vk::DependencyFlags{},
            "dependency info should preserve maintenance8 QFOT stage semantics");

        batch.clear();
        nr::test::require(batch.empty(), "clear should empty all barrier arrays");
        nr::test::require(
            batch.buildDependencyInfo().dependencyInfo().dependencyFlags == vk::DependencyFlags{},
            "clear should reset dependency flags");
    }};

const nr::test::CaseRegistrar commandBatchFrameBoundaryCase{
    "rhi command batch owns frame-boundary submit metadata",
    [] {
        auto batch = nr::rhi::CommandBatch{};
        auto flags = vk::FrameBoundaryFlagsEXT{};
        flags |= vk::FrameBoundaryFlagBitsEXT::eFrameEnd;

        auto images = std::array{vk::Image{}};
        batch.setFrameBoundary(
            42u,
            flags,
            std::span<const vk::Image>{images.data(), images.size()});

        auto frameBoundary = batch.frameBoundarySubmitInfo();
        nr::test::require(batch.hasFrameBoundary(), "batch should retain frame-boundary metadata");
        nr::test::require(frameBoundary.has_value(), "batch should build a frame-boundary view");
        auto submitInfo = batch.submitInfo2View(std::addressof(*frameBoundary));
        nr::test::require(submitInfo.pNext != nullptr, "submit info should expose frame-boundary pNext");

        auto const* boundary = static_cast<const vk::FrameBoundaryEXT*>(submitInfo.pNext);
        nr::test::require(boundary->sType == vk::StructureType::eFrameBoundaryEXT);
        nr::test::requireEqual(boundary->frameID, std::uint64_t{42});
        nr::test::require(
            (boundary->flags & vk::FrameBoundaryFlagBitsEXT::eFrameEnd) != vk::FrameBoundaryFlagsEXT{},
            "final submit should carry eFrameEnd");
        nr::test::requireEqual(boundary->imageCount, 1u);
        nr::test::require(boundary->pImages != nullptr, "frame-boundary images should point to batch-owned storage");

        batch.clearFrameBoundary();
        auto clearedFrameBoundary = batch.frameBoundarySubmitInfo();
        auto clearedSubmitInfo = batch.submitInfo2View();
        nr::test::require(!batch.hasFrameBoundary(), "clearFrameBoundary should remove metadata");
        nr::test::require(!clearedFrameBoundary.has_value(), "cleared batch should not build a frame-boundary view");
        nr::test::require(clearedSubmitInfo.pNext == nullptr, "cleared submit should not expose frame-boundary pNext");
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
