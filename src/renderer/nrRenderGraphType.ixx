export module nr.renderer:renderGraphType;
import dependency.vulkan;

import nr.rhi;
import nr.utils;
import std;
import :rendererType;

export namespace nr::renderer
{
inline constexpr std::uint32_t kInvalidGraphHandleValue = std::numeric_limits<std::uint32_t>::max();

template <typename TTag> struct GraphHandle
{
    std::uint32_t value = kInvalidGraphHandleValue;

    [[nodiscard]] bool valid() const noexcept
    {
        return value != kInvalidGraphHandleValue;
    }

    auto operator<=>(const GraphHandle &) const = default;
};

using GraphResourceHandle = GraphHandle<struct GraphResourceTag>;
using GraphFrameDataHandle = GraphHandle<struct GraphFrameDataTag>;
using GraphPassHandle = GraphHandle<struct GraphPassTag>;
using GraphNodeHandle = GraphHandle<struct GraphNodeTag>;
using GraphSubmitHandle = GraphHandle<struct GraphSubmitTag>;

struct GraphFrameDataDesc
{
    GraphFrameDataHandle handle{};
    std::string debugName{};
    std::any payload{};
};

struct GraphImportedBufferDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    vk::DeviceSize size = 0;
    std::vector<BufferUsageIntent> usageIntents{};

    /// Optional reference to a pre-allocated buffer resource held by the node.
    /// When set, the render graph executor resolves this buffer directly at prepare
    /// time. This enables nodes to pre-allocate per-frame-slot resources at
    /// initialize time and import them into the graph at build time, avoiding
    /// runtime memory allocations.
    std::optional<std::reference_wrapper<const nr::rhi::Buffer>> importedResource{};

    /// Optional retained state for persistent imported buffers reused across frames.
    std::optional<std::reference_wrapper<struct RetainedBufferState>> retainedState{};
};

/**
 * @brief Precise sync2 stage+access scope for one side of a barrier.
 *
 * An empty `stages` mask marks the scope as unresolved; barrier emission then
 * falls back to a conservative all-commands scope for that side only.
 */
struct AccessScope
{
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlags2{};
    vk::AccessFlags2 access = vk::AccessFlags2{};

    [[nodiscard]] bool resolved() const noexcept;
    [[nodiscard]] bool operator==(const AccessScope &) const = default;
};

struct RetainedExternalResourceState
{
    bool initialized = false;
    ResourceOwnershipDomain ownership = ResourceOwnershipDomain::Undefined;
    AccessScope access{};
    std::uint64_t lastSubmissionTimelineValue = 0;

    void reset() noexcept;
};

struct RetainedBufferState
{
    RetainedExternalResourceState common{};

    void reset() noexcept;
};

struct RetainedImageState
{
    RetainedExternalResourceState common{};
    ImageLayoutIntent layout = ImageLayoutIntent::Undefined;

    void reset() noexcept;
};

struct RetainedAccelerationStructureState
{
    RetainedExternalResourceState common{};

    void reset() noexcept;
};

struct GraphImportedImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
    std::vector<ImageUsageIntent> usageIntents{};
    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    AccessScope initialAccessScope{};
    ImageAspectIntent aspect = ImageAspectIntent::Color;

    /// Optional reference to a pre-allocated image resource held by the node.
    /// When set, the render graph executor resolves this image directly at prepare
    /// time. This enables nodes to pre-allocate per-frame-slot resources at
    /// initialize time and import them into the graph at build time, avoiding
    /// runtime memory allocations.
    std::optional<std::reference_wrapper<const nr::rhi::Image>> importedResource{};

    /// Optional retained state for renderer-persistent imported images reused across frames.
    std::optional<std::reference_wrapper<RetainedImageState>> retainedState{};
};

struct GraphImportedAccelerationStructureDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eTopLevel;
    vk::DeviceSize size = 0;
    std::vector<AccelerationStructureUsageIntent> usageIntents{};

    /// Optional reference to a pre-built acceleration structure held by the node or renderer cache.
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>> importedResource{};

    /// Optional retained state for the acceleration structure backing storage.
    std::optional<std::reference_wrapper<RetainedAccelerationStructureState>> retainedState{};
};

struct GraphImportedSwapchainImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::SwapchainRelative;
    ResourceResidency residency = ResourceResidency::Swapchain;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Compute;
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
};

struct GraphTransientBufferDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    vk::DeviceSize size = 0;
    std::vector<BufferUsageIntent> usageIntents{};
    nr::rhi::MemoryUsage memoryUsage = nr::rhi::MemoryUsage::GpuOnly;
};

struct GraphTransientImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
    std::vector<ImageUsageIntent> usageIntents{};
    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    ImageAspectIntent aspect = ImageAspectIntent::Color;
};

using GraphResourceDescVariant =
    std::variant<GraphImportedBufferDesc, GraphImportedImageDesc, GraphImportedAccelerationStructureDesc,
                 GraphImportedSwapchainImageDesc, GraphTransientBufferDesc, GraphTransientImageDesc>;

struct GraphResourceDesc
{
    GraphResourceHandle handle{};
    GraphResourceDescVariant desc{};
};

struct PassResourceUseDesc
{
    GraphResourceHandle resource{};

    std::optional<BufferUsageIntent> bufferUsage{};
    std::optional<BufferAccessIntent> bufferAccess{};

    std::optional<AccelerationStructureUsageIntent> accelerationStructureUsage{};
    std::optional<AccelerationStructureAccessIntent> accelerationStructureAccess{};

    std::optional<ImageUsageIntent> imageUsage{};
    std::optional<ImageAccessIntent> imageAccess{};
    std::optional<ImageLayoutIntent> imageLayout{};
    std::optional<ImageAspectIntent> imageAspect{};

    vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{};
    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
    bool requiresPreviousUseBarrier = false;

    [[nodiscard]] bool operator==(const PassResourceUseDesc &) const = default;
};

enum class CopyBufferDestinationIntent : std::uint8_t
{
    TransferDst,
    Readback,
};

struct CopyBufferToBufferPassDesc
{
    GraphResourceHandle source{};
    GraphResourceHandle destination{};
    vk::BufferCopy region{};
    CopyBufferDestinationIntent destinationIntent = CopyBufferDestinationIntent::TransferDst;

    [[nodiscard]] bool operator==(const CopyBufferToBufferPassDesc &) const = default;
};

struct CopyBufferToImagePassDesc
{
    GraphResourceHandle sourceBuffer{};
    GraphResourceHandle destinationImage{};
    vk::BufferImageCopy region{};
    std::optional<ImageAspectIntent> imageAspect{};

    [[nodiscard]] bool operator==(const CopyBufferToImagePassDesc &) const = default;
};

struct CopyImageToBufferPassDesc
{
    GraphResourceHandle sourceImage{};
    GraphResourceHandle destinationBuffer{};
    vk::BufferImageCopy region{};
    std::optional<ImageAspectIntent> imageAspect{};
    CopyBufferDestinationIntent destinationIntent = CopyBufferDestinationIntent::TransferDst;
    vk::DeviceSize destinationBufferRangeSize = 0;

    [[nodiscard]] bool operator==(const CopyImageToBufferPassDesc &) const = default;
};

struct CopyImageToImagePassDesc
{
    GraphResourceHandle source{};
    GraphResourceHandle destination{};
    vk::ImageCopy region{};
    std::optional<ImageAspectIntent> sourceAspect{};
    std::optional<ImageAspectIntent> destinationAspect{};
    bool presentDestination = false;

    [[nodiscard]] bool operator==(const CopyImageToImagePassDesc &) const = default;
};

using CopyPassDesc = std::variant<CopyBufferToBufferPassDesc, CopyBufferToImagePassDesc, CopyImageToBufferPassDesc,
                                  CopyImageToImagePassDesc>;

namespace use
{
[[nodiscard]] vk::PipelineStageFlags2 shaderStageScope(ShaderStageIntent intent) noexcept;

[[nodiscard]] vk::PipelineStageFlags2 shaderStageScope(std::span<const ShaderStageIntent> intents) noexcept;

[[nodiscard]] PassResourceUseDesc withShaderStages(PassResourceUseDesc use, vk::PipelineStageFlags2 stages) noexcept;

[[nodiscard]] PassResourceUseDesc withShaderStages(PassResourceUseDesc use, ShaderStageIntent stage) noexcept;

[[nodiscard]] PassResourceUseDesc withShaderStages(PassResourceUseDesc use,
                                                   std::initializer_list<ShaderStageIntent> stages) noexcept;

[[nodiscard]] PassResourceUseDesc colorRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc orderedAfterPrevious(PassResourceUseDesc use) noexcept;

[[nodiscard]] PassResourceUseDesc colorWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc colorReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc depthRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc depthWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc depthReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc sampledRead(GraphResourceHandle resource,
                                              ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc storageRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc inputAttachmentRead(GraphResourceHandle resource,
                                                      ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc uniformRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc bufferTransferSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc bufferTransferDst(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageBufferRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageBufferWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageBufferReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc vertexRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc indexRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc indirectRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc uniformTexelRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageTexelRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageTexelWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageTexelReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildInputRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureScratchWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureTraceRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc shaderBindingTableRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc hostUploadRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc readbackWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc imageTransferSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc imageTransferDst(GraphResourceHandle resource,
                                                   ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc copySource(GraphResourceHandle resource,
                                             ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc copyDestination(GraphResourceHandle resource,
                                                  ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc resolveSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc resolveDst(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc presentRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc presentRead(GraphResourceHandle resource,
                                              ResourceOwnershipDomain ownershipDomain) noexcept;
} // namespace use

struct PassBufferResource
{
    vk::Buffer buffer = vk::Buffer{};
    vk::DeviceSize size = 0;
    std::optional<std::reference_wrapper<const nr::rhi::Buffer>> resource{};
};

struct PassImageResource
{
    vk::Image image = vk::Image{};
    vk::ImageView view = vk::ImageView{};
    vk::Extent3D extent{1, 1, 1};
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1,
    };
    std::optional<std::reference_wrapper<const nr::rhi::Image>> resource{};
};

struct PassAccelerationStructureResource
{
    vk::AccelerationStructureKHR accelerationStructure{};
    vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eTopLevel;
    vk::DeviceSize size = 0;
    vk::Buffer storageBuffer{};
    vk::DeviceSize storageOffset = 0;
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>> resource{};
};

struct PassPrepareContext
{
    std::uint32_t frameIndex = 0;
    std::optional<std::reference_wrapper<nr::rhi::Device>> device{};

    std::function<std::optional<PassBufferResource>(GraphResourceHandle)> resolveBuffer{};
    std::function<std::optional<PassImageResource>(GraphResourceHandle)> resolveImage{};
    std::function<std::optional<PassAccelerationStructureResource>(GraphResourceHandle)> resolveAccelerationStructure{};
    std::function<std::optional<std::reference_wrapper<const std::any>>(GraphFrameDataHandle)>
        resolveFrameDataPayload{};

    template <typename TPayload>
    [[nodiscard]] std::optional<std::reference_wrapper<const std::remove_cvref_t<TPayload>>> resolveFrameData(
        GraphFrameDataHandle handle) const
    {
        using Payload = std::remove_cvref_t<TPayload>;

        nrAssert(handle.valid(), "PassPrepareContext::resolveFrameData requires a valid frame data handle.");
        nrAssert(static_cast<bool>(resolveFrameDataPayload),
                 "PassPrepareContext::resolveFrameData requires a frame data resolver callback.");

        auto payload = resolveFrameDataPayload(handle);
        if (!payload.has_value())
        {
            return {};
        }

        auto const typedPayload = std::any_cast<Payload>(&payload->get());
        nrAssert(typedPayload != nullptr,
                 "PassPrepareContext::resolveFrameData resolved unexpected payload type for frame data handle {}.",
                 handle.value);
        return std::cref(*typedPayload);
    }

    template <typename TPayload>
    [[nodiscard]] const std::remove_cvref_t<TPayload> &frameData(GraphFrameDataHandle handle) const
    {
        auto resolved = resolveFrameData<TPayload>(handle);
        nrAssert(resolved.has_value(), "PassPrepareContext::frameData failed to resolve frame data handle {}.",
                 handle.value);
        return resolved->get();
    }
};

using PassPrepareCallback = std::function<void(const PassPrepareContext &)>;

struct PassRecordContext
{
    std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer{};
    std::uint32_t frameIndex = 0;
    std::optional<std::reference_wrapper<nr::rhi::Device>> device{};

    std::function<std::optional<PassBufferResource>(GraphResourceHandle)> resolveBuffer{};
    std::function<std::optional<PassImageResource>(GraphResourceHandle)> resolveImage{};
    std::function<std::optional<PassAccelerationStructureResource>(GraphResourceHandle)> resolveAccelerationStructure{};
    std::function<std::optional<std::reference_wrapper<const std::any>>(GraphFrameDataHandle)>
        resolveFrameDataPayload{};

    template <typename TPayload>
    [[nodiscard]] std::optional<std::reference_wrapper<const std::remove_cvref_t<TPayload>>> resolveFrameData(
        GraphFrameDataHandle handle) const
    {
        using Payload = std::remove_cvref_t<TPayload>;

        nrAssert(handle.valid(), "PassRecordContext::resolveFrameData requires a valid frame data handle.");
        nrAssert(static_cast<bool>(resolveFrameDataPayload),
                 "PassRecordContext::resolveFrameData requires a frame data resolver callback.");

        auto payload = resolveFrameDataPayload(handle);
        if (!payload.has_value())
        {
            return {};
        }

        auto const typedPayload = std::any_cast<Payload>(&payload->get());
        nrAssert(typedPayload != nullptr,
                 "PassRecordContext::resolveFrameData resolved unexpected payload type for frame data handle {}.",
                 handle.value);
        return std::cref(*typedPayload);
    }

    template <typename TPayload>
    [[nodiscard]] const std::remove_cvref_t<TPayload> &frameData(GraphFrameDataHandle handle) const
    {
        auto resolved = resolveFrameData<TPayload>(handle);
        nrAssert(resolved.has_value(), "PassRecordContext::frameData failed to resolve frame data handle {}.",
                 handle.value);
        return resolved->get();
    }
};

using PassRecordCallback = std::function<void(const PassRecordContext &)>;

enum class ParallelRecordReplaySemantics : std::uint8_t
{
    Unordered,
};

enum class PassPrimaryRecordScopeKind : std::uint8_t
{
    None,
    DynamicRenderingSecondaryContents,
};

struct ParallelRecordRange
{
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return end - begin;
    }
};

struct PassParallelRecordPlan
{
    std::size_t itemCount = 0;
    std::uint32_t assignedThreadCount = 0;
    std::vector<ParallelRecordRange> ranges{};
};

struct PassDynamicRenderingSecondaryScope
{
    vk::Rect2D renderArea{};
    std::uint32_t layerCount = 1;
    std::uint32_t viewMask = 0;
    vk::RenderingFlags flags{};
    std::vector<nr::rhi::ops::RenderingAttachmentDesc> colorAttachments{};
    std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc> depthAttachment{};
    std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc> stencilAttachment{};
    std::vector<vk::Format> colorAttachmentFormats{};
    vk::Format depthAttachmentFormat = vk::Format::eUndefined;
    vk::Format stencilAttachmentFormat = vk::Format::eUndefined;
    vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1;

    [[nodiscard]] nr::rhi::ops::RenderingScopeDesc renderingScope() const noexcept
    {
        return nr::rhi::ops::RenderingScopeDesc{
            .renderArea = renderArea,
            .layerCount = layerCount,
            .viewMask = viewMask,
            .flags = flags | vk::RenderingFlagBits::eContentsSecondaryCommandBuffers,
            .colorAttachments = std::span<const nr::rhi::ops::RenderingAttachmentDesc>{colorAttachments.data(),
                                                                                       colorAttachments.size()},
            .depthAttachment = depthAttachment,
            .stencilAttachment = stencilAttachment,
        };
    }

    [[nodiscard]] vk::CommandBufferInheritanceRenderingInfo inheritanceRenderingInfo() const noexcept
    {
        auto info = vk::CommandBufferInheritanceRenderingInfo{};
        info.flags = flags;
        info.viewMask = viewMask;
        info.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachmentFormats.size());
        info.pColorAttachmentFormats = colorAttachmentFormats.data();
        info.depthAttachmentFormat = depthAttachmentFormat;
        info.stencilAttachmentFormat = stencilAttachmentFormat;
        info.rasterizationSamples = rasterizationSamples;
        return info;
    }
};

struct PassPrimaryRecordScope
{
    PassPrimaryRecordScopeKind kind = PassPrimaryRecordScopeKind::None;
    PassDynamicRenderingSecondaryScope dynamicRendering{};
};

struct ParallelRecordPlanner
{
    [[nodiscard]] static PassParallelRecordPlan planContiguousRanges(std::size_t itemCount,
                                                                     std::uint32_t availableRecordWorkers);
};

struct PassRangeRecordContext
{
    PassRecordContext pass{};
    std::reference_wrapper<const vk::raii::CommandBuffer> commandBuffer;
    std::reference_wrapper<const PassParallelRecordPlan> plan;
    std::size_t chunkIndex = 0;
    ParallelRecordRange range{};
};

using PassParallelRecordItemCountCallback = std::function<std::size_t(const PassRecordContext &)>;
using PassParallelRecordPrimaryScopeCallback = std::function<PassPrimaryRecordScope(const PassRecordContext &)>;
using PassParallelRecordRangeCallback = std::function<void(const PassRangeRecordContext &)>;

struct PassParallelRecordDesc
{
    ParallelRecordReplaySemantics replaySemantics = ParallelRecordReplaySemantics::Unordered;
    PassParallelRecordItemCountCallback itemCount{};
    PassParallelRecordPrimaryScopeCallback primaryScope{};
    PassParallelRecordRangeCallback recordRange{};
};

struct GraphNodeDesc
{
    GraphNodeHandle handle{};
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
};

struct PassExecutionDesc
{
    GraphPassHandle handle{};
    GraphNodeHandle node{};
    std::string debugName{};
    bool isCopyPass = false;
    QueueDomain queue = QueueDomain::Graphics;
    vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{};
    std::optional<CopyPassDesc> copy{};
    std::vector<PassResourceUseDesc> resourceUses{};
    std::vector<GraphFrameDataHandle> frameDataUses{};
    PassPrepareCallback prepare{};
    PassRecordCallback record{};
    std::optional<PassParallelRecordDesc> parallelRecord{};
};

enum class SubmitBoundaryKind : std::uint8_t
{
    QueueSubmission,
    SwapchainAcquire,
};

struct SubmitBoundaryDesc
{
    GraphSubmitHandle handle{};
    std::string debugName{};
    SubmitBoundaryKind kind = SubmitBoundaryKind::QueueSubmission;
};

using GraphExecutionStep = std::variant<GraphPassHandle, GraphSubmitHandle>;

struct RenderGraphFrameDescription
{
    std::vector<GraphResourceDesc> resources{};
    std::vector<GraphFrameDataDesc> frameData{};
    std::vector<GraphNodeDesc> nodes{};
    std::vector<PassExecutionDesc> passes{};
    std::vector<SubmitBoundaryDesc> submitBoundaries{};
    std::vector<GraphExecutionStep> executionOrder{};
};

struct ResourceStateTransition
{
    GraphResourceHandle resource{};
    QueueDomain srcQueue = QueueDomain::Graphics;
    QueueDomain dstQueue = QueueDomain::Graphics;
    ImageLayoutIntent oldLayout = ImageLayoutIntent::Undefined;
    ImageLayoutIntent newLayout = ImageLayoutIntent::Undefined;
    DependencyStrength strength = DependencyStrength::InOrder;

    /// Producer-side scope (last access before the transition). Unresolved on first use.
    AccessScope srcScope{};
    /// Consumer-side scope (first access after the transition).
    AccessScope dstScope{};

    /// Prior-frame source-queue timeline value used by an implicit initial acquire.
    std::uint64_t sourceSubmissionTimelineValue = 0;
};

struct CompiledResourceDesc
{
    GraphResourceHandle handle{};
    std::string debugName{};

    bool isBuffer = false;
    bool isImage = false;
    bool isAccelerationStructure = false;
    bool isSwapchain = false;

    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    ResourceResidency residency = ResourceResidency::Managed;

    vk::DeviceSize resolvedBufferSize = 0;
    vk::DeviceSize resolvedAccelerationStructureSize = 0;
    vk::Extent3D resolvedExtent{1, 1, 1};
    vk::Format resolvedFormat = vk::Format::eUndefined;
    vk::AccelerationStructureTypeKHR resolvedAccelerationStructureType = vk::AccelerationStructureTypeKHR::eTopLevel;
    ImageAspectIntent resolvedAspect = ImageAspectIntent::Color;

    vk::BufferUsageFlags resolvedBufferUsage{};
    vk::ImageUsageFlags resolvedImageUsage{};

    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    ImageLayoutIntent finalLayout = ImageLayoutIntent::Undefined;
    AccessScope initialAccessScope{};
    AccessScope finalAccessScope{};
    bool initialStateInitialized = false;

    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    ResourceOwnershipDomain finalOwnership = ResourceOwnershipDomain::Undefined;
    nr::rhi::MemoryUsage resolvedBufferMemoryUsage = nr::rhi::MemoryUsage::GpuOnly;

    /// Optional reference to a pre-allocated imported buffer held by the node.
    /// Populated from GraphImportedBufferDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<const nr::rhi::Buffer>> importedBufferResource{};

    /// Optional reference to a pre-allocated imported image held by the node.
    /// Populated from GraphImportedImageDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<const nr::rhi::Image>> importedImageResource{};

    /// Optional retained external-resource states updated after successful submission.
    std::optional<std::reference_wrapper<RetainedBufferState>> retainedBufferState{};
    std::optional<std::reference_wrapper<RetainedImageState>> retainedImageState{};
    std::optional<std::reference_wrapper<RetainedAccelerationStructureState>> retainedAccelerationStructureState{};
    /// Compatibility alias for image-only callers; mirrors retainedImageState.
    std::optional<std::reference_wrapper<RetainedImageState>> retainedState{};

    /// Optional reference to a pre-built acceleration structure held by the node or renderer cache.
    /// Populated from GraphImportedAccelerationStructureDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>>
        importedAccelerationStructureResource{};
};

struct CompiledPass
{
    GraphPassHandle handle{};
    GraphNodeHandle node{};
    std::string debugName{};

    bool isCopyPass = false;
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t submitBatchIndex = 0;
    vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{};

    std::optional<CopyPassDesc> copy{};
    std::vector<PassResourceUseDesc> resourceUses{};
    std::vector<GraphFrameDataHandle> frameDataUses{};
    std::vector<std::size_t> resolvedResourceIndices{};
    std::vector<ResourceStateTransition> preBarriers{};
    PassPrepareCallback prepare{};
    PassRecordCallback record{};
    std::optional<PassParallelRecordDesc> parallelRecord{};
};

struct CompiledSubmitBatch
{
    std::uint32_t batchIndex = 0;
    QueueDomain queue = QueueDomain::Graphics;
    std::optional<GraphSubmitHandle> openedBySubmitNode{};
    std::string openedBySubmitNodeDebugName{};
    SubmitBoundaryKind openedBySubmitNodeKind = SubmitBoundaryKind::QueueSubmission;
    std::vector<CompiledPass> passes{};
};

struct CompiledGraphFrame
{
    std::vector<CompiledResourceDesc> resources{};
    std::vector<GraphFrameDataDesc> frameData{};
    std::vector<CompiledSubmitBatch> submitBatches{};
    std::vector<ResourceStateTransition> ownershipTransitions{};
    std::string debugView{};
};

struct GpuPassTimingSample
{
    GraphPassHandle pass{};
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t batchIndex = 0u;
    bool isCopyPass = false;
    double milliseconds = 0.0;
};

struct GpuPassTimingFrame
{
    // This is the renderer's monotonic frame ordinal, not a recycled frame-slot index.
    std::uint64_t frameOrdinal = 0;
    std::size_t expectedPassCount = 0u;
    std::size_t availablePassCount = 0u;
    bool complete = false;
    std::vector<GpuPassTimingSample> passes{};
};

struct RendererGpuPassAverage
{
    GraphPassHandle pass{};
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
    bool isCopyPass = false;
    double milliseconds = 0.0;
    std::uint32_t sampleCount = 0;
};

struct RendererGpuPassStatistics
{
    std::vector<RendererGpuPassAverage> averages{};
    std::uint32_t pendingSampleFrameCount = 0;
    std::uint32_t averagedFrameCount = 0;
    bool valid = false;
};
} // namespace nr::renderer
