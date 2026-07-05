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

class RenderGraphCompileCache
{
  public:
    [[nodiscard]] CompiledGraphFrame compileConsumingCached(RenderGraphFrameDescription& frame);

    void clear() noexcept;

    [[nodiscard]] RenderGraphCompileCacheStatistics statistics() const noexcept;

    struct ResourceUseSignature
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
        ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
        bool readOnly = false;
        bool requiresPreviousUseBarrier = false;

        [[nodiscard]] bool operator==(const ResourceUseSignature&) const = default;
    };

    struct ImportedBufferResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
        ResourceResidency residency = ResourceResidency::Imported;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
        vk::DeviceSize size = 0;
        std::vector<BufferUsageIntent> usageIntents{};

        [[nodiscard]] bool operator==(const ImportedBufferResourceSignature&) const = default;
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

        [[nodiscard]] bool operator==(const ImportedImageResourceSignature&) const = default;
    };

    struct ImportedAccelerationStructureResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
        ResourceResidency residency = ResourceResidency::Imported;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
        vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eTopLevel;
        vk::DeviceSize size = 0;
        std::vector<AccelerationStructureUsageIntent> usageIntents{};

        [[nodiscard]] bool operator==(const ImportedAccelerationStructureResourceSignature&) const = default;
    };

    struct ImportedSwapchainImageResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::SwapchainRelative;
        ResourceResidency residency = ResourceResidency::Swapchain;
        ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Compute;
        vk::Extent3D extent{1, 1, 1};
        vk::Format format = vk::Format::eUndefined;

        [[nodiscard]] bool operator==(const ImportedSwapchainImageResourceSignature&) const = default;
    };

    struct TransientBufferResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
        vk::DeviceSize size = 0;
        std::vector<BufferUsageIntent> usageIntents{};
        nr::rhi::MemoryUsage memoryUsage = nr::rhi::MemoryUsage::GpuOnly;

        [[nodiscard]] bool operator==(const TransientBufferResourceSignature&) const = default;
    };

    struct TransientImageResourceSignature
    {
        ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
        vk::Extent3D extent{1, 1, 1};
        vk::Format format = vk::Format::eUndefined;
        std::vector<ImageUsageIntent> usageIntents{};
        ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
        ImageAspectIntent aspect = ImageAspectIntent::Color;

        [[nodiscard]] bool operator==(const TransientImageResourceSignature&) const = default;
    };

    using ResourceDescSignature = std::variant<
        ImportedBufferResourceSignature,
        ImportedImageResourceSignature,
        ImportedAccelerationStructureResourceSignature,
        ImportedSwapchainImageResourceSignature,
        TransientBufferResourceSignature,
        TransientImageResourceSignature>;

    struct ResourceSignature
    {
        GraphResourceHandle handle{};
        ResourceDescSignature desc{};

        [[nodiscard]] bool operator==(const ResourceSignature&) const = default;
    };

    struct NodeSignature
    {
        GraphNodeHandle handle{};
        QueueDomain queue = QueueDomain::Graphics;

        [[nodiscard]] bool operator==(const NodeSignature&) const = default;
    };

    struct PassSignature
    {
        GraphPassHandle handle{};
        GraphNodeHandle node{};
        bool isCopyPass = false;
        QueueDomain queue = QueueDomain::Graphics;
        std::vector<ResourceUseSignature> resourceUses{};
        bool hasPrepare = false;
        bool hasRecord = false;
        bool hasParallelRecord = false;
        ParallelRecordReplaySemantics parallelReplaySemantics = ParallelRecordReplaySemantics::Unordered;

        [[nodiscard]] bool operator==(const PassSignature&) const = default;
    };

    struct SubmitBoundarySignature
    {
        GraphSubmitHandle handle{};

        [[nodiscard]] bool operator==(const SubmitBoundarySignature&) const = default;
    };

    struct ExecutionStepSignature
    {
        GraphExecutionStep step{};

        [[nodiscard]] bool operator==(const ExecutionStepSignature&) const = default;
    };

    struct FrameSignature
    {
        std::vector<ResourceSignature> resources{};
        std::vector<NodeSignature> nodes{};
        std::vector<PassSignature> passes{};
        std::vector<SubmitBoundarySignature> submitBoundaries{};
        std::vector<ExecutionStepSignature> executionOrder{};

        [[nodiscard]] bool operator==(const FrameSignature&) const = default;
    };

    struct CacheEntry
    {
        FrameSignature signature{};
        CompiledGraphFrame compiledTemplate{};
    };

  private:
    [[nodiscard]] static FrameSignature makeSignature(const RenderGraphFrameDescription& frame);

    [[nodiscard]] static CompiledGraphFrame makeCompiledTemplate(const CompiledGraphFrame& compiled);

    [[nodiscard]] static CompiledGraphFrame materializeCachedFrame(
        const CompiledGraphFrame& compiledTemplate,
        RenderGraphFrameDescription& frame);

    static void patchCompiledResources(
        std::vector<CompiledResourceDesc>& compiledResources,
        RenderGraphFrameDescription& frame);

    static void patchCompiledPasses(
        std::vector<CompiledSubmitBatch>& submitBatches,
        RenderGraphFrameDescription& frame);

    static void patchCompiledSubmitDebugNames(
        std::vector<CompiledSubmitBatch>& submitBatches,
        const RenderGraphFrameDescription& frame);

    static constexpr std::size_t kMaxEntries = 8;

    RenderGraphCompiler compiler_{};
    std::vector<CacheEntry> entries_{};
    std::uint64_t hitCount_ = 0;
    std::uint64_t missCount_ = 0;
};

enum class BindlessImageTableRequirement
{
    required,
    optional,
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
    std::map<std::uint32_t, BindlessImageDescriptor> descriptorsById{};
    std::optional<BindlessImageDescriptor> fallbackDescriptor{};
    BindlessImageTableRequirement requirement = BindlessImageTableRequirement::required;
};

[[nodiscard]] inline bool bindlessImageTableHasDescriptors(const BindlessImageTableRequest& request)
{
    return request.fallbackDescriptor.has_value() || !request.descriptorsById.empty();
}

[[nodiscard]] inline bool bindlessImageTableNeedsDynamicSampler(const BindlessImageTableRequest& request)
{
    return bindlessImageTableHasDescriptors(request) &&
           nr::rhi::supportsImmutableSampler(request.expectedDescriptorType) &&
           !request.usesImmutableSampler;
}

class BindlessImageTableCache
{
  public:
    void clear() noexcept;

    void invalidateTableForFrame(
        std::uintptr_t ownerKey,
        std::string_view tableKey,
        std::uint32_t frameIndex) noexcept;

    template <typename PipelineRuntimeT>
    void ensureTableForFrame(
        PipelineRuntimeT& pipeline,
        std::uint32_t frameIndex,
        const BindlessImageTableRequest& request)
    {
        nrAssert(!request.tableKey.empty(), "BindlessImageTableCache requires a non-empty table key.");
        nrAssert(!request.shaderSymbol.empty(), "BindlessImageTableCache requires a shader symbol.");
        nrAssert(request.descriptorCapacity > 0u, "BindlessImageTableCache requires descriptorCapacity > 0.");

        auto ownerKey = reinterpret_cast<std::uintptr_t>(std::addressof(pipeline));
        auto root = pipeline.rootCursor();
        if (!root.hasField(request.shaderSymbol))
        {
            nrAssert(
                request.requirement == BindlessImageTableRequirement::optional,
                std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey, request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(frameIndex, {});
            if (reallocated)
            {
                invalidateTableForFrame(ownerKey, request.tableKey, frameIndex);
            }
            return;
        }

        auto tableCursor = root[request.shaderSymbol];
        if (!tableCursor.valid())
        {
            nrAssert(
                request.requirement == BindlessImageTableRequirement::optional,
                std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey, request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(frameIndex, {});
            if (reallocated)
            {
                invalidateTableForFrame(ownerKey, request.tableKey, frameIndex);
            }
            return;
        }

        auto tableBinding = tableCursor.descriptorBinding();
        nrAssert(
            tableBinding.has_value() &&
                tableBinding->supportsVariableDescriptorCount() &&
                tableBinding->set == request.expectedSet &&
                tableBinding->binding == request.expectedBinding &&
                tableBinding->descriptorType == request.expectedDescriptorType,
            std::format("Bindless image table '{}' has an unexpected descriptor binding.", request.tableKey));
        nrAssert(
            !request.usesImmutableSampler || nr::rhi::supportsImmutableSampler(request.expectedDescriptorType),
            "BindlessImageTableCache immutable sampler mode requires a sampler-capable descriptor type.");
        nrAssert(
            !bindlessImageTableNeedsDynamicSampler(request) || request.sampler != vk::Sampler{},
            "BindlessImageTableCache requires a valid sampler for dynamic sampler image descriptors.");

        auto const reallocated = pipeline.ensureBindingSetsForFrame(
            frameIndex,
            {{tableBinding->set, request.descriptorCapacity}});
        if (reallocated)
        {
            invalidateTableForFrame(ownerKey, request.tableKey, frameIndex);
        }
    }

    template <typename PipelineRuntimeT>
    [[nodiscard]] nr::rhi::ShaderBindingSnapshot makeSnapshotForFrame(
        PipelineRuntimeT& pipeline,
        std::uint32_t frameIndex,
        const BindlessImageTableRequest& request)
    {
        nrAssert(!request.tableKey.empty(), "BindlessImageTableCache requires a non-empty table key.");
        nrAssert(!request.shaderSymbol.empty(), "BindlessImageTableCache requires a shader symbol.");
        nrAssert(request.descriptorCapacity > 0u, "BindlessImageTableCache requires descriptorCapacity > 0.");

        auto ownerKey = reinterpret_cast<std::uintptr_t>(std::addressof(pipeline));
        auto root = pipeline.rootCursor();
        if (!root.hasField(request.shaderSymbol))
        {
            nrAssert(
                request.requirement == BindlessImageTableRequirement::optional,
                std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey, request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(frameIndex, {});
            if (reallocated)
            {
                invalidateTableForFrame(ownerKey, request.tableKey, frameIndex);
            }
            return {};
        }

        auto tableCursor = root[request.shaderSymbol];
        if (!tableCursor.valid())
        {
            nrAssert(
                request.requirement == BindlessImageTableRequirement::optional,
                std::format("Bindless image table '{}' requires shader symbol '{}'.", request.tableKey, request.shaderSymbol));
            auto const reallocated = pipeline.ensureBindingSetsForFrame(frameIndex, {});
            if (reallocated)
            {
                invalidateTableForFrame(ownerKey, request.tableKey, frameIndex);
            }
            return {};
        }

        auto tableBinding = tableCursor.descriptorBinding();
        nrAssert(
            tableBinding.has_value() &&
                tableBinding->supportsVariableDescriptorCount() &&
                tableBinding->set == request.expectedSet &&
                tableBinding->binding == request.expectedBinding &&
                tableBinding->descriptorType == request.expectedDescriptorType,
            std::format("Bindless image table '{}' has an unexpected descriptor binding.", request.tableKey));
        nrAssert(
            !request.usesImmutableSampler || nr::rhi::supportsImmutableSampler(request.expectedDescriptorType),
            "BindlessImageTableCache immutable sampler mode requires a sampler-capable descriptor type.");
        nrAssert(
            !bindlessImageTableNeedsDynamicSampler(request) || request.sampler != vk::Sampler{},
            "BindlessImageTableCache requires a valid sampler for dynamic sampler image descriptors.");

        auto const reallocated = pipeline.ensureBindingSetsForFrame(
            frameIndex,
            {{tableBinding->set, request.descriptorCapacity}});
        if (reallocated)
        {
            invalidateTableForFrame(ownerKey, request.tableKey, frameIndex);
        }

        return makeSnapshotForFrameCore(
            ownerKey,
            tableCursor,
            root,
            frameIndex,
            request);
    }

  private:
    struct TableKey
    {
        std::uintptr_t ownerKey = 0;
        std::string tableKey{};

        [[nodiscard]] auto operator<=>(const TableKey&) const = default;
    };

    struct TableState
    {
        std::array<bool, nr::maxFrameInFlight> initialized{};
        std::array<std::uint64_t, nr::maxFrameInFlight> versions{};
        std::array<std::set<std::uint32_t>, nr::maxFrameInFlight> descriptorIdsByFrame{};
    };

    [[nodiscard]] nr::rhi::ShaderBindingSnapshot makeSnapshotForFrameCore(
        std::uintptr_t ownerKey,
        const nr::rhi::ShaderCursor& tableCursor,
        const nr::rhi::ShaderCursor& root,
        std::uint32_t frameIndex,
        const BindlessImageTableRequest& request);

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
        const RendererSceneTextureDescriptorTableInput& input);

    void clear() noexcept;

  private:
    struct SceneTextureDescriptorKey
    {
        nr::resource::TextureHandle texture{};
        std::uint64_t gpuVersion = 0;

        [[nodiscard]] friend bool operator==(const SceneTextureDescriptorKey&, const SceneTextureDescriptorKey&) noexcept = default;
    };

    std::map<std::uint32_t, SceneTextureDescriptorKey> sceneTextureDescriptorKeys_{};
    std::uint64_t sceneTextureDescriptorVersion_ = 1;
};

struct RendererCacheSuite
{
    RenderGraphCompileCache compileCache{};
    BindlessImageTableCache bindlessImageTableCache{};
    RendererGlobalDescriptorTableCache globalDescriptorTableCache{};

    void clear() noexcept;
};
} // namespace nr::renderer
