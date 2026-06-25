export module nr.renderer:renderer;
import dependency.math;
import dependency.vulkan;

import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :frameServices;
import :renderGraphBuilder;
import :renderGraphCompiler;
import :renderGraphExecutor;
import :rendererSubmission;

export namespace nr::renderer
{
struct RendererCreateInfo
{
    std::string appName = "NewbieRenderer";
    std::string engineName = "NewbieRenderer";
    vk::DeviceSize frameUniformBytesPerFrame = 1024u * 1024u;
};

struct NodePort
{
    std::string name{};
};

struct NodeConfig
{
    std::string instanceName{};
    QueueDomain queue = QueueDomain::Graphics;
};

struct NodeDescription
{
    std::string name{};
    std::vector<NodePort> inputPorts{};
    std::vector<NodePort> outputPorts{};
};

struct NodeFrameParameters
{
    std::uint32_t frameIndex = 0;
    std::uint32_t swapchainImageIndex = 0;
    vk::Extent2D swapchainExtent{1, 1};
    vk::Format swapchainFormat = vk::Format::eUndefined;

    std::optional<GraphFrameDataHandle> sceneBridgeFrameHandle{};
    std::optional<std::reference_wrapper<const nr::scene::ScenePacketSet>> scenePackets{};
    std::optional<std::reference_wrapper<const nr::scene::SceneResolvedCamera>> primaryCamera{};
    std::optional<std::reference_wrapper<FrameServices>> frameServices{};
};

struct RendererCameraOverride
{
    nr::scene::SceneBridgeFrameConstants frameConstants{};
    nr::scene::SceneFrustum frustum{};
};

struct RendererCpuFrameTimings
{
    double cpuWaitGpuMilliseconds = 0.0;
    double frameSetupMilliseconds = 0.0;
    double sceneMilliseconds = 0.0;
    double buildMilliseconds = 0.0;
    double compileMilliseconds = 0.0;
    double prepareMilliseconds = 0.0;
    double executeMilliseconds = 0.0;
    double presentMilliseconds = 0.0;
    double totalMilliseconds = 0.0;
};

struct RendererCpuStatistics
{
    RendererCpuFrameTimings average{};
    std::uint32_t pendingSampleFrameCount = 0;
    std::uint32_t averagedFrameCount = 0;
    bool valid = false;
};

struct NodeInitContext
{
    std::reference_wrapper<nr::rhi::Device> device;
};

struct NodeShutdownContext
{
    std::reference_wrapper<nr::rhi::Device> device;
};

class FrameUniformArena;

struct FrameUniformBinding
{
    GraphResourceHandle resource{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = 0;
};

struct FrameGlobalResources
{
    FrameUniformBinding frameUniform{};
};

struct NodeBuildContext
{
    std::reference_wrapper<RenderGraphBuilder> graphBuilder;
    GraphNodeHandle nodeHandle{};
    std::uint32_t frameIndex = 0;
    std::reference_wrapper<const FrameGlobalResources> globalResources;
    std::function<GraphResourceHandle(std::string_view)> resolveInputPort;
    std::function<void(std::string_view, GraphResourceHandle)> publishOutputPort;

    [[nodiscard]] GraphResourceHandle resolveInput(std::string_view portName) const;

    void publishOutput(std::string_view portName, GraphResourceHandle resource);

    // Node-scoped graph authoring helpers: Generic resource addition interface.
    template <typename TDesc>
    [[nodiscard]] GraphResourceHandle addResource(const TDesc& desc)
    {
        return graphBuilder.get().addResource(desc);
    }

    template <typename TPayload>
    [[nodiscard]] GraphFrameDataHandle importFrameData(std::string_view debugName, TPayload&& payload)
    {
        return graphBuilder.get().addFrameData(debugName, std::forward<TPayload>(payload));
    }

    [[nodiscard]] GraphResourceHandle transientColor(
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format);

    [[nodiscard]] GraphResourceHandle importColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importStorageColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importSampledColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importDepth(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    template <std::size_t FrameSlotCount>
    [[nodiscard]] GraphResourceHandle importFrameColor(
        const std::array<nr::rhi::Image, FrameSlotCount>& images,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format)
    {
        auto const frameSlot = frameSlotIndex(FrameSlotCount);
        auto const& image = images[frameSlot];
        nrAssert(image.valid(), std::format("{} frame image slot {} is invalid.", debugName, frameSlot));

        return importColor(
            image,
            indexedFrameDebugName(debugName, frameSlot),
            extent,
            format,
            ResourceLifetime::FrameLocal);
    }

    template <std::size_t FrameSlotCount>
    [[nodiscard]] GraphResourceHandle importFrameStorageColor(
        const std::array<nr::rhi::Image, FrameSlotCount>& images,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format)
    {
        auto const frameSlot = frameSlotIndex(FrameSlotCount);
        auto const& image = images[frameSlot];
        nrAssert(image.valid(), std::format("{} frame storage image slot {} is invalid.", debugName, frameSlot));

        return importStorageColor(
            image,
            indexedFrameDebugName(debugName, frameSlot),
            extent,
            format,
            ResourceLifetime::FrameLocal);
    }

    template <std::size_t FrameSlotCount>
    [[nodiscard]] GraphResourceHandle importFrameDepth(
        const std::array<nr::rhi::Image, FrameSlotCount>& images,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format)
    {
        auto const frameSlot = frameSlotIndex(FrameSlotCount);
        auto const& image = images[frameSlot];
        nrAssert(image.valid(), std::format("{} frame depth image slot {} is invalid.", debugName, frameSlot));

        return importDepth(
            image,
            indexedFrameDebugName(debugName, frameSlot),
            extent,
            format,
            ResourceLifetime::FrameLocal);
    }

    [[nodiscard]] GraphResourceHandle importAccelerationStructure(
        const nr::rhi::AccelerationStructureResource& accelerationStructure,
        std::string_view debugName,
        ResourceLifetime lifetime = ResourceLifetime::ScenePersistent,
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined);

    [[nodiscard]] GraphResourceHandle importSwapchain(
        std::string_view debugName,
        const NodeFrameParameters& frameParameters);

    [[nodiscard]] GraphPassHandle addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback = nullptr,
        bool isCopyPass = false);

    [[nodiscard]] GraphSubmitHandle addSubmitNode(
        std::string_view debugName);

  private:
    [[nodiscard]] GraphResourceHandle importImage(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime,
        std::initializer_list<ImageUsageIntent> usageIntents,
        ImageAspectIntent aspect = ImageAspectIntent::Color);

    [[nodiscard]] std::size_t frameSlotIndex(std::size_t frameSlotCount) const;

    [[nodiscard]] static std::string indexedFrameDebugName(std::string_view debugName, std::size_t frameSlot);
};

class FrameUniformArena
{
  public:
    FrameUniformArena() = default;

    FrameUniformArena(const FrameUniformArena&) = delete;
    FrameUniformArena& operator=(const FrameUniformArena&) = delete;
    FrameUniformArena(FrameUniformArena&&) noexcept = default;
    FrameUniformArena& operator=(FrameUniformArena&&) noexcept = default;

    void initialize(nr::rhi::Device& device, vk::DeviceSize bytesPerFrame, std::string_view debugName);

    void beginFrame(std::uint32_t frameIndex);

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] FrameUniformBinding uploadBytes(
        RenderGraphBuilder& graphBuilder,
        std::string_view debugName,
        std::span<const std::byte> bytes);

    template <typename TPayload>
    requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
    [[nodiscard]] FrameUniformBinding upload(RenderGraphBuilder& graphBuilder, std::string_view debugName, const TPayload& value)
    {
        auto bytes = std::as_bytes(std::span{&value, 1});
        return uploadBytes(graphBuilder, debugName, bytes);
    }

  private:
    [[nodiscard]] static vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept;

    std::string debugName_{};
    nr::rhi::Buffer buffer_{};
    vk::DeviceSize frameSliceSize_ = 0;
    vk::DeviceSize currentFrameBaseOffset_ = 0;
    vk::DeviceSize currentFrameCursor_ = 0;
    vk::DeviceSize uniformOffsetAlignment_ = 1;
    vk::DeviceSize maxUniformBufferRange_ = std::numeric_limits<vk::DeviceSize>::max();
};

template <typename TPipeline, std::size_t FrameSlotCount = nr::maxFrameInFlight>
class PipelineRuntime
{
  public:
    PipelineRuntime() = default;

    PipelineRuntime(const PipelineRuntime&) = delete;
    PipelineRuntime& operator=(const PipelineRuntime&) = delete;
    PipelineRuntime(PipelineRuntime&&) noexcept = default;
    PipelineRuntime& operator=(PipelineRuntime&&) noexcept = default;

    ~PipelineRuntime();

    void initialize(nr::rhi::PipelineState<TPipeline> pipelineState);

    void initializeDeferred(nr::rhi::PipelineState<TPipeline> pipelineState);

    void clearBindingSets();

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] nr::rhi::PipelineState<TPipeline>& state() noexcept;

    [[nodiscard]] const nr::rhi::PipelineState<TPipeline>& state() const noexcept;

    [[nodiscard]] TPipeline& pipeline() noexcept;

    [[nodiscard]] const TPipeline& pipeline() const noexcept;

    [[nodiscard]] std::span<const nr::rhi::ShaderBindingSet> bindingSetsForFrame(std::uint32_t frameIndex) const;

    [[nodiscard]] bool ensureBindingSetsForFrame(
        std::uint32_t frameIndex,
        const std::map<std::uint32_t, std::uint32_t>& variableDescriptorCountsBySet);

    [[nodiscard]] nr::rhi::ShaderCursor rootCursor() const;

  private:
    void allocateBindingSetsForFrame(
        std::size_t frameSlot,
        const std::map<std::uint32_t, std::uint32_t>& variableDescriptorCountsBySet);

    nr::rhi::PipelineState<TPipeline> pipeline_{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, FrameSlotCount> bindingSetsByFrame_{};
    std::array<std::map<std::uint32_t, std::uint32_t>, FrameSlotCount> variableDescriptorCountsByFrame_{};
};

template <typename TPipeline, std::size_t FrameSlotCount>
PipelineRuntime<TPipeline, FrameSlotCount>::~PipelineRuntime()
{
    clearBindingSets();
}

template <typename TPipeline, std::size_t FrameSlotCount>
void PipelineRuntime<TPipeline, FrameSlotCount>::initialize(nr::rhi::PipelineState<TPipeline> pipelineState)
{
    clearBindingSets();
    pipeline_ = std::move(pipelineState);
    nrAssert(pipeline_.layout.valid(), "PipelineRuntime::initialize requires a valid cursor pipeline layout.");

    auto frameSlots = std::views::iota(std::size_t{0}, bindingSetsByFrame_.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        allocateBindingSetsForFrame(frameSlot, {});
    });
}

template <typename TPipeline, std::size_t FrameSlotCount>
void PipelineRuntime<TPipeline, FrameSlotCount>::initializeDeferred(nr::rhi::PipelineState<TPipeline> pipelineState)
{
    clearBindingSets();
    pipeline_ = std::move(pipelineState);
    nrAssert(pipeline_.layout.valid(), "PipelineRuntime::initializeDeferred requires a valid cursor pipeline layout.");
}

template <typename TPipeline, std::size_t FrameSlotCount>
void PipelineRuntime<TPipeline, FrameSlotCount>::clearBindingSets()
{
    std::ranges::for_each(bindingSetsByFrame_, [](auto& bindingSets) {
        bindingSets.clear();
    });
    std::ranges::for_each(variableDescriptorCountsByFrame_, [](auto& variableDescriptorCounts) {
        variableDescriptorCounts.clear();
    });
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] bool PipelineRuntime<TPipeline, FrameSlotCount>::valid() const noexcept
{
    return pipeline_.pipeline.valid();
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] nr::rhi::PipelineState<TPipeline>& PipelineRuntime<TPipeline, FrameSlotCount>::state() noexcept
{
    return pipeline_;
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] const nr::rhi::PipelineState<TPipeline>& PipelineRuntime<TPipeline, FrameSlotCount>::state() const noexcept
{
    return pipeline_;
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] TPipeline& PipelineRuntime<TPipeline, FrameSlotCount>::pipeline() noexcept
{
    return pipeline_.pipeline;
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] const TPipeline& PipelineRuntime<TPipeline, FrameSlotCount>::pipeline() const noexcept
{
    return pipeline_.pipeline;
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] std::span<const nr::rhi::ShaderBindingSet> PipelineRuntime<TPipeline, FrameSlotCount>::bindingSetsForFrame(std::uint32_t frameIndex) const
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % bindingSetsByFrame_.size());
    auto const& bindingSets = bindingSetsByFrame_[frameSlot];
    return std::span<const nr::rhi::ShaderBindingSet>{bindingSets.data(), bindingSets.size()};
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] bool PipelineRuntime<TPipeline, FrameSlotCount>::ensureBindingSetsForFrame(
    std::uint32_t frameIndex,
    const std::map<std::uint32_t, std::uint32_t>& variableDescriptorCountsBySet)
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % bindingSetsByFrame_.size());
    if (!bindingSetsByFrame_[frameSlot].empty() &&
        variableDescriptorCountsByFrame_[frameSlot] == variableDescriptorCountsBySet)
    {
        return false;
    }

    allocateBindingSetsForFrame(frameSlot, variableDescriptorCountsBySet);
    return true;
}

template <typename TPipeline, std::size_t FrameSlotCount>
[[nodiscard]] nr::rhi::ShaderCursor PipelineRuntime<TPipeline, FrameSlotCount>::rootCursor() const
{
    return pipeline_.descriptorLayout.rootCursor();
}

template <typename TPipeline, std::size_t FrameSlotCount>
void PipelineRuntime<TPipeline, FrameSlotCount>::allocateBindingSetsForFrame(
    std::size_t frameSlot,
    const std::map<std::uint32_t, std::uint32_t>& variableDescriptorCountsBySet)
{
    nrAssert(frameSlot < bindingSetsByFrame_.size(), "PipelineRuntime frame slot is out of range.");
    bindingSetsByFrame_[frameSlot] = nr::rhi::allocateBindingSetsForLayout(
        pipeline_.layout,
        pipeline_.bindingPool,
        variableDescriptorCountsBySet);
    variableDescriptorCountsByFrame_[frameSlot] = variableDescriptorCountsBySet;
}

struct RasterPassRecordContext
{
    const PassRecordContext& pass;
    const vk::raii::CommandBuffer& commandBuffer;
    const nr::rhi::ShaderDescriptorLayout& descriptorLayout;
    const nr::rhi::CursorPipelineLayout& pipelineLayout;
    vk::Extent2D extent{1, 1};

    template <typename TPayload>
    void pushConstants(std::string_view shaderPath, const TPayload& value) const
    {
        auto root = descriptorLayout.rootCursor();
        auto cursor = root.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));

        auto bindingSnapshot = root.snapshot();
        root.clearSnapshot();
        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipelineLayout, bindingSnapshot);
    }
};

using RasterPassRecordCallback = std::function<void(const RasterPassRecordContext&)>;

struct RasterColorAttachment
{
    GraphResourceHandle resource{};
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;
    vk::ClearValue clearValue{};
};

struct RasterDepthAttachment
{
    GraphResourceHandle resource{};
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eLoad;
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;
    vk::AttachmentLoadOp stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    vk::ClearDepthStencilValue clearValue{1.0f, 0u};
};

using RasterPassPrepareCallback = std::function<void(const PassPrepareContext&)>;
using PassBindingSnapshotCallback = std::function<nr::rhi::ShaderBindingSnapshot(const PassPrepareContext&)>;

struct DynamicBindingSnapshotDesc
{
    PassBindingSnapshotCallback snapshot{};
    nr::rhi::LogicalDescriptorResolver resolver{};
};

class RasterPassBuilder
{
  public:
    RasterPassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime);

    RasterPassBuilder& viewport(vk::Extent2D extent);

    RasterPassBuilder& colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue);

    RasterPassBuilder& depthAttachment(GraphResourceHandle resource);

    RasterPassBuilder& uniform(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);

    RasterPassBuilder& uniform(std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName);

    RasterPassBuilder& sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);

    RasterPassBuilder& storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);

    template <typename TPayload>
    RasterPassBuilder& pushConstants(std::string_view shaderPath, const TPayload& value)
    {
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));
        return *this;
    }

    RasterPassBuilder& rasterState(nr::rhi::MeshRasterState state);

    RasterPassBuilder& primitiveTopology(vk::PrimitiveTopology topology);

    RasterPassBuilder& record(RasterPassRecordCallback callback);

    RasterPassBuilder& resourceUse(PassResourceUseDesc resourceUse);

    RasterPassBuilder& prepare(RasterPassPrepareCallback callback);

    RasterPassBuilder& dynamicBindingSnapshot(
        PassBindingSnapshotCallback snapshotCallback,
        nr::rhi::LogicalDescriptorResolver resolver = {});

    [[nodiscard]] GraphPassHandle build();

  private:
    [[nodiscard]] static vk::Extent2D resolveTargetExtent(
        std::optional<vk::Extent2D> viewportExtent,
        std::span<const PassImageResource> resolvedColors,
        const std::optional<PassImageResource>& resolvedDepth);

    std::reference_wrapper<NodeBuildContext> context_;
    std::string debugName_{};
    std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime_{};
    nr::rhi::ShaderCursor rootCursor_{};
    std::optional<vk::Extent2D> viewportExtent_{};
    std::vector<RasterColorAttachment> colorAttachments_{};
    std::optional<RasterDepthAttachment> depthAttachment_{};
    std::vector<PassResourceUseDesc> resourceUses_{};
    nr::rhi::MeshRasterState rasterState_{};
    vk::PrimitiveTopology primitiveTopology_ = vk::PrimitiveTopology::eTriangleList;
    RasterPassRecordCallback recordCallback_{};
    std::vector<RasterPassPrepareCallback> prepareCallbacks_{};
    std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots_{};
};

struct ComputePassRecordContext
{
    const PassRecordContext& pass;
    const vk::raii::CommandBuffer& commandBuffer;
    const nr::rhi::ShaderDescriptorLayout& descriptorLayout;
    const nr::rhi::CursorPipelineLayout& pipelineLayout;

    template <typename TPayload>
    void pushConstants(std::string_view shaderPath, const TPayload& value) const
    {
        auto root = descriptorLayout.rootCursor();
        auto cursor = root.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));

        auto bindingSnapshot = root.snapshot();
        root.clearSnapshot();
        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipelineLayout, bindingSnapshot);
    }
};

using ComputePassRecordCallback = std::function<void(const ComputePassRecordContext&)>;
using ComputePassPrepareCallback = std::function<void(const PassPrepareContext&)>;

class ComputePassBuilder
{
  public:
    ComputePassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime);

    ComputePassBuilder& sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);

    ComputePassBuilder& storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);

    template <typename TPayload>
    ComputePassBuilder& pushConstants(std::string_view shaderPath, const TPayload& value)
    {
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));
        return *this;
    }

    ComputePassBuilder& resourceUse(PassResourceUseDesc resourceUse);

    ComputePassBuilder& prepare(ComputePassPrepareCallback callback);

    ComputePassBuilder& dynamicBindingSnapshot(
        PassBindingSnapshotCallback snapshotCallback,
        nr::rhi::LogicalDescriptorResolver resolver = {});

    ComputePassBuilder& record(ComputePassRecordCallback callback);

    [[nodiscard]] GraphPassHandle build();

  private:
    std::reference_wrapper<NodeBuildContext> context_;
    std::string debugName_{};
    std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime_{};
    nr::rhi::ShaderCursor rootCursor_{};
    std::vector<PassResourceUseDesc> resourceUses_{};
    ComputePassRecordCallback recordCallback_{};
    std::vector<ComputePassPrepareCallback> prepareCallbacks_{};
    std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots_{};
};

class NodeRuntime
{
  public:
    virtual ~NodeRuntime() = default;

    [[nodiscard]] virtual NodeDescription describe() const = 0;

        // Stage 1 (initialize): create persistent node state.
        // Typical work: shader/pipeline creation and long-lived GPU allocations.
    virtual void initialize(NodeInitContext&);

        // Stage 2 (build): declare per-frame intents and register execute lambdas.
        // Canonical path: context.addPass(intentList, name, executeLambda[, isCopyPass]).
        // Build should capture stable per-pass snapshots used later by execute lambdas.
    virtual void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) = 0;

        // Stage 3 (shutdown): release persistent node state.
    virtual void shutdown(NodeShutdownContext&);
};

struct NodeCreateInfo
{
    std::shared_ptr<NodeRuntime> runtime{};
    NodeConfig config{};
};

struct NodePortRef
{
    std::string nodeName{};
    std::string portName{};
};

struct NodeConnection
{
    NodePortRef from{};
    NodePortRef to{};
};

struct SubmitNodeSpec
{
    std::string debugName{};
    std::size_t afterNodeIndex = 0;
};

struct RendererGraphSpec
{
    std::vector<NodeCreateInfo> nodes{};
    std::vector<NodeConnection> connections{};
    std::vector<SubmitNodeSpec> submitNodes{};
};

struct RendererFrameInput
{
    std::optional<std::reference_wrapper<nr::scene::Scene>> scene{};
    std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max();
    std::optional<nr::scene::SceneExtractInput> sceneExtractInput{};
    std::optional<RendererCameraOverride> cameraOverride{};
    std::optional<std::reference_wrapper<FrameServices>> frameServices{};
};

struct RendererFrameResult
{
    bool rendered = false;
    std::uint32_t frameIndex = 0;
    std::uint32_t swapchainImageIndex = 0;
    vk::Result presentResult = vk::Result::eSuccess;

    std::size_t compiledSubmitBatchCount = 0;
    std::size_t submittedBatchCount = 0;

    std::size_t invokedPassPrepareCount = 0;
    std::size_t invokedPassRecordCount = 0;
    std::size_t appliedInPassBarrierCount = 0;
    std::size_t appliedAcquireBarrierCount = 0;
    std::size_t appliedReleaseBarrierCount = 0;

    bool syntheticPresentBatchUsed = false;

    bool usedScenePath = false;
    bool usedCameraOverride = false;
    bool sceneExtractProfileCreated = false;
    std::size_t sceneBridgeDrawCount = 0;
    std::size_t sceneRasterPacketCount = 0;
    std::size_t sceneRtPacketCount = 0;
    std::size_t sceneTlasPacketCount = 0;
    RendererCpuStatistics cpuStatistics{};
    RendererGpuPassStatistics gpuPassStatistics{};
};

class Renderer
{
  public:
    Renderer() = default;

    void initialize(const RendererCreateInfo& info = {});

    void installGraph(const RendererGraphSpec& spec);

    void shutdown();

    [[nodiscard]] bool initialized() const noexcept;

    [[nodiscard]] bool graphInstalled() const noexcept;

    void resize();

    [[nodiscard]] RendererFrameResult renderFrame(const RendererFrameInput& input = {});

    [[nodiscard]] nr::rhi::Device& device();

    [[nodiscard]] const nr::rhi::Device& device() const;

    [[nodiscard]] RenderGraphExecutor& graphExecutor() noexcept;

    [[nodiscard]] const RenderGraphExecutor& graphExecutor() const noexcept;

    [[nodiscard]] const RendererCpuStatistics& cpuStatistics() const noexcept;

    [[nodiscard]] const RendererGpuPassStatistics& gpuPassStatistics() const noexcept;

  private:
    struct InstalledNode
    {
        std::shared_ptr<NodeRuntime> runtime{};
        NodeDescription description{};
        NodeConfig config{};
        std::string runtimeName{};
    };

    [[nodiscard]] static std::string makePortKey(std::string_view nodeName, std::string_view portName);

    void buildInstalledGraph(
        const NodeFrameParameters& frameParameters,
        const nr::scene::SceneBridgeFrameConstants& frameConstants,
        std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame);

    void teardownInstalledGraph();

    [[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> ensureSceneExtractProfile(nr::scene::Scene& scene);

    void recordCpuTimingSample(const RendererCpuFrameTimings& timings) noexcept;

    void recordGpuPassTimingSample(const GpuPassTimingFrame& timings);

    std::unique_ptr<nr::rhi::Device> device_{};
    RenderGraphBuilder builder_{};
    RenderGraphCompiler compiler_{};
    RenderGraphExecutor executor_{};
    RendererSubmissionTimeline submissionTimeline_{};
    FrameUniformArena frameUniformArena_{};

    bool graphInstalled_ = false;
    std::vector<InstalledNode> installedNodes_{};
    std::map<std::string, std::size_t> nodeIndexByName_{};
    std::map<std::string, std::string> connectionsByTargetPort_{};
    std::multimap<std::size_t, SubmitNodeSpec> submitNodesByAfterIndex_{};

    std::optional<std::reference_wrapper<nr::scene::Scene>> activeScene_{};
    std::optional<nr::scene::SceneExtractProfileHandle> sceneExtractProfile_{};
    RendererCpuFrameTimings cpuTimingAccumulator_{};
    RendererCpuStatistics cpuStatistics_{};
    std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage> gpuPassTimingAccumulator_{};
    RendererGpuPassStatistics gpuPassStatistics_{};
};
} // namespace nr::renderer
