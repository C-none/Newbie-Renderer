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
import :rendererCache;
import :rendererSubmission;

export namespace nr::renderer
{
inline constexpr std::uint32_t kSceneTextureDescriptorCapacity = 1024u;
static_assert(
    kSceneTextureDescriptorCapacity <= nr::scene::kMaxSceneTextureId + 1u,
    "Scene texture descriptor capacity must fit the packed uint16 material texture id ABI.");

struct RendererCreateInfo
{
    std::string appName = "NewbieRenderer";
    std::string engineName = "NewbieRenderer";
    vk::DeviceSize frameUniformBytesPerFrame = 1024u * 1024u;
};

struct NodeConfig
{
    std::string instanceName{};
    QueueDomain queue = QueueDomain::Graphics;
};

struct NodeDescription
{
    std::string name{};
};

namespace frameResource
{
inline constexpr std::string_view presentSourceColor = "present.sourceColor";
inline constexpr std::string_view normalDepth = "normal.depth";
inline constexpr std::string_view uiColor = "ui.color";
inline constexpr std::string_view swapchainImage = "swapchain.image";
inline constexpr std::string_view sceneTlas = "scene.tlas";
inline constexpr std::string_view sceneRtInstanceMetadata = "scene.rt.instanceMetadata";
inline constexpr std::string_view sceneRtGeometryMetadata = "scene.rt.geometryMetadata";
inline constexpr std::string_view sceneRtMaterialHeaders = "scene.rt.materialHeaders";
inline constexpr std::string_view sceneRtMaterialLayers = "scene.rt.materialLayers";
inline constexpr std::string_view sceneRtMaterialTextureRefs = "scene.rt.materialTextureRefs";
inline constexpr std::string_view sceneRtVertexAtlas = "scene.rt.vertexAtlas";
inline constexpr std::string_view sceneRtIndexAtlas = "scene.rt.indexAtlas";
} // namespace frameResource

struct NodeFrameParameters
{
    std::uint32_t frameIndex = 0;
    std::uint32_t swapchainImageIndex = 0;
    vk::Extent2D swapchainExtent{1, 1};
    vk::Format swapchainFormat = vk::Format::eUndefined;

    std::optional<GraphFrameDataHandle> sceneBridgeFrameHandle{};
    std::optional<std::reference_wrapper<const nr::scene::Scene>> scene{};
    std::optional<std::reference_wrapper<const nr::scene::ScenePacketSet>> scenePackets{};
    std::optional<std::reference_wrapper<const std::vector<nr::scene::TlasBuildInputPacket>>> sceneTlasBuildInputs{};
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
    std::map<std::uint32_t, SceneTextureDescriptorBinding> sceneTextureDescriptorsById{};
    vk::Sampler sceneTextureSampler{};
    std::uint32_t sceneTextureDescriptorCapacity = kSceneTextureDescriptorCapacity;
    std::uint64_t sceneTextureDescriptorVersion = 0;
    std::reference_wrapper<BindlessImageTableCache> bindlessImageTableCache;
};

struct NodeBuildContext
{
    std::reference_wrapper<RenderGraphBuilder> graphBuilder;
    GraphNodeHandle nodeHandle{};
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t frameIndex = 0;
    std::reference_wrapper<const FrameGlobalResources> globalResources;
    std::reference_wrapper<std::map<std::string, GraphResourceHandle>> frameResources;

    void publishFrameResource(std::string_view key, GraphResourceHandle resource) const;

    [[nodiscard]] GraphResourceHandle resolveFrameResource(std::string_view key) const;

    [[nodiscard]] GraphResourceHandle requireFrameResource(
        std::string_view key,
        std::string_view consumerDebugName) const;

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

    [[nodiscard]] GraphResourceHandle importSampledImage(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent3D extent,
        vk::Format format,
        ResourceLifetime lifetime = ResourceLifetime::RendererPersistent,
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Graphics);

    [[nodiscard]] GraphResourceHandle importDepth(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importBuffer(
        const nr::rhi::Buffer& buffer,
        std::string_view debugName,
        ResourceLifetime lifetime,
        std::initializer_list<BufferUsageIntent> usageIntents,
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined);

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

    [[nodiscard]] GraphPassHandle addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassParallelRecordDesc parallelRecord,
        PassPrepareCallback prepareCallback = nullptr);

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

    [[nodiscard]] nr::rhi::DescriptorWriteCache& descriptorWriteCacheForFrame(std::uint32_t frameIndex) noexcept;

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
    std::array<nr::rhi::DescriptorWriteCache, FrameSlotCount> descriptorWriteCachesByFrame_{};
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
    std::ranges::for_each(descriptorWriteCachesByFrame_, [](auto& descriptorWriteCache) {
        descriptorWriteCache.clear();
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
[[nodiscard]] nr::rhi::DescriptorWriteCache& PipelineRuntime<TPipeline, FrameSlotCount>::descriptorWriteCacheForFrame(std::uint32_t frameIndex) noexcept
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % descriptorWriteCachesByFrame_.size());
    return descriptorWriteCachesByFrame_[frameSlot];
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
    descriptorWriteCachesByFrame_[frameSlot].clear();
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

struct PushConstantLocation
{
    vk::ShaderStageFlags stageFlags{};
    std::uint32_t offset = 0;
    std::uint32_t maxBytes = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return stageFlags != vk::ShaderStageFlags{} && maxBytes > 0u;
    }
};

[[nodiscard]] inline PushConstantLocation resolvePushConstantLocation(
    const nr::rhi::ShaderDescriptorLayout& descriptorLayout,
    std::string_view shaderPath)
{
    auto cursor = descriptorLayout.rootCursor().getPath(shaderPath);
    auto pushConstantRange = cursor.pushConstantRange();
    nrAssert(
        pushConstantRange.has_value(),
        std::format("Push constant path '{}' must reference push-constant storage.", shaderPath));

    auto const cursorOffset = cursor.address().uniformOffset;
    auto const rangeBegin = static_cast<std::uint64_t>(pushConstantRange->offset);
    auto const rangeEnd = rangeBegin + static_cast<std::uint64_t>(pushConstantRange->size);
    nrAssert(
        cursorOffset <= std::numeric_limits<std::uint32_t>::max(),
        std::format("Push constant cursor offset overflow for '{}': {}", shaderPath, cursorOffset));
    nrAssert(
        cursorOffset >= rangeBegin && cursorOffset < rangeEnd,
        std::format(
            "Push constant path '{}' resolved outside its range. offset={}, rangeBegin={}, rangeEnd={}",
            shaderPath,
            cursorOffset,
            rangeBegin,
            rangeEnd));

    auto const remainingBytes = rangeEnd - cursorOffset;
    nrAssert(
        remainingBytes <= std::numeric_limits<std::uint32_t>::max(),
        std::format("Push constant path '{}' remaining byte count overflow: {}", shaderPath, remainingBytes));

    return PushConstantLocation{
        .stageFlags = pushConstantRange->stageFlags,
        .offset = static_cast<std::uint32_t>(cursorOffset),
        .maxBytes = static_cast<std::uint32_t>(remainingBytes),
    };
}

template <typename TPayload>
requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
void pushConstantsToLocation(
    const vk::raii::CommandBuffer& commandBuffer,
    const nr::rhi::CursorPipelineLayout& pipelineLayout,
    PushConstantLocation location,
    const TPayload& value)
{
    nrAssert(location.valid(), "pushConstantsToLocation requires a valid push-constant location.");
    auto bytes = std::as_bytes(std::span{std::addressof(value), std::size_t{1}});
    nrAssert(
        bytes.size() <= location.maxBytes,
        std::format(
            "Push constant payload exceeds reflected range. size={}, maxBytes={}, offset={}",
            bytes.size(),
            location.maxBytes,
            location.offset));

    auto const* rawBytes = reinterpret_cast<const std::uint8_t*>(bytes.data());
    pipelineLayout.pushConstants(
        commandBuffer,
        location.stageFlags,
        location.offset,
        std::span<const std::uint8_t>{rawBytes, bytes.size()});
}

struct RasterPassRangeRecordContext
{
    const PassRecordContext& pass;
    const PassParallelRecordPlan& plan;
    std::size_t chunkIndex = 0;
    ParallelRecordRange range{};
    const vk::raii::CommandBuffer& commandBuffer;
    const nr::rhi::ShaderDescriptorLayout& descriptorLayout;
    const nr::rhi::CursorPipelineLayout& pipelineLayout;
    vk::Extent2D extent{1, 1};

    [[nodiscard]] PushConstantLocation pushConstantLocation(std::string_view shaderPath) const
    {
        return resolvePushConstantLocation(descriptorLayout, shaderPath);
    }

    template <typename TPayload>
    requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
    void pushConstants(PushConstantLocation location, const TPayload& value) const
    {
        pushConstantsToLocation(commandBuffer, pipelineLayout, location, value);
    }

    template <typename TPayload>
    requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
    void pushConstants(std::string_view shaderPath, const TPayload& value) const
    {
        pushConstants(pushConstantLocation(shaderPath), value);
    }
};

using RasterPassItemCountCallback = std::function<std::size_t(const PassRecordContext&)>;
using RasterPassRangeRecordCallback = std::function<void(const RasterPassRangeRecordContext&)>;

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

enum class RasterViewportYMode : std::uint8_t
{
    FramebufferTopLeft,
    ClipSpaceYUp,
};

using ShaderVisiblePassPrepareCallback = std::function<void(const PassPrepareContext&)>;
using RasterPassPrepareCallback = ShaderVisiblePassPrepareCallback;
using ComputePassPrepareCallback = ShaderVisiblePassPrepareCallback;
using PassBindingSnapshotCallback = std::function<nr::rhi::ShaderBindingSnapshot(const PassPrepareContext&)>;

struct DynamicBindingSnapshotDesc
{
    PassBindingSnapshotCallback snapshot{};
    nr::rhi::LogicalDescriptorResolver resolver{};
};

namespace detail
{
template <typename TDerived, typename TPipeline, vk::PipelineBindPoint BindPoint>
class ShaderVisiblePassBuilderBase
{
  protected:
    using Runtime = PipelineRuntime<TPipeline>;
    using RuntimePtr = std::shared_ptr<Runtime>;

    struct CommonBuildState
    {
        std::vector<PassResourceUseDesc> resourceUses{};
        RuntimePtr runtime{};
        std::string debugName{};
        nr::rhi::ShaderBindingSnapshot bindingSnapshot{};
        PassPrepareCallback prepareCallback{};
    };

    ShaderVisiblePassBuilderBase(
        NodeBuildContext& context,
        std::string_view debugName,
        RuntimePtr runtime,
        std::string_view builderLabel)
        : context_(context)
        , debugName_(debugName)
        , runtime_(std::move(runtime))
        , builderLabel_(builderLabel)
    {
        nrAssert(
            static_cast<bool>(runtime_),
            std::format("{} requires a valid PipelineRuntime shared pointer.", builderLabel_));
        nrAssert(runtime_->valid(), std::format("{} requires initialized PipelineRuntime state.", builderLabel_));
        rootCursor_ = runtime_->rootCursor();
    }

    TDerived& uniform(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
    {
        nrAssert(resource.valid(), std::format("{}::uniform requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::uniformRead(resource));
        return derived();
    }

    TDerived& uniform(std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName)
    {
        nrAssert(binding.resource.valid(), std::format("{}::uniform requires a valid frame uniform resource.", builderLabel_));
        nrAssert(binding.range > 0u, std::format("{}::uniform requires a non-zero frame uniform range.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = binding.resource.value,
            .debugName = std::string(debugName),
            .offset = binding.offset,
            .range = binding.range,
        }));
        resourceUses_.push_back(use::uniformRead(binding.resource));
        return derived();
    }

    TDerived& sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
    {
        nrAssert(resource.valid(), std::format("{}::sampledImage requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        resourceUses_.push_back(use::sampledRead(resource));
        return derived();
    }

    TDerived& sampledImageGeneral(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
    {
        nrAssert(resource.valid(), std::format("{}::sampledImageGeneral requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
            .imageLayout = vk::ImageLayout::eGeneral,
        }));
        resourceUses_.push_back(PassResourceUseDesc{
            .resource = resource,
            .imageUsage = ImageUsageIntent::Sampled,
            .imageAccess = ImageAccessIntent::SampledRead,
            .imageLayout = ImageLayoutIntent::General,
            .readOnly = true,
        });
        return derived();
    }

    TDerived& storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
    {
        nrAssert(resource.valid(), std::format("{}::storageImage requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::storageWrite(resource));
        return derived();
    }

    TDerived& storageBuffer(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
    {
        nrAssert(resource.valid(), std::format("{}::storageBuffer requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::storageBufferRead(resource));
        return derived();
    }

    TDerived& accelerationStructure(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
    {
        nrAssert(resource.valid(), std::format("{}::accelerationStructure requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::orderedAfterPrevious(use::accelerationStructureTraceRead(resource)));
        return derived();
    }

    template <typename TPayload>
    TDerived& pushConstants(std::string_view shaderPath, const TPayload& value)
    {
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));
        return derived();
    }

    TDerived& resourceUse(PassResourceUseDesc resourceUse)
    {
        nrAssert(resourceUse.resource.valid(), std::format("{}::resourceUse requires a valid graph resource.", builderLabel_));
        resourceUses_.push_back(resourceUse);
        return derived();
    }

    TDerived& prepare(ShaderVisiblePassPrepareCallback callback)
    {
        prepareCallbacks_.push_back(std::move(callback));
        return derived();
    }

    TDerived& dynamicBindingSnapshot(
        PassBindingSnapshotCallback snapshotCallback,
        nr::rhi::LogicalDescriptorResolver resolver = {})
    {
        nrAssert(
            static_cast<bool>(snapshotCallback),
            std::format("{}::dynamicBindingSnapshot requires a snapshot callback.", builderLabel_));
        dynamicBindingSnapshots_.push_back(DynamicBindingSnapshotDesc{
            .snapshot = std::move(snapshotCallback),
            .resolver = std::move(resolver),
        });
        return derived();
    }

    [[nodiscard]] CommonBuildState takeCommonBuildState()
    {
        auto bindingSnapshot = rootCursor_.snapshot();
        rootCursor_.clearSnapshot();

        auto runtime = runtime_;
        auto prepareCallback = makePrepareCallback(
            runtime,
            bindingSnapshot,
            std::move(prepareCallbacks_),
            std::move(dynamicBindingSnapshots_),
            builderLabel_);

        return CommonBuildState{
            .resourceUses = std::move(resourceUses_),
            .runtime = std::move(runtime),
            .debugName = std::move(debugName_),
            .bindingSnapshot = std::move(bindingSnapshot),
            .prepareCallback = std::move(prepareCallback),
        };
    }

    [[nodiscard]] static PassPrepareCallback makePrepareCallback(
        RuntimePtr runtime,
        nr::rhi::ShaderBindingSnapshot bindingSnapshot,
        std::vector<ShaderVisiblePassPrepareCallback> prepareCallbacks,
        std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots,
        std::string builderLabel)
    {
        return [runtime = std::move(runtime),
                bindingSnapshot = std::move(bindingSnapshot),
                prepareCallbacks = std::move(prepareCallbacks),
                dynamicBindingSnapshots = std::move(dynamicBindingSnapshots),
                builderLabel = std::move(builderLabel)](const PassPrepareContext& prepareContext) {
            nrAssert(static_cast<bool>(runtime), std::format("{} prepare requires initialized runtime state.", builderLabel));
            std::ranges::for_each(prepareCallbacks, [&](const ShaderVisiblePassPrepareCallback& callback) {
                if (callback)
                {
                    callback(prepareContext);
                }
            });

            auto& descriptorWriteCache = runtime->descriptorWriteCacheForFrame(prepareContext.frameIndex);
            nr::rhi::updateResourcesForBindingSnapshot(
                runtime->state().bindingPool,
                runtime->bindingSetsForFrame(prepareContext.frameIndex),
                descriptorWriteCache,
                bindingSnapshot,
                makeDefaultLogicalDescriptorResolver(prepareContext));

            std::ranges::for_each(dynamicBindingSnapshots, [&](const DynamicBindingSnapshotDesc& desc) {
                auto dynamicSnapshot = desc.snapshot(prepareContext);
                auto resolver = desc.resolver ? desc.resolver : makeDefaultLogicalDescriptorResolver(prepareContext);
                nr::rhi::updateResourcesForBindingSnapshot(
                    runtime->state().bindingPool,
                    runtime->bindingSetsForFrame(prepareContext.frameIndex),
                    descriptorWriteCache,
                    dynamicSnapshot,
                    std::move(resolver));
            });
        };
    }

    static void bindPipelinePreparedResourcesAndPushConstants(
        const vk::raii::CommandBuffer& commandBuffer,
        const Runtime& runtime,
        const nr::rhi::ShaderBindingSnapshot& bindingSnapshot,
        std::uint32_t frameIndex)
    {
        commandBuffer.bindPipeline(BindPoint, runtime.pipeline().raw());

        nr::rhi::bindPreparedResourcesToCommandBuffer(
            commandBuffer,
            BindPoint,
            runtime.state().layout,
            runtime.bindingSetsForFrame(frameIndex));

        nr::rhi::pushConstantsToCommandBuffer(
            commandBuffer,
            runtime.state().layout,
            bindingSnapshot);
    }

    std::reference_wrapper<NodeBuildContext> context_;

  private:
    [[nodiscard]] TDerived& derived() noexcept
    {
        return static_cast<TDerived&>(*this);
    }

    std::string debugName_{};
    RuntimePtr runtime_{};
    nr::rhi::ShaderCursor rootCursor_{};
    std::vector<PassResourceUseDesc> resourceUses_{};
    std::vector<ShaderVisiblePassPrepareCallback> prepareCallbacks_{};
    std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots_{};
    std::string builderLabel_{};
};
} // namespace detail

class RasterPassBuilder : public detail::ShaderVisiblePassBuilderBase<
                              RasterPassBuilder,
                              nr::rhi::GraphicsPipeline,
                              vk::PipelineBindPoint::eGraphics>
{
    using Base = detail::ShaderVisiblePassBuilderBase<
        RasterPassBuilder,
        nr::rhi::GraphicsPipeline,
        vk::PipelineBindPoint::eGraphics>;

  public:
    using Base::dynamicBindingSnapshot;
    using Base::prepare;
    using Base::pushConstants;
    using Base::resourceUse;
    using Base::sampledImage;
    using Base::sampledImageGeneral;
    using Base::storageBuffer;
    using Base::storageImage;
    using Base::uniform;

    RasterPassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime);

    RasterPassBuilder& viewport(vk::Extent2D extent);

    RasterPassBuilder& viewportYMode(RasterViewportYMode mode);

    RasterPassBuilder& colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue);

    RasterPassBuilder& depthAttachment(GraphResourceHandle resource);

    RasterPassBuilder& rasterState(nr::rhi::MeshRasterState state);

    RasterPassBuilder& primitiveTopology(vk::PrimitiveTopology topology);

    RasterPassBuilder& record(RasterPassRecordCallback callback);

    RasterPassBuilder& recordParallel(
        RasterPassItemCountCallback itemCountCallback,
        RasterPassRangeRecordCallback rangeRecordCallback);

    [[nodiscard]] GraphPassHandle build();

  private:
    struct RasterPassRenderingSetup
    {
        std::vector<PassImageResource> resolvedColors{};
        std::optional<PassImageResource> resolvedDepth{};
        vk::Extent2D targetExtent{1, 1};
        std::vector<nr::rhi::ops::RenderingAttachmentDesc> colorAttachments{};
        std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc> depthAttachment{};
        std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc> stencilAttachment{};
    };

    [[nodiscard]] static vk::Extent2D resolveTargetExtent(
        std::optional<vk::Extent2D> viewportExtent,
        std::span<const PassImageResource> resolvedColors,
        const std::optional<PassImageResource>& resolvedDepth);

    [[nodiscard]] static RasterPassRenderingSetup makeRenderingSetup(
        const PassRecordContext& recordContext,
        std::span<const RasterColorAttachment> colorAttachments,
        const std::optional<RasterDepthAttachment>& depthAttachment,
        std::optional<vk::Extent2D> viewportExtent,
        std::string_view debugName);

    [[nodiscard]] static PassPrimaryRecordScope makeDynamicRenderingSecondaryScope(
        const RasterPassRenderingSetup& setup,
        const PipelineRuntime<nr::rhi::GraphicsPipeline>& runtime,
        std::string_view debugName);

    static void bindGraphicsSetup(
        const vk::raii::CommandBuffer& commandBuffer,
        const PipelineRuntime<nr::rhi::GraphicsPipeline>& runtime,
        const nr::rhi::ShaderBindingSnapshot& bindingSnapshot,
        std::uint32_t frameIndex,
        vk::Extent2D targetExtent,
        RasterViewportYMode viewportYMode,
        nr::rhi::MeshRasterState rasterState,
        vk::PrimitiveTopology primitiveTopology);

    std::optional<vk::Extent2D> viewportExtent_{};
    RasterViewportYMode viewportYMode_ = RasterViewportYMode::FramebufferTopLeft;
    std::vector<RasterColorAttachment> colorAttachments_{};
    std::optional<RasterDepthAttachment> depthAttachment_{};
    nr::rhi::MeshRasterState rasterState_{};
    vk::PrimitiveTopology primitiveTopology_ = vk::PrimitiveTopology::eTriangleList;
    RasterPassRecordCallback recordCallback_{};
    RasterPassItemCountCallback parallelItemCountCallback_{};
    RasterPassRangeRecordCallback parallelRangeRecordCallback_{};
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

class ComputePassBuilder : public detail::ShaderVisiblePassBuilderBase<
                               ComputePassBuilder,
                               nr::rhi::ComputePipeline,
                               vk::PipelineBindPoint::eCompute>
{
    using Base = detail::ShaderVisiblePassBuilderBase<
        ComputePassBuilder,
        nr::rhi::ComputePipeline,
        vk::PipelineBindPoint::eCompute>;

  public:
    using Base::dynamicBindingSnapshot;
    using Base::prepare;
    using Base::pushConstants;
    using Base::resourceUse;
    using Base::sampledImage;
    using Base::sampledImageGeneral;
    using Base::storageBuffer;
    using Base::storageImage;
    using Base::uniform;

    ComputePassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime);

    ComputePassBuilder& record(ComputePassRecordCallback callback);

    [[nodiscard]] GraphPassHandle build();

  private:
    ComputePassRecordCallback recordCallback_{};
};

struct RayTracingPassRecordContext
{
    const PassRecordContext& pass;
    const vk::raii::CommandBuffer& commandBuffer;
    const nr::rhi::ShaderDescriptorLayout& descriptorLayout;
    const nr::rhi::CursorPipelineLayout& pipelineLayout;
};

using RayTracingPassRecordCallback = std::function<void(const RayTracingPassRecordContext&)>;

class RayTracingPassBuilder : public detail::ShaderVisiblePassBuilderBase<
                                  RayTracingPassBuilder,
                                  nr::rhi::RayTracingPipeline,
                                  vk::PipelineBindPoint::eRayTracingKHR>
{
    using Base = detail::ShaderVisiblePassBuilderBase<
        RayTracingPassBuilder,
        nr::rhi::RayTracingPipeline,
        vk::PipelineBindPoint::eRayTracingKHR>;

  public:
    using Base::accelerationStructure;
    using Base::dynamicBindingSnapshot;
    using Base::prepare;
    using Base::pushConstants;
    using Base::resourceUse;
    using Base::sampledImage;
    using Base::sampledImageGeneral;
    using Base::storageBuffer;
    using Base::storageImage;
    using Base::uniform;

    RayTracingPassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime);

    RayTracingPassBuilder& record(RayTracingPassRecordCallback callback);

    [[nodiscard]] GraphPassHandle build();

  private:
    RayTracingPassRecordCallback recordCallback_{};
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

struct SubmitNodeSpec
{
    std::string debugName{};
    std::size_t afterNodeIndex = 0;
};

struct RendererGraphSpec
{
    std::vector<NodeCreateInfo> nodes{};
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

    void uninstallGraph();

    void shutdown();

    [[nodiscard]] bool initialized() const noexcept;

    [[nodiscard]] bool graphInstalled() const noexcept;

    void resize();

    void resetSceneBinding() noexcept;

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

    void buildInstalledGraph(
        const NodeFrameParameters& frameParameters,
        const nr::scene::SceneBridgeFrameConstants& frameConstants,
        std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame,
        const std::map<std::uint32_t, nr::resource::TextureHandle>& sceneTextureHandlesById);

    void teardownInstalledGraph();

    [[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> ensureSceneExtractProfile(nr::scene::Scene& scene);

    [[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> ensureSceneTlasExtractProfile(nr::scene::Scene& scene);

    void recordCpuTimingSample(const RendererCpuFrameTimings& timings) noexcept;

    void recordGpuPassTimingSample(const GpuPassTimingFrame& timings);

    void ensureSceneTextureFallback();

    [[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeTransferToGraphicsImageUploadPlan() const;

    void uploadSceneTextureFallback();

    [[nodiscard]] RendererSceneTextureDescriptorTable buildSceneTextureDescriptorTable(
        const NodeFrameParameters& frameParameters,
        const std::map<std::uint32_t, nr::resource::TextureHandle>& sceneTextureHandlesById);

    std::unique_ptr<nr::rhi::Device> device_{};
    RenderGraphBuilder builder_{};
    RenderGraphExecutor executor_{};
    RendererCacheSuite cacheSuite_{};
    RendererSubmissionTimeline submissionTimeline_{};
    FrameUniformArena frameUniformArena_{};
    nr::rhi::Image sceneTextureFallback_{};
    nr::rhi::SlangSampler sceneTextureSampler_{};

    bool graphInstalled_ = false;
    std::vector<InstalledNode> installedNodes_{};
    std::multimap<std::size_t, SubmitNodeSpec> submitNodesByAfterIndex_{};

    std::optional<std::reference_wrapper<nr::scene::Scene>> activeScene_{};
    std::optional<nr::scene::SceneExtractProfileHandle> sceneExtractProfile_{};
    std::optional<nr::scene::SceneExtractProfileHandle> sceneTlasExtractProfile_{};
    RendererCpuFrameTimings cpuTimingAccumulator_{};
    RendererCpuStatistics cpuStatistics_{};
    std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage> gpuPassTimingAccumulator_{};
    RendererGpuPassStatistics gpuPassStatistics_{};
};
} // namespace nr::renderer
