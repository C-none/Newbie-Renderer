export module nr.renderer:renderGraphExecutor;
import dependency.vulkan;

import nr.rhi;
import nr.utils;
import std;
import :renderGraphCompiler;
import :renderGraphType;
import :rendererSubmission;

namespace nr::renderer
{
struct RecordTaskResult
{
    std::size_t batchOrdinal = 0;
    std::size_t passOrdinal = 0;
    std::size_t chunkIndex = 0;
    std::size_t chunkCount = 1;
    std::uint32_t workerId = 0;
    QueueDomain queue = QueueDomain::Graphics;
    bool parallel = false;
    vk::CommandBuffer commandBuffer{};
    std::size_t invokedPassRecordCount = 0;
    std::size_t appliedInPassBarrierCount = 0;
};
} // namespace nr::renderer

namespace nr::renderer::detail
{
using nr::rhi::ScopedCommandBufferDebugLabel;

inline constexpr std::uint32_t kMainThreadSecondaryPoolSlot = 0;
inline constexpr std::uint32_t kWorkerSecondaryPoolSlotBase = kMainThreadSecondaryPoolSlot + 1;

[[nodiscard]] std::uint32_t secondaryPoolSlotForRecordWorker(std::uint32_t recordWorkerId) noexcept;

[[nodiscard]] std::string_view queueDomainLabel(QueueDomain queue) noexcept;

[[nodiscard]] std::string nodeScopeLabel(std::string_view passDebugName);

[[nodiscard]] std::string rendererBatchScopeLabel(
    std::uint32_t batchIndex,
    QueueDomain queue,
    std::string_view openedBySubmitNodeDebugName);

} // namespace nr::renderer::detail

export namespace nr::renderer
{
struct ExecutorBatchPlan
{
    std::uint32_t batchIndex = 0;
    QueueDomain queue = QueueDomain::Graphics;

    std::size_t passCount = 0;
    std::size_t inPassBarrierCount = 0;

    std::vector<ResourceStateTransition> headAcquireTransitions{};
    std::vector<ResourceStateTransition> tailReleaseTransitions{};

    vk::PipelineStageFlags2 waitStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    bool waitsForPreviousBatch = false;
    bool signalsNextBatch = false;
    bool signalsPresent = false;
    bool acquiresSwapchainBeforeSubmit = false;
};

struct ExecutorPlan
{
    std::vector<ExecutorBatchPlan> initialReleaseBatches{};
    std::vector<ExecutorBatchPlan> batches{};
    std::size_t totalPassCount = 0;
    std::size_t totalInPassBarrierCount = 0;
    bool finalQueueIsCompute = false;
    bool requiresSyntheticPresentBatch = false;
};

struct ExecuteReport
{
    ExecutorPlan plan{};

    std::optional<std::uint32_t> swapchainImageIndex{};
    vk::Result swapchainAcquireResult = vk::Result::eSuccess;

    std::size_t invokedPassPrepareCount = 0;
    std::size_t invokedPassRecordCount = 0;
    std::size_t appliedInPassBarrierCount = 0;
    std::size_t appliedAcquireBarrierCount = 0;
    std::size_t appliedReleaseBarrierCount = 0;
    std::size_t submittedBatchCount = 0;
    std::size_t submittedRecordTaskCount = 0;
    std::size_t recordedSecondaryCommandBufferCount = 0;
    std::size_t recordWorkerCount = 0;
    std::optional<GpuPassTimingFrame> completedGpuPassTimingFrame{};
    bool parallelPassRecording = false;
};

struct PreparedResourceBinding
{
    bool isBuffer = false;
    bool isImage = false;
    bool isAccelerationStructure = false;

    vk::Buffer buffer = vk::Buffer{};
    vk::DeviceSize bufferSize = 0;
    std::optional<std::reference_wrapper<const nr::rhi::Buffer>> bufferResource{};
    vk::Image image = vk::Image{};
    vk::ImageView imageView = vk::ImageView{};
    std::optional<std::reference_wrapper<const nr::rhi::Image>> imageResource{};
    vk::AccelerationStructureKHR accelerationStructure{};
    vk::AccelerationStructureTypeKHR accelerationStructureType = vk::AccelerationStructureTypeKHR::eTopLevel;
    vk::DeviceSize accelerationStructureSize = 0;
    vk::Buffer accelerationStructureStorageBuffer{};
    vk::DeviceSize accelerationStructureStorageOffset = 0;
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>> accelerationStructureResource{};

    vk::Extent3D extent{1, 1, 1};
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,
        0,
        1,
        0,
        1,
    };
};

struct PreparedGraphFrame
{
    CompiledGraphFrame compiled{};
    ExecutorPlan plan{};
    std::map<GraphResourceHandle, PreparedResourceBinding> runtimeBindings{};
    std::size_t invokedPassPrepareCount = 0;
    std::optional<std::size_t> firstDeferredPrepareBatch{};
};

template <typename TContext>
[[nodiscard]] inline std::optional<nr::rhi::DescriptorWritePayload> resolveLogicalDescriptorWriteDefault(
    const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
    const nr::rhi::DescriptorBindingInfo& binding,
    [[maybe_unused]] std::uint32_t arrayElement,
    const TContext& bindingContext)
{
    nrAssert(
        static_cast<bool>(bindingContext.resolveBuffer),
        "resolveLogicalDescriptorWriteDefault requires resolveBuffer callback.");
    nrAssert(
        static_cast<bool>(bindingContext.resolveImage),
        "resolveLogicalDescriptorWriteDefault requires resolveImage callback.");
    nrAssert(
        static_cast<bool>(bindingContext.resolveAccelerationStructure),
        "resolveLogicalDescriptorWriteDefault requires resolveAccelerationStructure callback.");

    nrAssert(
        logicalResource.logicalResourceId <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::format(
            "resolveLogicalDescriptorWriteDefault logical resource id {} exceeds GraphResourceHandle capacity.",
            logicalResource.logicalResourceId));

    auto resourceHandle = GraphResourceHandle{
        static_cast<std::uint32_t>(logicalResource.logicalResourceId),
    };

    auto resolveBufferDescriptor = [&]() -> std::optional<nr::rhi::DescriptorWritePayload> {
        auto resolvedBuffer = bindingContext.resolveBuffer(resourceHandle);
        nrAssert(
            resolvedBuffer.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical buffer '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            logicalResource.offset <= resolvedBuffer->size,
            std::format(
                "resolveLogicalDescriptorWriteDefault buffer offset out of range. name='{}' offset={} size={}",
                logicalResource.debugName,
                logicalResource.offset,
                resolvedBuffer->size));

        auto resolvedRange = logicalResource.range == std::numeric_limits<vk::DeviceSize>::max()
                                 ? resolvedBuffer->size - logicalResource.offset
                                 : logicalResource.range;
        nrAssert(
            resolvedRange > 0u,
            std::format(
                "resolveLogicalDescriptorWriteDefault buffer range must be non-zero. name='{}' offset={} range={}",
                logicalResource.debugName,
                logicalResource.offset,
                resolvedRange));
        nrAssert(
            resolvedRange <= resolvedBuffer->size - logicalResource.offset,
            std::format(
                "resolveLogicalDescriptorWriteDefault buffer range out of bounds. name='{}' offset={} range={} size={}",
                logicalResource.debugName,
                logicalResource.offset,
                resolvedRange,
                resolvedBuffer->size));

        if (binding.descriptorType == vk::DescriptorType::eStorageBuffer ||
            binding.descriptorType == vk::DescriptorType::eStorageBufferDynamic)
        {
            if (resolvedBuffer->resource.has_value())
            {
                auto const usage = resolvedBuffer->resource->get().usage();
                nrAssert(
                    (usage & vk::BufferUsageFlagBits::eStorageBuffer) != vk::BufferUsageFlags{},
                    std::format(
                        "Storage buffer descriptor '{}' at shader path '{}' resolved to buffer without eStorageBuffer usage. usage={}",
                        logicalResource.debugName,
                        binding.debugPath,
                        vk::to_string(usage)));
            }

            if (bindingContext.device.has_value())
            {
                auto const alignment = std::max<vk::DeviceSize>(
                    1u,
                    bindingContext.device->get().physicalDevice.getProperties().limits.minStorageBufferOffsetAlignment);
                nrAssert(
                    (logicalResource.offset % alignment) == 0u,
                    std::format(
                        "Storage buffer descriptor '{}' at shader path '{}' has unaligned offset {} for minStorageBufferOffsetAlignment {}.",
                        logicalResource.debugName,
                        binding.debugPath,
                        logicalResource.offset,
                        alignment));
            }
        }

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::BufferDescriptorWrite{
                .buffer = resolvedBuffer->buffer,
                .offset = logicalResource.offset,
                .range = resolvedRange,
            }};
    };

    auto resolveAccelerationStructureDescriptor = [&]() -> std::optional<nr::rhi::DescriptorWritePayload> {
        auto resolvedAccelerationStructure = bindingContext.resolveAccelerationStructure(resourceHandle);
        nrAssert(
            resolvedAccelerationStructure.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical acceleration structure '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            resolvedAccelerationStructure->accelerationStructure != vk::AccelerationStructureKHR{},
            std::format(
                "resolveLogicalDescriptorWriteDefault resolved logical acceleration structure '{}' ({}) without a valid handle.",
                logicalResource.debugName,
                logicalResource.logicalResourceId));

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::AccelerationStructureDescriptorWrite{
                .accelerationStructure = resolvedAccelerationStructure->accelerationStructure,
            }};
    };

    switch (binding.descriptorType)
    {
    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
        // Uniform descriptors use the logical offset and resolve range to [offset, bufferEnd).
        return resolveBufferDescriptor();

    case vk::DescriptorType::eStorageBuffer:
        // Storage buffer descriptors share DescriptorBufferInfo payload shape with uniforms.
        // The descriptor type itself remains `eStorageBuffer` through `binding.descriptorType`.
        return resolveBufferDescriptor();

    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eInputAttachment:
    {
        auto resolvedImage = bindingContext.resolveImage(resourceHandle);
        nrAssert(
            resolvedImage.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical image '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            resolvedImage->view != vk::ImageView{},
            std::format(
                "resolveLogicalDescriptorWriteDefault resolved logical image '{}' ({}) without a valid image view.",
                logicalResource.debugName,
                logicalResource.logicalResourceId));

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::ImageDescriptorWrite{
                .imageView = resolvedImage->view,
                .imageLayout = logicalResource.imageLayout,
            }};
    }
    case vk::DescriptorType::eSampler:
    {
        return nr::rhi::DescriptorWritePayload{
            nr::rhi::ImageDescriptorWrite{
                .imageLayout = vk::ImageLayout::eUndefined,
                .sampler = logicalResource.sampler,
            }};
    }
    case vk::DescriptorType::eCombinedImageSampler:
    {
        auto resolvedImage = bindingContext.resolveImage(resourceHandle);
        nrAssert(
            resolvedImage.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical combined-image '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            resolvedImage->view != vk::ImageView{},
            std::format(
                "resolveLogicalDescriptorWriteDefault resolved logical combined-image '{}' ({}) without a valid image view.",
                logicalResource.debugName,
                logicalResource.logicalResourceId));

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::ImageDescriptorWrite{
                .imageView = resolvedImage->view,
                .imageLayout = logicalResource.imageLayout,
                .sampler = logicalResource.sampler,
            }};
    }
    case vk::DescriptorType::eAccelerationStructureKHR:
        return resolveAccelerationStructureDescriptor();
    default:
        return std::nullopt;
    }
}

[[nodiscard]] nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(const PassRecordContext& recordContext);

[[nodiscard]] nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(const PassPrepareContext& prepareContext);

class RenderGraphExecutor
{
  public:
    struct ExecuteContext
    {
        nr::rhi::Device& device;
        std::uint32_t frameIndex = 0;
        std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max();
        std::optional<std::reference_wrapper<RendererSubmissionTimelines>> submissionTimelines{};
    };

    [[nodiscard]] ExecutorPlan buildPlan(const CompiledGraphFrame& compiled) const;

    [[nodiscard]] PreparedGraphFrame prepareFrame(const CompiledGraphFrame& compiled, const ExecuteContext& context) const;

    [[nodiscard]] PreparedGraphFrame prepareFrame(CompiledGraphFrame&& compiled, const ExecuteContext& context) const;

    [[nodiscard]] ExecuteReport execute(const CompiledGraphFrame& compiled, const ExecuteContext& context);

    [[nodiscard]] ExecuteReport executePrepared(const PreparedGraphFrame& prepared, const ExecuteContext& context);

    void clearRetainedState();

  private:
    enum class TransitionPlacement : std::uint8_t
    {
        InPass,
        Release,
        Acquire,
    };

    using RuntimeBindingMap = std::map<GraphResourceHandle, PreparedResourceBinding>;
    using CompiledResourceLookup = std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>;
    using CompiledFrameDataLookup = std::map<GraphFrameDataHandle, std::reference_wrapper<const GraphFrameDataDesc>>;

    struct RecordTaskDesc
    {
        std::size_t batchOrdinal = 0;
        std::size_t passOrdinal = 0;
        std::size_t chunkIndex = 0;
        std::uint32_t workerId = 0;
        std::uint32_t secondaryPoolSlot = detail::kWorkerSecondaryPoolSlotBase;
        QueueDomain queue = QueueDomain::Graphics;
        std::reference_wrapper<const CompiledPass> pass;
        std::reference_wrapper<const vk::raii::CommandBuffer> commandBuffer;
        std::uint32_t frameIndex = 0;
        std::reference_wrapper<nr::rhi::Device> device;
        std::reference_wrapper<const CompiledResourceLookup> compiledResourceByHandle;
        std::reference_wrapper<const RuntimeBindingMap> runtimeBindings;
        std::reference_wrapper<const CompiledFrameDataLookup> frameDataByHandle;
        bool parallel = false;
        PassParallelRecordPlan parallelPlan{};
        ParallelRecordRange range{};
        PassPrimaryRecordScope primaryScope{};
    };

    struct RecordPassExecutionPlan
    {
        bool parallel = false;
        PassParallelRecordPlan parallelPlan{};
        PassPrimaryRecordScope primaryScope{};
    };

    struct RecordBatchTasks
    {
        std::size_t batchOrdinal = 0;
        std::vector<RecordPassExecutionPlan> passPlans{};
        std::vector<std::future<RecordTaskResult>> futures{};
    };

    [[nodiscard]] static nr::rhi::QueueRole toQueueRole(QueueDomain queue);

    static void attachFrameBoundaryMetadata(
        nr::rhi::CommandBatch& submitBatch,
        const ExecuteContext& context,
        std::uint64_t frameBoundaryFrameID,
        bool isFrameEnd,
        std::optional<std::uint32_t> swapchainImageIndex);

    [[nodiscard]] static vk::PipelineStageFlags2 submissionWaitStage(QueueDomain queue);

    [[nodiscard]] static vk::PipelineStageFlags2 shaderWaitStageForQueue(QueueDomain queue);

    [[nodiscard]] static vk::PipelineStageFlags2 imageAccessWaitStage(
        QueueDomain queue,
        const PassResourceUseDesc& use,
        vk::PipelineStageFlags2 passShaderStages);

    [[nodiscard]] static vk::PipelineStageFlags2 imageAvailableWaitStageForBatch(
        const CompiledSubmitBatch& batch,
        const std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>& compiledResourceByHandle);

    static void updateRetainedImageStates(const CompiledGraphFrame& compiled);

    [[nodiscard]] static std::uint32_t queueFamilyIndexFor(const nr::rhi::Device& device, QueueDomain queue);

    [[nodiscard]] static vk::ImageSubresourceRange subresourceRangeFor(const CompiledResourceDesc& resource);

    [[nodiscard]] static std::map<GraphResourceHandle, PreparedResourceBinding> resolveRuntimeResources(
        const CompiledGraphFrame& compiled,
        const ExecuteContext& context);

    static void resolveSwapchainRuntimeResources(
        const CompiledGraphFrame& compiled,
        const ExecuteContext& context,
        std::uint32_t swapchainImageIndex,
        RuntimeBindingMap& runtimeBindings);

    [[nodiscard]] static std::size_t invokePassPrepareCallbacks(
        const CompiledGraphFrame& compiled,
        const ExecuteContext& context,
        const RuntimeBindingMap& runtimeBindings,
        std::size_t firstBatchOrdinal,
        std::size_t lastBatchOrdinal);

    [[nodiscard]] static nr::rhi::CommandPool& primaryPoolForQueue(nr::rhi::FrameContext& frame, QueueDomain queue);

    [[nodiscard]] static nr::rhi::CommandPool& secondaryPoolForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue,
        std::uint32_t secondaryPoolSlot);

    [[nodiscard]] static std::uint32_t preparedSecondaryPoolSlotCountForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue);

    [[nodiscard]] static std::uint32_t preparedRecordWorkerCountForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue);

    [[nodiscard]] static std::uint32_t resolvedRecordWorkerCount(nr::rhi::FrameContext& frame);

    struct CachedPrimaryCommandBuffer
    {
        QueueDomain queue = QueueDomain::Graphics;
        vk::raii::CommandBuffers buffers{nullptr};
    };

    struct CachedSecondaryCommandBuffer
    {
        QueueDomain queue = QueueDomain::Graphics;
        std::uint32_t secondaryPoolSlot = detail::kWorkerSecondaryPoolSlotBase;
        vk::raii::CommandBuffers buffers{nullptr};
    };

    struct FrameGpuPassTimingState
    {
        vk::raii::QueryPool queryPool{nullptr};
        std::uint32_t queryCapacity = 0;
        std::uint32_t pendingFrameIndex = 0;
        std::vector<GpuPassTimingSample> pendingPasses{};
    };

    [[nodiscard]] vk::raii::CommandBuffer& primaryCommandBufferForQueue(
        const ExecuteContext& context,
        std::size_t frameSlot,
        QueueDomain queue,
        std::size_t ordinal);

    [[nodiscard]] vk::raii::CommandBuffer& secondaryCommandBufferForPass(
        const ExecuteContext& context,
        std::size_t frameSlot,
        QueueDomain queue,
        std::size_t batchOrdinal,
        std::size_t passOrdinal,
        std::size_t chunkIndex,
        std::uint32_t secondaryPoolSlot);

    [[nodiscard]] static bool addTransitionBarrier(
        nr::rhi::ops::BarrierBatch& barriers,
        const CompiledResourceDesc& resource,
        const PreparedResourceBinding& binding,
        const ResourceStateTransition& transition,
        TransitionPlacement placement,
        std::uint32_t srcQueueFamilyIndex,
        std::uint32_t dstQueueFamilyIndex);

    [[nodiscard]] static PassRecordContext makePassRecordContext(
        std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer,
        std::uint32_t frameIndex,
        nr::rhi::Device& device,
        const RuntimeBindingMap& runtimeBindings,
        const CompiledFrameDataLookup& frameDataByHandle);

    [[nodiscard]] static RecordTaskResult recordPassToSecondary(const RecordTaskDesc& desc);

    [[nodiscard]] static RecordTaskResult recordPassRangeToSecondary(const RecordTaskDesc& desc);

    [[nodiscard]] static std::uint32_t timestampValidBitsForQueue(
        const nr::rhi::Device& device,
        QueueDomain queue);

    [[nodiscard]] static std::map<QueueDomain, std::uint32_t> timestampValidBitsForQueues(
        const nr::rhi::Device& device,
        std::span<const GpuPassTimingSample> passes);

    [[nodiscard]] static std::uint64_t timestampDeltaTicks(
        std::uint64_t begin,
        std::uint64_t end,
        std::uint32_t validBits) noexcept;

    [[nodiscard]] static constexpr std::uint32_t timingQueryCountForPassCount(std::size_t passCount) noexcept
    {
        return static_cast<std::uint32_t>(passCount * 2u);
    }

    [[nodiscard]] static constexpr std::uint32_t beginTimingQueryForPass(std::size_t passIndex) noexcept
    {
        return static_cast<std::uint32_t>(passIndex * 2u);
    }

    [[nodiscard]] static std::vector<GpuPassTimingSample> buildPassTimingSamples(
        const CompiledGraphFrame& compiled);

    static void ensureTimingQueryPool(
        const nr::rhi::Device& device,
        FrameGpuPassTimingState& state,
        std::uint32_t requiredQueryCount);

    [[nodiscard]] static std::optional<GpuPassTimingFrame> collectCompletedGpuPassTimings(
        const nr::rhi::Device& device,
        FrameGpuPassTimingState& state);

    [[nodiscard]] RecordBatchTasks launchRecordTasksForBatch(
        const ExecuteContext& context,
        std::size_t frameSlot,
        std::size_t batchOrdinal,
        const CompiledGraphFrame& compiled,
        const CompiledResourceLookup& compiledResourceByHandle,
        const RuntimeBindingMap& runtimeBindings,
        const CompiledFrameDataLookup& frameDataByHandle,
        ExecuteReport& report);

    [[nodiscard]] static std::vector<RecordTaskResult> collectRecordTaskResults(
        RecordBatchTasks& tasks,
        std::size_t batchOrdinal,
        ExecuteReport& report);

    static void executeRecordedSecondaries(
        const vk::raii::CommandBuffer& primaryCommandBuffer,
        const CompiledSubmitBatch& batch,
        std::span<const RecordPassExecutionPlan> passPlans,
        std::span<const RecordTaskResult> results,
        vk::QueryPool timingQueryPool,
        std::size_t firstTimedPassIndex,
        const CompiledResourceLookup& compiledResourceByHandle,
        const RuntimeBindingMap& runtimeBindings,
        ExecuteReport& report);

    [[nodiscard]] static vk::ImageSubresourceLayers toSubresourceLayers(const vk::ImageSubresourceRange& range);

    static void recordImplicitCopyPass(
        const CompiledPass& pass,
        const vk::raii::CommandBuffer& commandBuffer,
        const std::map<GraphResourceHandle, PreparedResourceBinding>& runtimeBindings);

    std::vector<std::vector<CachedPrimaryCommandBuffer>> primaryCommandBuffersByFrame_{};
    std::vector<std::vector<std::vector<std::vector<CachedSecondaryCommandBuffer>>>> secondaryCommandBuffersByFrame_{};
    std::vector<FrameGpuPassTimingState> gpuPassTimingStatesByFrame_{};
    nr::threading::StaticThreadPool recordThreadPool_{};
    std::uint64_t nextFrameBoundaryId_ = 1;
};
} // namespace nr::renderer
