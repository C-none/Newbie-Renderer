import std;
import nr.rhi;

namespace
{
using nr::rhi::CommandBatch;

[[nodiscard]] bool testSubmitInfo2PacketCounts()
{
    CommandBatch batch{};
    batch.addCommandBuffer(vk::CommandBuffer{});
    batch.addCommandBuffer(vk::CommandBuffer{});

    batch.addWait(vk::Semaphore{}, vk::PipelineStageFlagBits2::eComputeShader, 3);
    batch.addSignal(vk::Semaphore{}, 7, 0, vk::PipelineStageFlagBits2::eAllGraphics);

    auto packet = batch.buildSubmitInfo2();
    auto const& info = packet.info();

    if (info.commandBufferInfoCount != 2)
        return false;
    if (info.waitSemaphoreInfoCount != 1)
        return false;
    if (info.signalSemaphoreInfoCount != 1)
        return false;

    if (packet.waitInfos.front().stageMask != vk::PipelineStageFlagBits2::eComputeShader)
        return false;
    if (packet.waitInfos.front().value != 3)
        return false;
    if (packet.signalInfos.front().value != 7)
        return false;

    return true;
}

[[nodiscard]] bool testBarrierBatchCounts()
{
    nr::rhi::ops::BarrierBatch barriers{};
    barriers.add(vk::MemoryBarrier2{
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderRead,
        nullptr,
    });

    auto packet = barriers.buildDependencyInfo();
    auto const& info = packet.dependencyInfo();

    if (info.memoryBarrierCount != 1)
        return false;
    if (info.bufferMemoryBarrierCount != 0)
        return false;
    if (info.imageMemoryBarrierCount != 0)
        return false;

    return true;
}

[[nodiscard]] bool testCompileTimeContracts()
{
    static_assert(nr::rhi::FrameContext::kMaxSecondaryWorkers > 0);

    static_assert(requires(nr::rhi::FrameContext& frame) {
        frame.prepareSecondaryPools();
    });

    static_assert(std::is_invocable_v<
                  decltype(&nr::rhi::ops::pipelineBarrier),
                  vk::CommandBuffer,
                  const nr::rhi::ops::BarrierBatch&>);

    static_assert(std::is_invocable_v<
                  decltype(&nr::rhi::ops::copyBuffer),
                  vk::CommandBuffer,
                  const nr::rhi::Buffer&,
                  const nr::rhi::Buffer&,
                  vk::DeviceSize>);

    static_assert(std::is_invocable_v<
                  decltype(&nr::rhi::ops::makeBufferTransferWriteToShaderReadBarrier),
                  const nr::rhi::Buffer&,
                  vk::PipelineStageFlags2,
                  vk::DeviceSize,
                  vk::DeviceSize>);

    static_assert(std::is_invocable_v<
                  decltype(&nr::rhi::ops::makeImageTransferDstToShaderReadBarrier),
                  const nr::rhi::Image&,
                  vk::PipelineStageFlags2>);

    static_assert(std::is_invocable_v<
                  decltype(&nr::rhi::ops::makeImageColorAttachmentToPresentBarrier),
                  const nr::rhi::Image&>);

    static_assert(requires(
        const nr::rhi::ShaderCursor &cursor,
        const nr::rhi::Buffer &buffer,
        nr::rhi::Buffer &mutableBuffer,
        const nr::rhi::Image &image,
        vk::BufferView texelView,
        vk::Sampler sampler,
        vk::AccelerationStructureKHR accelerationStructure,
        const nr::rhi::LogicalResourceDescriptorWrite &logicalResource,
        std::span<const uint8_t> bytes) {
        { cursor.setData(bytes) } -> std::same_as<bool>;
        { cursor.setObject(buffer, vk::DeviceSize{0}, vk::WholeSize) } -> std::same_as<bool>;
        { cursor.setObject(texelView) } -> std::same_as<bool>;
        { cursor.setObject(mutableBuffer, vk::Format::eR32Sfloat) } -> std::same_as<bool>;
        { cursor.setObject(image, vk::ImageLayout::eGeneral) } -> std::same_as<bool>;
        { cursor.setObject(sampler) } -> std::same_as<bool>;
        { cursor.setObject(image, sampler, vk::ImageLayout::eShaderReadOnlyOptimal) } -> std::same_as<bool>;
        { cursor.setObject(accelerationStructure) } -> std::same_as<bool>;
        { cursor.setObject(logicalResource) } -> std::same_as<bool>;
        { cursor.snapshot() } -> std::same_as<nr::rhi::ShaderBindingSnapshot>;
        cursor.clearSnapshot();
    });

    static_assert(std::is_invocable_v<
                  decltype(&nr::rhi::resolveDescriptorWriteRequests),
                  const nr::rhi::ShaderBindingSnapshot &,
                  nr::rhi::LogicalDescriptorResolver>);

    using BindWithSetsOverload = void (*) (
        vk::CommandBuffer,
        vk::PipelineBindPoint,
        const nr::rhi::CursorPipelineLayout &,
        nr::rhi::ShaderBindingPool &,
        std::span<const nr::rhi::ShaderBindingSet>,
        const nr::rhi::ShaderBindingSnapshot &,
        nr::rhi::LogicalDescriptorResolver);
    [[maybe_unused]] BindWithSetsOverload bindWithSets = &nr::rhi::bindResourcesToCommandBuffer;

    using BindAllocateOverload = std::vector<nr::rhi::ShaderBindingSet> (*) (
        vk::CommandBuffer,
        vk::PipelineBindPoint,
        const nr::rhi::CursorPipelineLayout &,
        nr::rhi::ShaderBindingPool &,
        const nr::rhi::ShaderBindingSnapshot &,
        nr::rhi::LogicalDescriptorResolver);
    [[maybe_unused]] BindAllocateOverload bindAndAllocate = &nr::rhi::bindResourcesToCommandBuffer;

    using PushReplayOverload = void (*) (
        vk::CommandBuffer,
        const nr::rhi::CursorPipelineLayout &,
        const nr::rhi::ShaderBindingSnapshot &);
    [[maybe_unused]] PushReplayOverload replayPushConstants = &nr::rhi::pushConstantsToCommandBuffer;

    using AllocateSetsOverload = std::vector<nr::rhi::ShaderBindingSet> (*) (
        const nr::rhi::CursorPipelineLayout &,
        nr::rhi::ShaderBindingPool &);
    [[maybe_unused]] AllocateSetsOverload allocateSets = &nr::rhi::allocateBindingSetsForLayout;

    return true;
}
} // namespace

int main()
{
    if (!testCompileTimeContracts())
    {
        std::println("[FAIL] compile-time contract test returned false");
        return 1;
    }

    if (!testSubmitInfo2PacketCounts())
    {
        std::println("[FAIL] submit info2 packet contract failed");
        return 2;
    }

    if (!testBarrierBatchCounts())
    {
        std::println("[FAIL] barrier batch contract failed");
        return 3;
    }

    std::println("[OK] stage1 interface tests passed");
    return 0;
}
