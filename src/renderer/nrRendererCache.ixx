export module nr.renderer:rendererCache;
import dependency.vulkan;

import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :renderGraphCompiler;
import :renderGraphType;

export namespace nr::renderer
{
struct FrameGlobalResources;

struct SceneTextureDescriptorBinding
{
    std::uint32_t descriptorIndex = nr::scene::kDefaultSceneTextureId;
    std::reference_wrapper<const nr::rhi::Image> image;
    vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    std::uint64_t gpuVersion = 0;
};

struct RenderGraphCompileCacheStatistics
{
    std::uint64_t hitCount = 0;
    std::uint64_t missCount = 0;
    std::size_t entryCount = 0;
};

enum class RenderGraphSkeletonMissReason : std::uint8_t
{
    None,
    Disabled,
    UnsupportedNode,
    KeyNotFound,
    StructureMismatch,
    PatchFailed,
    Invalidated,
};

struct RenderGraphSkeletonNodeKey
{
    std::uint64_t configurationRevision = 0;
    std::uint64_t runtimeConfigurationRevision = 0;
    std::string structuralBranchKey{};

    [[nodiscard]] bool operator==(const RenderGraphSkeletonNodeKey &) const = default;
};

struct RenderGraphSkeletonKey
{
    std::uint64_t installedGraphGeneration = 0;
    vk::Extent2D displayExtent{1u, 1u};
    vk::Extent2D renderExtent{1u, 1u};
    vk::Extent2D swapchainExtent{1u, 1u};
    vk::Format swapchainFormat = vk::Format::eUndefined;
    vk::ColorSpaceKHR swapchainColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    std::uint64_t shaderSessionGeneration = 0;
    std::uint64_t swapchainRecreationGeneration = 0;
    std::uint64_t submitAcquirePolicyRevision = 0;
    bool hasSceneBridgeFrame = false;
    std::vector<RenderGraphSkeletonNodeKey> nodes{};

    [[nodiscard]] bool operator==(const RenderGraphSkeletonKey &) const = default;
};

struct RenderGraphSkeletonCacheStatistics
{
    std::uint64_t hitCount = 0;
    std::uint64_t missCount = 0;
    std::uint64_t invalidationCount = 0;
    std::uint64_t structureMismatchCount = 0;
    std::size_t entryCount = 0;
    RenderGraphSkeletonMissReason lastMissReason = RenderGraphSkeletonMissReason::None;
};

struct RenderGraphSkeletonNodePatchLayout
{
    QueueDomain queue = QueueDomain::Graphics;
    std::size_t resourceBegin = 0;
    std::size_t resourceCount = 0;
    std::size_t frameDataBegin = 0;
    std::size_t frameDataCount = 0;
    std::size_t passBegin = 0;
    std::size_t passCount = 0;
};

struct RenderGraphSkeletonTemplate
{
    RenderGraphFrameDescription staticFrame{};
    RenderGraphSkeletonNodePatchLayout globalPatchLayout{};
    std::vector<RenderGraphSkeletonNodePatchLayout> nodePatchLayouts{};
    std::map<std::string, GraphResourceHandle> namedFrameResources{};
    std::map<std::string, GraphFrameDataHandle> namedFrameData{};
};

struct RenderGraphSkeletonCapture
{
    RenderGraphSkeletonNodePatchLayout globalPatchLayout{};
    std::vector<RenderGraphSkeletonNodePatchLayout> nodePatchLayouts{};
    std::map<std::string, GraphResourceHandle> namedFrameResources{};
    std::map<std::string, GraphFrameDataHandle> namedFrameData{};
};

struct RenderGraphSkeletonImageResourceDesc
{
    std::string debugName{};
    vk::Extent3D extent{1u, 1u, 1u};
    vk::Format format = vk::Format::eUndefined;
    ImageAspectIntent aspect = ImageAspectIntent::Color;
};

class RenderGraphSkeletonPatchContext
{
  public:
    RenderGraphSkeletonPatchContext(RenderGraphFrameDescription &frame, RenderGraphSkeletonNodePatchLayout layout,
                                    const std::map<std::string, GraphResourceHandle> &namedFrameResources,
                                    const std::map<std::string, GraphFrameDataHandle> &namedFrameData,
                                    const FrameGlobalResources *globalResources = nullptr,
                                    std::string_view runtimeName = {}) noexcept;

    void patchResource(std::size_t localSlot, GraphResourceDescVariant desc);

    void patchFrameData(std::size_t localSlot, std::string_view debugName, std::any payload);

    void patchPass(std::size_t localSlot, std::string_view debugName, PassPrepareCallback prepare,
                   PassRecordCallback record, std::optional<PassParallelRecordDesc> parallelRecord = std::nullopt,
                   std::span<const GraphFrameDataHandle> frameDataUses = {});

    void patchCopy(std::size_t localSlot, std::string_view debugName, CopyPassDesc copy);

    [[nodiscard]] GraphResourceHandle namedResource(std::string_view name) const;

    [[nodiscard]] GraphFrameDataHandle namedFrameData(std::string_view name) const;

    [[nodiscard]] std::optional<std::reference_wrapper<const std::any>> resolveFrameDataPayload(
        GraphFrameDataHandle handle) const;

    template <typename TPayload>
    [[nodiscard]] std::optional<std::reference_wrapper<const std::remove_cvref_t<TPayload>>> resolveFrameData(
        GraphFrameDataHandle handle) const
    {
        using Payload = std::remove_cvref_t<TPayload>;
        nrAssert(handle.valid(), "RenderGraphSkeletonPatchContext::resolveFrameData requires a valid handle.");
        auto payload = resolveFrameDataPayload(handle);
        if (!payload.has_value())
        {
            return {};
        }
        auto const typedPayload = std::any_cast<Payload>(&payload->get());
        nrAssert(
            typedPayload != nullptr,
            std::format(
                "RenderGraphSkeletonPatchContext::resolveFrameData resolved unexpected payload type for handle {}.",
                handle.value));
        return std::cref(*typedPayload);
    }

    [[nodiscard]] const FrameGlobalResources &globalResources() const noexcept;

    [[nodiscard]] GraphResourceHandle resource(std::size_t localSlot) const;

    [[nodiscard]] QueueDomain queue() const noexcept;

    [[nodiscard]] std::string_view runtimeName() const noexcept;

    [[nodiscard]] GraphFrameDataHandle frameData(std::size_t localSlot) const;

    [[nodiscard]] GraphPassHandle passHandle(std::size_t localSlot) const;

    [[nodiscard]] GraphResourceHandle passResource(std::size_t localPassSlot, std::size_t useSlot) const;

    [[nodiscard]] bool hasNamedResource(std::string_view name) const;

    [[nodiscard]] bool hasNamedFrameData(std::string_view name) const;

    [[nodiscard]] std::optional<RenderGraphSkeletonImageResourceDesc> describeImageResource(
        GraphResourceHandle resource) const;

  private:
    std::reference_wrapper<RenderGraphFrameDescription> frame_;
    RenderGraphSkeletonNodePatchLayout layout_{};
    std::reference_wrapper<const std::map<std::string, GraphResourceHandle>> namedFrameResources_;
    std::reference_wrapper<const std::map<std::string, GraphFrameDataHandle>> namedFrameData_;
    const FrameGlobalResources *globalResources_ = nullptr;
    std::string_view runtimeName_{};
};

class RenderGraphCompileCache
{
  public:
    struct FrameSignature;

    [[nodiscard]] CompiledGraphFrame compileConsumingCached(RenderGraphFrameDescription &frame);

    void clear() noexcept;

    [[nodiscard]] RenderGraphCompileCacheStatistics statistics() const noexcept;

    [[nodiscard]] static FrameSignature structuralSignature(const RenderGraphFrameDescription &frame);

    using ResourceUseSignature = PassResourceUseDesc;

    struct ImportedBufferResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
        ResourceResidency residency = ResourceResidency::Imported;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
        vk::DeviceSize size = 0;
        std::vector<BufferUsageIntent> usageIntents{};
        bool retainedInitialized = false;
        ResourceOwnershipDomain retainedOwnership = ResourceOwnershipDomain::Undefined;
        AccessScope retainedAccess{};

        [[nodiscard]] bool operator==(const ImportedBufferResourceSignature &) const = default;
    };

    struct ImportedImageResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
        ResourceResidency residency = ResourceResidency::Imported;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
        vk::Extent3D extent{1, 1, 1};
        vk::Format format = vk::Format::eUndefined;
        std::vector<ImageUsageIntent> usageIntents{};
        ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
        AccessScope initialAccessScope{};
        ImageAspectIntent aspect = ImageAspectIntent::Color;
        bool retainedInitialized = false;
        ResourceOwnershipDomain retainedOwnership = ResourceOwnershipDomain::Undefined;
        AccessScope retainedAccess{};
        ImageLayoutIntent retainedLayout = ImageLayoutIntent::Undefined;

        [[nodiscard]] bool operator==(const ImportedImageResourceSignature &) const = default;
    };

    struct ImportedAccelerationStructureResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
        ResourceResidency residency = ResourceResidency::Imported;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
        vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eTopLevel;
        vk::DeviceSize size = 0;
        std::vector<AccelerationStructureUsageIntent> usageIntents{};
        bool retainedInitialized = false;
        ResourceOwnershipDomain retainedOwnership = ResourceOwnershipDomain::Undefined;
        AccessScope retainedAccess{};

        [[nodiscard]] bool operator==(const ImportedAccelerationStructureResourceSignature &) const = default;
    };

    struct ImportedSwapchainImageResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::SwapchainRelative;
        ResourceResidency residency = ResourceResidency::Swapchain;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Compute;
        vk::Extent3D extent{1, 1, 1};
        vk::Format format = vk::Format::eUndefined;

        [[nodiscard]] bool operator==(const ImportedSwapchainImageResourceSignature &) const = default;
    };

    struct TransientBufferResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
        vk::DeviceSize size = 0;
        std::vector<BufferUsageIntent> usageIntents{};
        nr::rhi::MemoryUsage memoryUsage = nr::rhi::MemoryUsage::GpuOnly;

        [[nodiscard]] bool operator==(const TransientBufferResourceSignature &) const = default;
    };

    struct TransientImageResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
        vk::Extent3D extent{1, 1, 1};
        vk::Format format = vk::Format::eUndefined;
        std::vector<ImageUsageIntent> usageIntents{};
        ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
        ImageAspectIntent aspect = ImageAspectIntent::Color;

        [[nodiscard]] bool operator==(const TransientImageResourceSignature &) const = default;
    };

    using ResourceDescSignature =
        std::variant<ImportedBufferResourceSignature, ImportedImageResourceSignature,
                     ImportedAccelerationStructureResourceSignature, ImportedSwapchainImageResourceSignature,
                     TransientBufferResourceSignature, TransientImageResourceSignature>;

    struct ResourceSignature
    {
        GraphResourceHandle handle{};
        ResourceDescSignature desc{};

        [[nodiscard]] bool operator==(const ResourceSignature &) const = default;
    };

    struct NodeSignature
    {
        GraphNodeHandle handle{};
        QueueDomain queue = QueueDomain::Graphics;

        [[nodiscard]] bool operator==(const NodeSignature &) const = default;
    };

    struct PassSignature
    {
        GraphPassHandle handle{};
        GraphNodeHandle node{};
        bool isCopyPass = false;
        QueueDomain queue = QueueDomain::Graphics;
        vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{};
        std::optional<CopyPassDesc> copy{};
        std::vector<ResourceUseSignature> resourceUses{};
        std::vector<GraphFrameDataHandle> frameDataUses{};
        bool hasPrepare = false;
        bool hasRecord = false;
        bool hasParallelRecord = false;
        ParallelRecordReplaySemantics parallelReplaySemantics = ParallelRecordReplaySemantics::Unordered;

        [[nodiscard]] bool operator==(const PassSignature &) const = default;
    };

    struct SubmitBoundarySignature
    {
        GraphSubmitHandle handle{};
        SubmitBoundaryKind kind = SubmitBoundaryKind::QueueSubmission;

        [[nodiscard]] bool operator==(const SubmitBoundarySignature &) const = default;
    };

    struct ExecutionStepSignature
    {
        GraphExecutionStep step{};

        [[nodiscard]] bool operator==(const ExecutionStepSignature &) const = default;
    };

    struct FrameSignature
    {
        std::vector<ResourceSignature> resources{};
        std::vector<NodeSignature> nodes{};
        std::vector<PassSignature> passes{};
        std::vector<SubmitBoundarySignature> submitBoundaries{};
        std::vector<ExecutionStepSignature> executionOrder{};

        [[nodiscard]] bool operator==(const FrameSignature &) const = default;
    };

    struct CacheEntry
    {
        FrameSignature signature{};
        CompiledGraphFrame compiledTemplate{};
    };

  private:
    [[nodiscard]] static FrameSignature makeSignature(const RenderGraphFrameDescription &frame);

    [[nodiscard]] static CompiledGraphFrame makeCompiledTemplate(const CompiledGraphFrame &compiled);

    [[nodiscard]] static CompiledGraphFrame materializeCachedFrame(const CompiledGraphFrame &compiledTemplate,
                                                                   RenderGraphFrameDescription &frame);

    static void patchCompiledResources(std::vector<CompiledResourceDesc> &compiledResources,
                                       RenderGraphFrameDescription &frame);

    static void patchCompiledPasses(std::vector<CompiledSubmitBatch> &submitBatches,
                                    RenderGraphFrameDescription &frame);

    static void patchCompiledSubmitDebugNames(std::vector<CompiledSubmitBatch> &submitBatches,
                                              const RenderGraphFrameDescription &frame);

    static constexpr std::size_t kMaxEntries = 8;

    RenderGraphCompiler compiler_{};
    std::vector<CacheEntry> entries_{};
    std::uint64_t hitCount_ = 0;
    std::uint64_t missCount_ = 0;
};

class RenderGraphSkeletonCache
{
  public:
    struct ProbeResult
    {
        bool keyHit = false;
        bool structureMatches = false;
        RenderGraphSkeletonMissReason missReason = RenderGraphSkeletonMissReason::None;
    };

    [[nodiscard]] bool contains(const RenderGraphSkeletonKey &key) const;

    [[nodiscard]] std::shared_ptr<const RenderGraphSkeletonTemplate> lookup(const RenderGraphSkeletonKey &key) const;

    [[nodiscard]] static RenderGraphFrameDescription instantiate(const RenderGraphSkeletonTemplate &skeleton);

    [[nodiscard]] ProbeResult acceptMaterialized(RenderGraphSkeletonKey key, const RenderGraphFrameDescription &frame,
                                                 RenderGraphSkeletonCapture capture = {});

    void recordHit() noexcept;

    void refreshMaterialized(RenderGraphSkeletonKey key, const RenderGraphFrameDescription &frame,
                             RenderGraphSkeletonCapture capture);

    void recordMiss(RenderGraphSkeletonMissReason reason) noexcept;

    void clear(RenderGraphSkeletonMissReason reason = RenderGraphSkeletonMissReason::Invalidated) noexcept;

    [[nodiscard]] RenderGraphSkeletonCacheStatistics statistics() const noexcept;

  private:
    struct Entry
    {
        RenderGraphSkeletonKey key{};
        RenderGraphCompileCache::FrameSignature structure{};
        std::shared_ptr<const RenderGraphSkeletonTemplate> skeleton{};
    };

    [[nodiscard]] static RenderGraphSkeletonTemplate makeTemplate(const RenderGraphFrameDescription &frame,
                                                                  RenderGraphSkeletonCapture capture);

    static constexpr std::size_t kMaxEntries = 8;
    std::vector<Entry> entries_{};
    RenderGraphSkeletonCacheStatistics statistics_{};
};

enum class BindlessImageTableRequirement
{
    required,
    optional,
};

struct PipelinePassBindingCacheKey
{
    std::uint64_t runtimeIdentity = 0;
    std::uint64_t generation = 0;
    std::size_t stateIndex = std::numeric_limits<std::size_t>::max();
    std::size_t frameSlot = 0;

    [[nodiscard]] auto operator<=>(const PipelinePassBindingCacheKey &) const = default;
};

struct BindlessImageDescriptor
{
    std::optional<std::reference_wrapper<const nr::rhi::Image>> image{};
    vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    std::uint64_t logicalResourceId = 0;
    std::string debugName{};
};

struct BindlessImageTableRequest
{
    std::string tableKey{};
    std::string shaderSymbol{};
    std::uint32_t expectedSet = 0;
    std::uint32_t expectedBinding = 0;
    vk::DescriptorType expectedDescriptorType = vk::DescriptorType::eCombinedImageSampler;
    std::uint32_t descriptorCapacity = 0;
    vk::Sampler sampler{};
    bool usesImmutableSampler = false;
    std::uint64_t tableVersion = 0;
    bool refreshActiveDescriptorsOnCacheHit = false;
    std::map<std::uint32_t, BindlessImageDescriptor> descriptorsById{};
    std::optional<BindlessImageDescriptor> fallbackDescriptor{};
    BindlessImageTableRequirement requirement = BindlessImageTableRequirement::required;
};

[[nodiscard]] inline bool bindlessImageTableHasDescriptors(const BindlessImageTableRequest &request)
{
    return request.fallbackDescriptor.has_value() || !request.descriptorsById.empty();
}

[[nodiscard]] inline bool bindlessImageTableNeedsDynamicSampler(const BindlessImageTableRequest &request)
{
    return bindlessImageTableHasDescriptors(request) &&
           nr::rhi::supportsImmutableSampler(request.expectedDescriptorType) && !request.usesImmutableSampler;
}

class BindlessImageTableCache
{
  public:
    void clear() noexcept;

    template <typename PipelineRuntimeT>
    void ensureTableForFrame(PipelineRuntimeT &pipeline, typename PipelineRuntimeT::PassBindingHandle passBinding,
                             std::uint32_t frameIndex,
                             const BindlessImageTableRequest &request)
    {
        nrAssert(!request.tableKey.empty(), "BindlessImageTableCache requires a non-empty table key.");
        nrAssert(!request.shaderSymbol.empty(), "BindlessImageTableCache requires a shader symbol.");
        nrAssert(request.descriptorCapacity > 0u, "BindlessImageTableCache requires descriptorCapacity > 0.");

        auto const ownerKey = pipeline.passBindingCacheKey(passBinding, frameIndex);
        auto root = pipeline.rootCursor();
        if (!root.hasField(request.shaderSymbol))
        {
            nrAssert(request.requirement == BindlessImageTableRequirement::optional,
                     std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey,
                                 request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(passBinding, frameIndex, {});
            if (reallocated)
            {
                invalidateTablesForFrame(ownerKey);
            }
            return;
        }

        auto tableCursor = root[request.shaderSymbol];
        if (!tableCursor.valid())
        {
            nrAssert(request.requirement == BindlessImageTableRequirement::optional,
                     std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey,
                                 request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(passBinding, frameIndex, {});
            if (reallocated)
            {
                invalidateTablesForFrame(ownerKey);
            }
            return;
        }

        auto tableBinding = tableCursor.descriptorBinding();
        nrAssert(tableBinding.has_value() && tableBinding->supportsVariableDescriptorCount() &&
                     tableBinding->set == request.expectedSet && tableBinding->binding == request.expectedBinding &&
                     tableBinding->descriptorType == request.expectedDescriptorType,
                 std::format("Bindless image table '{}' has an unexpected descriptor binding.", request.tableKey));
        nrAssert(!request.usesImmutableSampler || nr::rhi::supportsImmutableSampler(request.expectedDescriptorType),
                 "BindlessImageTableCache immutable sampler mode requires a sampler-capable descriptor type.");
        nrAssert(!bindlessImageTableNeedsDynamicSampler(request) || request.sampler != vk::Sampler{},
                 "BindlessImageTableCache requires a valid sampler for dynamic sampler image descriptors.");

        auto const reallocated =
            pipeline.ensureBindingSetsForFrame(passBinding, frameIndex,
                                               {{tableBinding->set, request.descriptorCapacity}});
        if (reallocated)
        {
            invalidateTablesForFrame(ownerKey);
        }
    }

    template <typename PipelineRuntimeT>
    [[nodiscard]] nr::rhi::ShaderBindingSnapshot makeSnapshotForFrame(PipelineRuntimeT &pipeline,
                                                                      typename PipelineRuntimeT::PassBindingHandle passBinding,
                                                                      std::uint32_t frameIndex,
                                                                      const BindlessImageTableRequest &request)
    {
        nrAssert(!request.tableKey.empty(), "BindlessImageTableCache requires a non-empty table key.");
        nrAssert(!request.shaderSymbol.empty(), "BindlessImageTableCache requires a shader symbol.");
        nrAssert(request.descriptorCapacity > 0u, "BindlessImageTableCache requires descriptorCapacity > 0.");

        auto const ownerKey = pipeline.passBindingCacheKey(passBinding, frameIndex);
        auto root = pipeline.rootCursor();
        if (!root.hasField(request.shaderSymbol))
        {
            nrAssert(request.requirement == BindlessImageTableRequirement::optional,
                     std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey,
                                 request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(passBinding, frameIndex, {});
            if (reallocated)
            {
                invalidateTablesForFrame(ownerKey);
            }
            return {};
        }

        auto tableCursor = root[request.shaderSymbol];
        if (!tableCursor.valid())
        {
            nrAssert(request.requirement == BindlessImageTableRequirement::optional,
                     std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey,
                                 request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(passBinding, frameIndex, {});
            if (reallocated)
            {
                invalidateTablesForFrame(ownerKey);
            }
            return {};
        }

        auto tableBinding = tableCursor.descriptorBinding();
        nrAssert(tableBinding.has_value() && tableBinding->supportsVariableDescriptorCount() &&
                     tableBinding->set == request.expectedSet && tableBinding->binding == request.expectedBinding &&
                     tableBinding->descriptorType == request.expectedDescriptorType,
                 std::format("Bindless image table '{}' has an unexpected descriptor binding.", request.tableKey));
        nrAssert(!request.usesImmutableSampler || nr::rhi::supportsImmutableSampler(request.expectedDescriptorType),
                 "BindlessImageTableCache immutable sampler mode requires a sampler-capable descriptor type.");
        nrAssert(!bindlessImageTableNeedsDynamicSampler(request) || request.sampler != vk::Sampler{},
                 "BindlessImageTableCache requires a valid sampler for dynamic sampler image descriptors.");

        auto const reallocated =
            pipeline.ensureBindingSetsForFrame(passBinding, frameIndex,
                                               {{tableBinding->set, request.descriptorCapacity}});
        if (reallocated)
        {
            invalidateTablesForFrame(ownerKey);
        }

        return makeSnapshotForFrameCore(ownerKey, tableCursor, root, request);
    }

  private:
    struct TableKey
    {
        PipelinePassBindingCacheKey ownerKey{};
        std::string tableKey{};

        [[nodiscard]] auto operator<=>(const TableKey &) const = default;
    };

    struct TableState
    {
        bool initialized = false;
        std::uint64_t version = 0;
        std::set<std::uint32_t> descriptorIds{};
    };

    void invalidateTablesForFrame(PipelinePassBindingCacheKey ownerKey) noexcept;

    [[nodiscard]] nr::rhi::ShaderBindingSnapshot makeSnapshotForFrameCore(PipelinePassBindingCacheKey ownerKey,
                                                                          const nr::rhi::ShaderCursor &tableCursor,
                                                                          const nr::rhi::ShaderCursor &root,
                                                                          const BindlessImageTableRequest &request);

    std::map<TableKey, TableState> tables_{};
};

struct RendererSceneTextureDescriptorTableInput
{
    std::reference_wrapper<const nr::rhi::Image> fallbackImage;
    std::optional<std::reference_wrapper<const nr::scene::Scene>> scene{};
    std::map<std::uint32_t, nr::resource::TextureHandle> sceneTextureHandlesById{};
};

struct RendererSceneTextureDescriptorTable
{
    std::map<std::uint32_t, SceneTextureDescriptorBinding> descriptorsById{};
    std::uint64_t version = 0;
};

class RendererGlobalDescriptorTableCache
{
  public:
    [[nodiscard]] RendererSceneTextureDescriptorTable buildSceneTextureDescriptorTable(
        const RendererSceneTextureDescriptorTableInput &input);

    void clear() noexcept;

  private:
    struct SceneTextureDescriptorKey
    {
        nr::resource::TextureHandle texture{};
        std::uint64_t gpuVersion = 0;

        [[nodiscard]] friend bool operator==(const SceneTextureDescriptorKey &,
                                             const SceneTextureDescriptorKey &) noexcept = default;
    };

    std::map<std::uint32_t, SceneTextureDescriptorKey> sceneTextureDescriptorKeys_{};
    std::uint64_t sceneTextureDescriptorVersion_ = 1;
};

struct RendererCacheSuite
{
    RenderGraphSkeletonCache skeletonCache{};
    RenderGraphCompileCache compileCache{};
    BindlessImageTableCache bindlessImageTableCache{};
    RendererGlobalDescriptorTableCache globalDescriptorTableCache{};

    void clear() noexcept;
};
} // namespace nr::renderer
