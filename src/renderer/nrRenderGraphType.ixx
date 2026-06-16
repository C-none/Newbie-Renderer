export module nr.renderer:renderGraphType;
import dependency;

import nr.rhi;
import std;
import :rendererType;

export namespace nr::renderer
{
inline constexpr std::uint32_t kInvalidGraphHandleValue = std::numeric_limits<std::uint32_t>::max();

template <typename TTag>
struct GraphHandle
{
    std::uint32_t value = kInvalidGraphHandleValue;

    [[nodiscard]] bool valid() const noexcept
    {
        return value != kInvalidGraphHandleValue;
    }

    auto operator<=>(const GraphHandle&) const = default;
};

using GraphResourceHandle = GraphHandle<struct GraphResourceTag>;
using GraphPassHandle = GraphHandle<struct GraphPassTag>;
using GraphNodeHandle = GraphHandle<struct GraphNodeTag>;
using GraphSubmitHandle = GraphHandle<struct GraphSubmitTag>;

struct GraphImportedResourceDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
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
    /// When set, the render graph executor will use this buffer directly instead of
    /// looking it up in the importedBuffers map. This enables nodes to pre-allocate
    /// per-frame-slot resources at initialize time and import them into the graph
    /// at build time, avoiding runtime memory allocations.
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> importedResource{};
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
    ImageAspectIntent aspect = ImageAspectIntent::Color;

    /// Optional reference to a pre-allocated image resource held by the node.
    /// When set, the render graph executor will use this image directly instead of
    /// looking it up in the importedImages map. This enables nodes to pre-allocate
    /// per-frame-slot resources at initialize time and import them into the graph
    /// at build time, avoiding runtime memory allocations.
    std::optional<std::reference_wrapper<const nr::rhi::Image>> importedResource{};
};

struct GraphImportedSwapchainImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::SwapchainRelative;
    ResourceResidency residency = ResourceResidency::Swapchain;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Compute;
    std::uint32_t swapchainImageIndex = 0;
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

using GraphResourceDescVariant = std::variant<
    GraphImportedBufferDesc,
    GraphImportedImageDesc,
    GraphImportedSwapchainImageDesc,
    GraphTransientBufferDesc,
    GraphTransientImageDesc>;

struct GraphResourceDesc
{
    GraphResourceHandle handle{};
    GraphResourceDescVariant desc{};
};

struct PassResourceUseDesc
{
    GraphResourceHandle resource{};

    std::optional<BufferUsageIntent> bufferUsage = std::nullopt;
    std::optional<BufferAccessIntent> bufferAccess = std::nullopt;

    std::optional<ImageUsageIntent> imageUsage = std::nullopt;
    std::optional<ImageAccessIntent> imageAccess = std::nullopt;
    std::optional<ImageLayoutIntent> imageLayout = std::nullopt;
    std::optional<ImageAspectIntent> imageAspect = std::nullopt;

    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
    bool readOnly = false;
};

struct PassBufferResource
{
    vk::Buffer buffer = vk::Buffer{};
    vk::DeviceSize size = 0;
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> resource{};
};

struct PassImageResource
{
    vk::Image image = vk::Image{};
    vk::ImageView view = vk::ImageView{};
    vk::Extent3D extent{1, 1, 1};
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,
        0,
        1,
        0,
        1,
    };
    std::optional<std::reference_wrapper<const nr::rhi::Image>> resource{};
};

struct PassPrepareContext
{
    std::uint32_t frameIndex = 0;
    std::optional<std::reference_wrapper<nr::rhi::Device>> device{};

    std::function<std::optional<PassBufferResource>(GraphResourceHandle)> resolveBuffer{};
    std::function<std::optional<PassImageResource>(GraphResourceHandle)> resolveImage{};
};

using PassPrepareCallback = std::function<void(const PassPrepareContext&)>;

struct PassRecordContext
{
    std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer{};
    std::uint32_t frameIndex = 0;
    std::optional<std::reference_wrapper<nr::rhi::Device>> device{};

    std::function<std::optional<PassBufferResource>(GraphResourceHandle)> resolveBuffer{};
    std::function<std::optional<PassImageResource>(GraphResourceHandle)> resolveImage{};
};

using PassRecordCallback = std::function<void(const PassRecordContext&)>;

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
    std::vector<PassResourceUseDesc> resourceUses{};
    PassPrepareCallback prepare{};
    PassRecordCallback record{};
};

struct SubmitBoundaryDesc
{
    GraphSubmitHandle handle{};
    std::string debugName{};
    SubmitBoundaryKind kind = SubmitBoundaryKind::Explicit;
};

using GraphExecutionStep = std::variant<GraphPassHandle, GraphSubmitHandle>;

struct RenderGraphFrameDescription
{
    std::vector<GraphResourceDesc> resources{};
    std::vector<GraphNodeDesc> nodes{};
    std::vector<PassExecutionDesc> passes{};
    std::vector<SubmitBoundaryDesc> submitBoundaries{};
    std::vector<GraphExecutionStep> executionOrder{};
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

    [[nodiscard]] bool resolved() const noexcept
    {
        return stages != vk::PipelineStageFlags2{};
    }
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
};

struct CompiledResourceDesc
{
    GraphResourceHandle handle{};
    std::string debugName{};

    bool isBuffer = false;
    bool isImage = false;
    bool isSwapchain = false;

    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    ResourceResidency residency = ResourceResidency::Managed;

    vk::DeviceSize resolvedBufferSize = 0;
    vk::Extent3D resolvedExtent{1, 1, 1};
    vk::Format resolvedFormat = vk::Format::eUndefined;
    ImageAspectIntent resolvedAspect = ImageAspectIntent::Color;

    vk::BufferUsageFlags resolvedBufferUsage{};
    vk::ImageUsageFlags resolvedImageUsage{};

    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    ImageLayoutIntent finalLayout = ImageLayoutIntent::Undefined;

    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    ResourceOwnershipDomain finalOwnership = ResourceOwnershipDomain::Undefined;
    nr::rhi::MemoryUsage resolvedBufferMemoryUsage = nr::rhi::MemoryUsage::GpuOnly;

    /// Optional reference to a pre-allocated imported buffer held by the node.
    /// Populated from GraphImportedBufferDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> importedBufferResource{};

    /// Optional reference to a pre-allocated imported image held by the node.
    /// Populated from GraphImportedImageDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<const nr::rhi::Image>> importedImageResource{};
};

struct CompiledPass
{
    GraphPassHandle handle{};
    GraphNodeHandle node{};
    std::string debugName{};

    bool isCopyPass = false;
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t submitBatchIndex = 0;

    std::vector<PassResourceUseDesc> resourceUses{};
    std::vector<std::size_t> resolvedResourceIndices{};
    std::vector<ResourceStateTransition> preBarriers{};
    PassPrepareCallback prepare{};
    PassRecordCallback record{};
};

struct CompiledSubmitBatch
{
    std::uint32_t batchIndex = 0;
    QueueDomain queue = QueueDomain::Graphics;
    std::optional<GraphSubmitHandle> openedBySubmitNode{};
    std::vector<CompiledPass> passes{};
};

struct CompiledGraphFrame
{
    std::vector<CompiledResourceDesc> resources{};
    std::vector<CompiledSubmitBatch> submitBatches{};
    std::vector<ResourceStateTransition> ownershipTransitions{};
    std::string debugView{};
};
} // namespace nr::renderer
