export module nr.renderer:renderer;
import dependency.math;
import dependency.vulkan;

import nr.rhi;
import nr.scene;
import nr.resource;
import nr.options;
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
static_assert(kSceneTextureDescriptorCapacity <= nr::scene::kMaxSceneTextureId + 1u, "Scene texture descriptor capacity must fit the packed uint16 material texture id ABI.");

struct RendererCreateInfo
{
    std::string appName = "NewbieRenderer";
    std::string engineName = "NewbieRenderer";
    vk::DeviceSize frameUniformBytesPerFrame = 1024u * 1024u;
    nr::rhi::PipelineCacheConfig pipelineCache{};
};

struct NodeConfig
{
    std::string instanceName{};
    QueueDomain queue = QueueDomain::Graphics;
    std::uint64_t configurationRevision = 1;
};

enum class RenderGraphSkeletonMode : std::uint8_t
{
    Legacy,
    Enabled,
    Differential,
};

[[nodiscard]] std::string_view renderGraphSkeletonModeName(RenderGraphSkeletonMode mode) noexcept;
[[nodiscard]] std::string_view renderGraphSkeletonMissReasonName(
    RenderGraphSkeletonMissReason reason) noexcept;
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
inline constexpr std::string_view sceneLightHeader = "scene.lightHeader";
inline constexpr std::string_view sceneLights = "scene.lights";
inline constexpr std::string_view sceneLightAliasTable = "scene.lightAliasTable";
} // namespace frameResource

namespace frameData
{
inline constexpr std::string_view sceneRtHitSbtPlan = "scene.rt.hitSbtPlan";
} // namespace frameData

struct FrameResolutionPlan
{
    vk::Extent2D displayExtent{1u, 1u};
    vk::Extent2D renderExtent{1u, 1u};
    bool resetHistory = false;
};

using FrameResolutionResolver = std::function<FrameResolutionPlan(
    nr::rhi::Device&,
    vk::Extent2D,
    const nr::options::OptionFrameSnapshot&)>;

class NodeRuntime;

enum class FrameEffectFinalizeDisposition : std::uint8_t
{
    terminalSucceeded,
    terminalFailed,
    continuationArmed,
};

class FrameEffectSink
{
  public:
    explicit FrameEffectSink(std::optional<nr::options::FrameEffect> effect = {});

    [[nodiscard]] const std::optional<nr::options::FrameEffect>& effect() const noexcept;
    [[nodiscard]] bool claim(NodeRuntime& runtime, GraphPassHandle targetPass) noexcept;
    [[nodiscard]] bool claimed() const noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<NodeRuntime>> claimedRuntime() const noexcept;
    [[nodiscard]] GraphPassHandle targetPass() const noexcept;

  private:
    std::optional<nr::options::FrameEffect> effect_{};
    std::optional<std::reference_wrapper<NodeRuntime>> claimedRuntime_{};
    GraphPassHandle targetPass_{};
};

struct RendererBenchmarkBuildTelemetry;

struct NodeFrameParameters
{
    std::reference_wrapper<const nr::options::OptionFrameSnapshot> optionSnapshot;
    std::uint32_t frameIndex = 0;
    vk::Extent2D swapchainExtent{1, 1};
    FrameResolutionPlan resolutionPlan{};
    vk::Format swapchainFormat = vk::Format::eUndefined;
    vk::ColorSpaceKHR swapchainColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;

    std::optional<GraphFrameDataHandle> sceneBridgeFrameHandle{};
    std::optional<std::reference_wrapper<const nr::scene::Scene>> scene{};
    nr::scene::SceneRevisionSnapshot sceneRevisions{};
    std::optional<std::reference_wrapper<const nr::scene::ScenePacketSet>> scenePackets{};
    std::optional<std::reference_wrapper<const std::vector<nr::scene::TlasBuildInputPacket>>> sceneTlasBuildInputs{};
    std::optional<std::reference_wrapper<const nr::scene::SceneResolvedCamera>> primaryCamera{};
    nr::scene::SceneBridgeFrameConstants renderCameraConstants{};
    std::optional<std::reference_wrapper<FrameServices>> frameServices{};
    std::optional<std::reference_wrapper<FrameEffectSink>> frameEffectSink{};
    std::optional<std::reference_wrapper<RendererBenchmarkBuildTelemetry>> benchmarkTelemetry{};
};

using RendererTlasTextureRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;

struct RendererTlasTexturePacketIdentity
{
    std::uint64_t renderableId = 0u;
    nr::resource::MeshHandle mesh{};
    std::uint16_t tlasBucket = 0u;

    [[nodiscard]] bool operator==(const RendererTlasTexturePacketIdentity &) const noexcept = default;
};

struct RendererTlasTextureCollectionKey
{
    std::uint64_t sceneIdentity = 0u;
    RendererTlasTextureRevisionProjection revisions{};
    std::vector<RendererTlasTexturePacketIdentity> packets{};

    [[nodiscard]] bool operator==(const RendererTlasTextureCollectionKey &) const noexcept = default;
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
    double postSceneMilliseconds = 0.0;
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

enum class RendererBenchmarkPhase : std::uint8_t
{
    disabled,
    warmup,
    measure,
    drain,
    finalized,
};

struct RendererBenchmarkConfig
{
    bool enabled = false;
    std::uint32_t warmupFrames = 0u;
    std::uint32_t measureFrames = 0u;
    std::filesystem::path outputDirectory{};
    std::string dlssQuality{"dlaa"};
    std::string modelPath{};
    std::string pipelineId{};
    RenderGraphSkeletonMode renderGraphSkeletonMode = RenderGraphSkeletonMode::Enabled;
    std::string commandLine{};
};

struct RendererBenchmarkFrame
{
    std::uint64_t frameOrdinal = 0u;
    std::uint32_t frameSlot = 0u;
    std::uint32_t configRevision = 1u;
    vk::Extent2D displayExtent{1u, 1u};
    vk::Extent2D renderExtent{1u, 1u};
    RendererCpuFrameTimings cpu{};
    double sceneBeginUploadMilliseconds = 0.0;
    double sceneRasterExtractMilliseconds = 0.0;
    double sceneTlasExtractMilliseconds = 0.0;
    double sceneBridgeMilliseconds = 0.0;
    double tlasTextureCollectionMilliseconds = 0.0;
    double graphPreludeMilliseconds = 0.0;
    double uiCollectMilliseconds = 0.0;
    double nodeLoopMilliseconds = 0.0;
    double skeletonPatchMilliseconds = 0.0;
    double skeletonRebuildMilliseconds = 0.0;
    bool skeletonHit = false;
    RenderGraphSkeletonMissReason skeletonMissReason = RenderGraphSkeletonMissReason::None;
    ExecutorBenchmarkTelemetry execute{};
    double executeAccountedMainThreadMilliseconds = 0.0;
    double executeUnclassifiedMilliseconds = 0.0;
    std::size_t sceneRasterPacketCount = 0u;
    std::size_t sceneRtPacketCount = 0u;
    std::size_t sceneTlasPacketCount = 0u;
    std::size_t submitBatchCount = 0u;
    std::size_t recordTaskCount = 0u;
};

struct RendererBenchmarkGpuPass
{
    std::uint64_t frameOrdinal = 0u;
    std::uint32_t passIndex = 0u;
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t batchIndex = 0u;
    bool isCopyPass = false;
    double milliseconds = 0.0;
};

struct RendererBenchmarkGpuFrameStatus
{
    std::uint64_t frameOrdinal = 0u;
    std::size_t expectedPassCount = 0u;
    std::size_t availablePassCount = 0u;
    bool complete = false;
};

struct RendererBenchmarkQualityAudit
{
    bool valid = true;
    bool framesValid = true;
    bool nodeTelemetryValid = true;
    bool accelerationStructureTelemetryValid = true;
    std::size_t missingGpuFrames = 0u;
    std::size_t partialGpuFrames = 0u;
    std::size_t extraGpuStatuses = 0u;
    std::size_t duplicateGpuStatuses = 0u;
    std::size_t invalidGpuDurations = 0u;
    std::size_t duplicateGpuPasses = 0u;
    std::size_t schemaDriftFrames = 0u;
    std::size_t passRowCountMismatchFrames = 0u;
    std::size_t extraGpuPassFrames = 0u;
};

struct RendererBenchmarkDistribution
{
    std::size_t count = 0u;
    double minimum = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double populationStddev = 0.0;
};

struct RendererBenchmarkAsTelemetry
{
    bool recorded = false;
    bool available = false;
    double cacheScanMilliseconds = 0.0;
    double metadataPlanMilliseconds = 0.0;
    double cpuWritesMilliseconds = 0.0;
    double tlasSizingMilliseconds = 0.0;
    double graphDeclareMilliseconds = 0.0;
    std::size_t packetCount = 0u;
    std::size_t instanceCount = 0u;
    std::size_t dirtyBlasCount = 0u;
};

struct RendererBenchmarkBuildTelemetry
{
    std::span<double> nodeBuildMilliseconds{};
    std::size_t nodeOrdinal = 0u;
    std::reference_wrapper<RendererBenchmarkAsTelemetry> accelerationStructure;
};

struct RendererGraphBuildTimings
{
    double preludeMilliseconds = 0.0;
    double uiCollectMilliseconds = 0.0;
    double nodeLoopMilliseconds = 0.0;
    double skeletonPatchMilliseconds = 0.0;
    double skeletonRebuildMilliseconds = 0.0;
    bool skeletonHit = false;
    RenderGraphSkeletonMissReason skeletonMissReason = RenderGraphSkeletonMissReason::None;
};

[[nodiscard]] double benchmarkType7Quantile(std::vector<double> values, double probability);
[[nodiscard]] std::string_view rendererBenchmarkSchemaVersion() noexcept;
[[nodiscard]] std::span<const std::string_view> rendererBenchmarkCpuStageColumns() noexcept;
[[nodiscard]] std::span<const std::string_view> rendererBenchmarkCpuSubstageColumns() noexcept;
[[nodiscard]] std::span<const std::string_view> rendererBenchmarkExecuteCsvColumns() noexcept;
[[nodiscard]] std::span<const std::string_view> rendererBenchmarkExecuteSummarySections() noexcept;
[[nodiscard]] double rendererBenchmarkClassifiedCpuMilliseconds(const RendererCpuFrameTimings &timings) noexcept;
[[nodiscard]] double rendererBenchmarkExecuteAccountedMainThreadMilliseconds(const ExecutorBenchmarkTelemetry &telemetry) noexcept;
[[nodiscard]] bool validateRendererBenchmarkExecuteTelemetry(const RendererBenchmarkFrame &frame) noexcept;
[[nodiscard]] bool validateRendererBenchmarkSkeletonTelemetry(
    const RendererBenchmarkFrame &frame,
    RenderGraphSkeletonMode mode) noexcept;
[[nodiscard]] bool validateBenchmarkFrames(
    std::span<const RendererBenchmarkFrame> frames,
    RenderGraphSkeletonMode mode);
[[nodiscard]] RendererBenchmarkQualityAudit auditRendererBenchmark(std::span<const RendererBenchmarkFrame> frames, std::span<const RendererBenchmarkGpuPass> passes, std::span<const RendererBenchmarkGpuFrameStatus> statuses, std::size_t expectedNodeCount,
                                                                   std::span<const double> nodeBuildMilliseconds, std::span<const RendererBenchmarkAsTelemetry> asTelemetry, RenderGraphSkeletonMode skeletonMode);
[[nodiscard]] RendererBenchmarkDistribution makeRendererBenchmarkDistribution(std::vector<double> values);

struct NodeInitContext
{
    std::reference_wrapper<nr::rhi::Device> device;
    std::string runtimeName{};
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

inline constexpr std::uint32_t kRendererDefaultCameraJitterCycleLength = 256u;

enum class RendererCameraJitterSequence : std::uint8_t
{
    None,
    Halton23,
};

struct RendererCameraJitterConfig
{
    RendererCameraJitterSequence sequence = RendererCameraJitterSequence::None;
    std::uint32_t cycleLength = kRendererDefaultCameraJitterCycleLength;

    [[nodiscard]] bool enabled() const noexcept
    {
        return sequence != RendererCameraJitterSequence::None && cycleLength > 0u;
    }
};

struct RendererCameraJitterSample
{
    std::uint32_t sampleIndex = 0u;
    glm::vec2 pixelOffset{0.0f};
    glm::vec2 ndcOffset{0.0f};
};

struct RendererCameraFrameState
{
    bool jitterEnabled = false;
    RendererCameraJitterSample jitter{};
    vk::Extent2D viewportExtent{1u, 1u};
};

[[nodiscard]] float haltonSequenceValue(std::uint32_t index, std::uint32_t base) noexcept;

[[nodiscard]] RendererCameraJitterSample makeHalton23CameraJitterSample(std::uint64_t frameOrdinal, vk::Extent2D viewportExtent, std::uint32_t cycleLength = kRendererDefaultCameraJitterCycleLength) noexcept;

[[nodiscard]] glm::mat4 applyCameraProjectionJitter(const glm::mat4 &projection, glm::vec2 ndcOffset) noexcept;

[[nodiscard]] RendererCameraFrameState makeRendererCameraFrameState(const RendererCameraJitterConfig &jitterConfig, std::uint64_t frameOrdinal, vk::Extent2D viewportExtent) noexcept;

struct alignas(16) EnvironmentMapParameters
{
    float radianceDecodeScale = 1.0f;
    float intensity = 1.0f;
    float yawRadians = 0.0f;
    float padding = 0.0f;
};

static_assert(sizeof(EnvironmentMapParameters) == 16u);

struct FrameGlobalResources
{
    FrameUniformBinding frameUniform{};
    GraphResourceHandle environmentMap{};
    EnvironmentMapParameters environmentMapParameters{};
    std::map<std::uint32_t, SceneTextureDescriptorBinding> sceneTextureDescriptorsById{};
    std::uint32_t sceneTextureDescriptorCapacity = kSceneTextureDescriptorCapacity;
    std::uint64_t sceneTextureDescriptorVersion = 0;
    std::reference_wrapper<BindlessImageTableCache> bindlessImageTableCache;
    RendererCameraFrameState cameraFrameState{};
};

struct NodeImageResourceDesc
{
    std::string debugName{};
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
    ImageAspectIntent aspect = ImageAspectIntent::Color;
};

struct NodeBuildContext
{
    std::reference_wrapper<RenderGraphBuilder> graphBuilder;
    GraphNodeHandle nodeHandle{};
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t frameIndex = 0;
    std::string runtimeName{};
    std::reference_wrapper<const FrameGlobalResources> globalResources;
    std::reference_wrapper<std::map<std::string, GraphResourceHandle>> frameResources;
    std::reference_wrapper<std::map<std::string, GraphFrameDataHandle>> frameDataResources;
    std::optional<std::reference_wrapper<RendererBenchmarkBuildTelemetry>> benchmarkTelemetry{};

    void publishFrameResource(std::string_view key, GraphResourceHandle resource) const;

    [[nodiscard]] GraphResourceHandle resolveFrameResource(std::string_view key) const;

    [[nodiscard]] GraphResourceHandle requireFrameResource(std::string_view key, std::string_view consumerDebugName) const;

    void publishFrameData(std::string_view key, GraphFrameDataHandle frameData) const;

    [[nodiscard]] GraphFrameDataHandle resolveFrameData(std::string_view key) const;

    [[nodiscard]] GraphFrameDataHandle requireFrameData(std::string_view key, std::string_view consumerDebugName) const;

    [[nodiscard]] std::optional<std::reference_wrapper<const std::any>> resolveFrameDataPayload(GraphFrameDataHandle handle) const;

    template <typename TPayload> [[nodiscard]] std::optional<std::reference_wrapper<const std::remove_cvref_t<TPayload>>> resolveBuildFrameData(GraphFrameDataHandle handle) const
    {
        using Payload = std::remove_cvref_t<TPayload>;

        nrAssert(handle.valid(), "NodeBuildContext::resolveBuildFrameData requires a valid frame data handle.");
        auto payload = resolveFrameDataPayload(handle);
        if (!payload.has_value())
        {
            return {};
        }

        auto const typedPayload = std::any_cast<Payload>(&payload->get());
        nrAssert(typedPayload != nullptr, std::format("NodeBuildContext::resolveBuildFrameData resolved unexpected payload type for frame data handle {}.", handle.value));
        return std::cref(*typedPayload);
    }

    template <typename TPayload> [[nodiscard]] const std::remove_cvref_t<TPayload> &buildFrameData(GraphFrameDataHandle handle) const
    {
        auto resolved = resolveBuildFrameData<TPayload>(handle);
        nrAssert(resolved.has_value(), std::format("NodeBuildContext::buildFrameData failed to resolve frame data handle {}.", handle.value));
        return resolved->get();
    }

    [[nodiscard]] std::optional<NodeImageResourceDesc> describeImageResource(GraphResourceHandle resource) const;

    // Node-scoped graph authoring helpers: Generic resource addition interface.
    template <typename TDesc> [[nodiscard]] GraphResourceHandle addResource(const TDesc &desc)
    {
        return graphBuilder.get().addResource(desc);
    }

    template <typename TPayload> [[nodiscard]] GraphFrameDataHandle importFrameData(std::string_view debugName, TPayload &&payload)
    {
        return graphBuilder.get().addFrameData(debugName, std::forward<TPayload>(payload));
    }

    [[nodiscard]] GraphResourceHandle transientColor(std::string_view debugName, vk::Extent2D extent, vk::Format format);

    [[nodiscard]] GraphResourceHandle importColor(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importStorageColor(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importRetainedStorageColor(const nr::rhi::Image &image, RetainedImageState &state, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importSampledColor(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importSampledImage(const nr::rhi::Image &image, std::string_view debugName, vk::Extent3D extent, vk::Format format, ResourceLifetime lifetime = ResourceLifetime::RendererPersistent, ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Graphics);

    [[nodiscard]] GraphResourceHandle importDepth(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime = ResourceLifetime::RendererPersistent);

    [[nodiscard]] GraphResourceHandle importBuffer(const nr::rhi::Buffer &buffer, std::string_view debugName, ResourceLifetime lifetime, std::initializer_list<BufferUsageIntent> usageIntents, ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined);

    template <std::size_t FrameSlotCount> [[nodiscard]] GraphResourceHandle importFrameColor(const std::array<nr::rhi::Image, FrameSlotCount> &images, std::string_view debugName, vk::Extent2D extent, vk::Format format)
    {
        auto const frameSlot = frameSlotIndex(FrameSlotCount);
        auto const &image = images[frameSlot];
        nrAssert(image.valid(), std::format("{} frame image slot {} is invalid.", debugName, frameSlot));

        return importColor(image, indexedFrameDebugName(debugName, frameSlot), extent, format, ResourceLifetime::FrameLocal);
    }

    template <std::size_t FrameSlotCount> [[nodiscard]] GraphResourceHandle importFrameStorageColor(const std::array<nr::rhi::Image, FrameSlotCount> &images, std::string_view debugName, vk::Extent2D extent, vk::Format format)
    {
        auto const frameSlot = frameSlotIndex(FrameSlotCount);
        auto const &image = images[frameSlot];
        nrAssert(image.valid(), std::format("{} frame storage image slot {} is invalid.", debugName, frameSlot));

        return importStorageColor(image, indexedFrameDebugName(debugName, frameSlot), extent, format, ResourceLifetime::FrameLocal);
    }

    template <std::size_t FrameSlotCount> [[nodiscard]] GraphResourceHandle importFrameDepth(const std::array<nr::rhi::Image, FrameSlotCount> &images, std::string_view debugName, vk::Extent2D extent, vk::Format format)
    {
        auto const frameSlot = frameSlotIndex(FrameSlotCount);
        auto const &image = images[frameSlot];
        nrAssert(image.valid(), std::format("{} frame depth image slot {} is invalid.", debugName, frameSlot));

        return importDepth(image, indexedFrameDebugName(debugName, frameSlot), extent, format, ResourceLifetime::FrameLocal);
    }

    [[nodiscard]] GraphResourceHandle importAccelerationStructure(const nr::rhi::AccelerationStructureResource &accelerationStructure, std::string_view debugName, ResourceLifetime lifetime = ResourceLifetime::ScenePersistent,
                                                                  ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined);

    [[nodiscard]] GraphResourceHandle importSwapchain(std::string_view debugName, const NodeFrameParameters &frameParameters);

    [[nodiscard]] GraphPassHandle addPass(std::span<const PassResourceUseDesc> intentList, std::string_view debugName, PassRecordCallback executeLambda, PassPrepareCallback prepareCallback = nullptr, bool isCopyPass = false, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{});

    [[nodiscard]] GraphPassHandle addPass(std::span<const PassResourceUseDesc> intentList, std::string_view debugName, PassParallelRecordDesc parallelRecord, PassPrepareCallback prepareCallback = nullptr, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{});

    [[nodiscard]] GraphSubmitHandle addSubmitNode(std::string_view debugName);

    [[nodiscard]] GraphSubmitHandle addSwapchainAcquireNode(std::string_view debugName);

  private:
    [[nodiscard]] GraphResourceHandle importImage(const nr::rhi::Image &image, std::string_view debugName, vk::Extent2D extent, vk::Format format, ResourceLifetime lifetime, std::initializer_list<ImageUsageIntent> usageIntents, ImageAspectIntent aspect = ImageAspectIntent::Color);

    [[nodiscard]] std::size_t frameSlotIndex(std::size_t frameSlotCount) const;

    [[nodiscard]] static std::string indexedFrameDebugName(std::string_view debugName, std::size_t frameSlot);
};

class FrameUniformArena
{
  public:
    FrameUniformArena() = default;

    FrameUniformArena(const FrameUniformArena &) = delete;
    FrameUniformArena &operator=(const FrameUniformArena &) = delete;
    FrameUniformArena(FrameUniformArena &&) noexcept = default;
    FrameUniformArena &operator=(FrameUniformArena &&) noexcept = default;

    void initialize(nr::rhi::Device &device, vk::DeviceSize bytesPerFrame, std::string_view debugName);

    void beginFrame(std::uint32_t frameIndex);

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] FrameUniformBinding uploadBytes(RenderGraphBuilder &graphBuilder, std::string_view debugName, std::span<const std::byte> bytes);

    [[nodiscard]] FrameUniformBinding patchUploadBytes(
        RenderGraphSkeletonPatchContext& patchContext,
        std::size_t resourceSlot,
        std::string_view debugName,
        std::span<const std::byte> bytes);

    template <typename TPayload>
        requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
    [[nodiscard]] FrameUniformBinding upload(RenderGraphBuilder &graphBuilder, std::string_view debugName, const TPayload &value)
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

template <typename TPipeline, std::size_t FrameSlotCount = nr::maxFrameInFlight> class PipelineRuntime
{
  public:
    PipelineRuntime() = default;

    PipelineRuntime(const PipelineRuntime &) = delete;
    PipelineRuntime &operator=(const PipelineRuntime &) = delete;
    PipelineRuntime(PipelineRuntime &&) noexcept = default;
    PipelineRuntime &operator=(PipelineRuntime &&) noexcept = default;

    ~PipelineRuntime();

    void initialize(nr::rhi::PipelineState<TPipeline> pipelineState);

    void initializeDeferred(nr::rhi::PipelineState<TPipeline> pipelineState);

    void clearBindingSets();

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] nr::rhi::PipelineState<TPipeline> &state() noexcept;

    [[nodiscard]] const nr::rhi::PipelineState<TPipeline> &state() const noexcept;

    [[nodiscard]] TPipeline &pipeline() noexcept;

    [[nodiscard]] const TPipeline &pipeline() const noexcept;

    [[nodiscard]] std::span<const nr::rhi::ShaderBindingSet> bindingSetsForFrame(std::uint32_t frameIndex) const;

    [[nodiscard]] nr::rhi::DescriptorWriteCache &descriptorWriteCacheForFrame(std::uint32_t frameIndex) noexcept;

    [[nodiscard]] bool ensureBindingSetsForFrame(std::uint32_t frameIndex, const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet);

    [[nodiscard]] nr::rhi::ShaderCursor rootCursor() const;

  private:
    void allocateBindingSetsForFrame(std::size_t frameSlot, const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet);

    nr::rhi::PipelineState<TPipeline> pipeline_{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, FrameSlotCount> bindingSetsByFrame_{};
    std::array<nr::rhi::DescriptorWriteCache, FrameSlotCount> descriptorWriteCachesByFrame_{};
    std::array<std::map<std::uint32_t, std::uint32_t>, FrameSlotCount> variableDescriptorCountsByFrame_{};
};

template <typename TPipeline, std::size_t FrameSlotCount> PipelineRuntime<TPipeline, FrameSlotCount>::~PipelineRuntime()
{
    clearBindingSets();
}

template <typename TPipeline, std::size_t FrameSlotCount> void PipelineRuntime<TPipeline, FrameSlotCount>::initialize(nr::rhi::PipelineState<TPipeline> pipelineState)
{
    clearBindingSets();
    pipeline_ = std::move(pipelineState);
    nrAssert(pipeline_.layout.valid(), "PipelineRuntime::initialize requires a valid cursor pipeline layout.");

    auto frameSlots = std::views::iota(std::size_t{0}, bindingSetsByFrame_.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) { allocateBindingSetsForFrame(frameSlot, {}); });
}

template <typename TPipeline, std::size_t FrameSlotCount> void PipelineRuntime<TPipeline, FrameSlotCount>::initializeDeferred(nr::rhi::PipelineState<TPipeline> pipelineState)
{
    clearBindingSets();
    pipeline_ = std::move(pipelineState);
    nrAssert(pipeline_.layout.valid(), "PipelineRuntime::initializeDeferred requires a valid cursor pipeline layout.");
}

template <typename TPipeline, std::size_t FrameSlotCount> void PipelineRuntime<TPipeline, FrameSlotCount>::clearBindingSets()
{
    std::ranges::for_each(bindingSetsByFrame_, [](auto &bindingSets) { bindingSets.clear(); });
    std::ranges::for_each(variableDescriptorCountsByFrame_, [](auto &variableDescriptorCounts) { variableDescriptorCounts.clear(); });
    std::ranges::for_each(descriptorWriteCachesByFrame_, [](auto &descriptorWriteCache) { descriptorWriteCache.clear(); });
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] bool PipelineRuntime<TPipeline, FrameSlotCount>::valid() const noexcept
{
    return pipeline_.pipeline.valid();
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] nr::rhi::PipelineState<TPipeline> &PipelineRuntime<TPipeline, FrameSlotCount>::state() noexcept
{
    return pipeline_;
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] const nr::rhi::PipelineState<TPipeline> &PipelineRuntime<TPipeline, FrameSlotCount>::state() const noexcept
{
    return pipeline_;
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] TPipeline &PipelineRuntime<TPipeline, FrameSlotCount>::pipeline() noexcept
{
    return pipeline_.pipeline;
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] const TPipeline &PipelineRuntime<TPipeline, FrameSlotCount>::pipeline() const noexcept
{
    return pipeline_.pipeline;
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] std::span<const nr::rhi::ShaderBindingSet> PipelineRuntime<TPipeline, FrameSlotCount>::bindingSetsForFrame(std::uint32_t frameIndex) const
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % bindingSetsByFrame_.size());
    auto const &bindingSets = bindingSetsByFrame_[frameSlot];
    return std::span<const nr::rhi::ShaderBindingSet>{bindingSets.data(), bindingSets.size()};
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] nr::rhi::DescriptorWriteCache &PipelineRuntime<TPipeline, FrameSlotCount>::descriptorWriteCacheForFrame(std::uint32_t frameIndex) noexcept
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % descriptorWriteCachesByFrame_.size());
    return descriptorWriteCachesByFrame_[frameSlot];
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] bool PipelineRuntime<TPipeline, FrameSlotCount>::ensureBindingSetsForFrame(std::uint32_t frameIndex, const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet)
{
    auto const frameSlot = static_cast<std::size_t>(frameIndex % bindingSetsByFrame_.size());
    if (!bindingSetsByFrame_[frameSlot].empty() && variableDescriptorCountsByFrame_[frameSlot] == variableDescriptorCountsBySet)
    {
        return false;
    }

    allocateBindingSetsForFrame(frameSlot, variableDescriptorCountsBySet);
    return true;
}

template <typename TPipeline, std::size_t FrameSlotCount> [[nodiscard]] nr::rhi::ShaderCursor PipelineRuntime<TPipeline, FrameSlotCount>::rootCursor() const
{
    return pipeline_.descriptorLayout.rootCursor();
}

template <typename TPipeline, std::size_t FrameSlotCount> void PipelineRuntime<TPipeline, FrameSlotCount>::allocateBindingSetsForFrame(std::size_t frameSlot, const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet)
{
    nrAssert(frameSlot < bindingSetsByFrame_.size(), "PipelineRuntime frame slot is out of range.");
    bindingSetsByFrame_[frameSlot] = nr::rhi::allocateBindingSetsForLayout(pipeline_.layout, pipeline_.bindingPool, variableDescriptorCountsBySet);
    variableDescriptorCountsByFrame_[frameSlot] = variableDescriptorCountsBySet;
    descriptorWriteCachesByFrame_[frameSlot].clear();
}

struct RasterPassRecordContext
{
    const PassRecordContext &pass;
    const vk::raii::CommandBuffer &commandBuffer;
    const nr::rhi::ShaderDescriptorLayout &descriptorLayout;
    const nr::rhi::CursorPipelineLayout &pipelineLayout;
    vk::Extent2D extent{1, 1};

    template <typename TPayload> void pushConstants(std::string_view shaderPath, const TPayload &value) const
    {
        auto root = descriptorLayout.rootCursor();
        auto cursor = root.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));

        auto bindingSnapshot = root.snapshot();
        root.clearSnapshot();
        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipelineLayout, bindingSnapshot);
    }
};

using RasterPassRecordCallback = std::function<void(const RasterPassRecordContext &)>;

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

[[nodiscard]] inline PushConstantLocation resolvePushConstantLocation(const nr::rhi::ShaderDescriptorLayout &descriptorLayout, std::string_view shaderPath)
{
    auto cursor = descriptorLayout.rootCursor().getPath(shaderPath);
    auto pushConstantRange = cursor.pushConstantRange();
    nrAssert(pushConstantRange.has_value(), std::format("Push constant path '{}' must reference push-constant storage.", shaderPath));

    auto const cursorOffset = cursor.address().uniformOffset;
    auto const rangeBegin = static_cast<std::uint64_t>(pushConstantRange->offset);
    auto const rangeEnd = rangeBegin + static_cast<std::uint64_t>(pushConstantRange->size);
    nrAssert(cursorOffset <= std::numeric_limits<std::uint32_t>::max(), std::format("Push constant cursor offset overflow for '{}': {}", shaderPath, cursorOffset));
    nrAssert(cursorOffset >= rangeBegin && cursorOffset < rangeEnd, std::format("Push constant path '{}' resolved outside its range. offset={}, rangeBegin={}, rangeEnd={}", shaderPath, cursorOffset, rangeBegin, rangeEnd));

    auto const remainingBytes = rangeEnd - cursorOffset;
    nrAssert(remainingBytes <= std::numeric_limits<std::uint32_t>::max(), std::format("Push constant path '{}' remaining byte count overflow: {}", shaderPath, remainingBytes));

    return PushConstantLocation{
        .stageFlags = pushConstantRange->stageFlags,
        .offset = static_cast<std::uint32_t>(cursorOffset),
        .maxBytes = static_cast<std::uint32_t>(remainingBytes),
    };
}

template <typename TPayload>
    requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
void pushConstantsToLocation(const vk::raii::CommandBuffer &commandBuffer, const nr::rhi::CursorPipelineLayout &pipelineLayout, PushConstantLocation location, const TPayload &value)
{
    nrAssert(location.valid(), "pushConstantsToLocation requires a valid push-constant location.");
    auto bytes = std::as_bytes(std::span{std::addressof(value), std::size_t{1}});
    nrAssert(bytes.size() <= location.maxBytes, std::format("Push constant payload exceeds reflected range. size={}, maxBytes={}, offset={}", bytes.size(), location.maxBytes, location.offset));

    auto const *rawBytes = reinterpret_cast<const std::uint8_t *>(bytes.data());
    pipelineLayout.pushConstants(commandBuffer, location.stageFlags, location.offset, std::span<const std::uint8_t>{rawBytes, bytes.size()});
}

struct RasterPassRangeRecordContext
{
    const PassRecordContext &pass;
    const PassParallelRecordPlan &plan;
    std::size_t chunkIndex = 0;
    ParallelRecordRange range{};
    const vk::raii::CommandBuffer &commandBuffer;
    const nr::rhi::ShaderDescriptorLayout &descriptorLayout;
    const nr::rhi::CursorPipelineLayout &pipelineLayout;
    vk::Extent2D extent{1, 1};

    [[nodiscard]] PushConstantLocation pushConstantLocation(std::string_view shaderPath) const
    {
        return resolvePushConstantLocation(descriptorLayout, shaderPath);
    }

    template <typename TPayload>
        requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
    void pushConstants(PushConstantLocation location, const TPayload &value) const
    {
        pushConstantsToLocation(commandBuffer, pipelineLayout, location, value);
    }

    template <typename TPayload>
        requires(std::is_trivially_copyable_v<std::remove_cvref_t<TPayload>>)
    void pushConstants(std::string_view shaderPath, const TPayload &value) const
    {
        pushConstants(pushConstantLocation(shaderPath), value);
    }
};

using RasterPassItemCountCallback = std::function<std::size_t(const PassRecordContext &)>;
using RasterPassRangeRecordCallback = std::function<void(const RasterPassRangeRecordContext &)>;

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

using ShaderVisiblePassPrepareCallback = std::function<void(const PassPrepareContext &)>;
using RasterPassPrepareCallback = ShaderVisiblePassPrepareCallback;
using ComputePassPrepareCallback = ShaderVisiblePassPrepareCallback;
using PassBindingSnapshotCallback = std::function<nr::rhi::ShaderBindingSnapshot(const PassPrepareContext &)>;

struct DynamicBindingSnapshotDesc
{
    PassBindingSnapshotCallback snapshot{};
    nr::rhi::LogicalDescriptorResolver resolver{};
};

namespace detail
{
template <typename TDerived, typename TPipeline, vk::PipelineBindPoint BindPoint> class ShaderVisiblePassBuilderBase
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

    ShaderVisiblePassBuilderBase(NodeBuildContext &context, std::string_view debugName, RuntimePtr runtime, std::string_view builderLabel) : context_(context), debugName_(debugName), runtime_(std::move(runtime)), builderLabel_(builderLabel)
    {
        nrAssert(static_cast<bool>(runtime_), std::format("{} requires a valid PipelineRuntime shared pointer.", builderLabel_));
        nrAssert(runtime_->valid(), std::format("{} requires initialized PipelineRuntime state.", builderLabel_));
        rootCursor_ = runtime_->rootCursor();
    }

    TDerived &uniform(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{})
    {
        nrAssert(resource.valid(), std::format("{}::uniform requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(withOptionalShaderStages(use::uniformRead(resource), shaderStages));
        return derived();
    }

    TDerived &uniform(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, ShaderStageIntent shaderStage)
    {
        return uniform(shaderPath, resource, debugName, use::shaderStageScope(shaderStage));
    }

    TDerived &uniform(std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{})
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
        resourceUses_.push_back(withOptionalShaderStages(use::uniformRead(binding.resource), shaderStages));
        return derived();
    }

    TDerived &uniform(std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName, ShaderStageIntent shaderStage)
    {
        return uniform(shaderPath, binding, debugName, use::shaderStageScope(shaderStage));
    }

    TDerived &sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{})
    {
        nrAssert(resource.valid(), std::format("{}::sampledImage requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        resourceUses_.push_back(withOptionalShaderStages(use::sampledRead(resource), shaderStages));
        return derived();
    }

    TDerived &sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, ShaderStageIntent shaderStage)
    {
        return sampledImage(shaderPath, resource, debugName, use::shaderStageScope(shaderStage));
    }

    TDerived &sampledImageGeneral(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{})
    {
        nrAssert(resource.valid(), std::format("{}::sampledImageGeneral requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
            .imageLayout = vk::ImageLayout::eGeneral,
        }));
        auto resourceUse = PassResourceUseDesc{
            .resource = resource,
            .imageUsage = ImageUsageIntent::Sampled,
            .imageAccess = ImageAccessIntent::SampledRead,
            .imageLayout = ImageLayoutIntent::General,
            .readOnly = true,
        };
        resourceUses_.push_back(withOptionalShaderStages(std::move(resourceUse), shaderStages));
        return derived();
    }

    TDerived &sampledImageGeneral(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, ShaderStageIntent shaderStage)
    {
        return sampledImageGeneral(shaderPath, resource, debugName, use::shaderStageScope(shaderStage));
    }

    TDerived &storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{})
    {
        nrAssert(resource.valid(), std::format("{}::storageImage requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(withOptionalShaderStages(use::storageWrite(resource), shaderStages));
        return derived();
    }

    TDerived &storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, ShaderStageIntent shaderStage)
    {
        return storageImage(shaderPath, resource, debugName, use::shaderStageScope(shaderStage));
    }

    TDerived &storageBuffer(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, vk::PipelineStageFlags2 shaderStages = vk::PipelineStageFlags2{})
    {
        nrAssert(resource.valid(), std::format("{}::storageBuffer requires a valid graph resource.", builderLabel_));
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(withOptionalShaderStages(use::storageBufferRead(resource), shaderStages));
        return derived();
    }

    TDerived &storageBuffer(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName, ShaderStageIntent shaderStage)
    {
        return storageBuffer(shaderPath, resource, debugName, use::shaderStageScope(shaderStage));
    }

    TDerived &accelerationStructure(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
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

    template <typename TPayload> TDerived &pushConstants(std::string_view shaderPath, const TPayload &value)
    {
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));
        return derived();
    }

    TDerived &resourceUse(PassResourceUseDesc resourceUse)
    {
        nrAssert(resourceUse.resource.valid(), std::format("{}::resourceUse requires a valid graph resource.", builderLabel_));
        resourceUses_.push_back(resourceUse);
        return derived();
    }

    TDerived &prepare(ShaderVisiblePassPrepareCallback callback)
    {
        prepareCallbacks_.push_back(std::move(callback));
        return derived();
    }

    TDerived &dynamicBindingSnapshot(PassBindingSnapshotCallback snapshotCallback, nr::rhi::LogicalDescriptorResolver resolver = {})
    {
        nrAssert(static_cast<bool>(snapshotCallback), std::format("{}::dynamicBindingSnapshot requires a snapshot callback.", builderLabel_));
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
        auto prepareCallback = makePrepareCallback(runtime, bindingSnapshot, std::move(prepareCallbacks_), std::move(dynamicBindingSnapshots_), builderLabel_);

        return CommonBuildState{
            .resourceUses = std::move(resourceUses_),
            .runtime = std::move(runtime),
            .debugName = std::move(debugName_),
            .bindingSnapshot = std::move(bindingSnapshot),
            .prepareCallback = std::move(prepareCallback),
        };
    }

    [[nodiscard]] static PassPrepareCallback makePrepareCallback(RuntimePtr runtime, nr::rhi::ShaderBindingSnapshot bindingSnapshot, std::vector<ShaderVisiblePassPrepareCallback> prepareCallbacks, std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots, std::string builderLabel)
    {
        return [runtime = std::move(runtime), bindingSnapshot = std::move(bindingSnapshot), prepareCallbacks = std::move(prepareCallbacks), dynamicBindingSnapshots = std::move(dynamicBindingSnapshots), builderLabel = std::move(builderLabel)](const PassPrepareContext &prepareContext) {
            nrAssert(static_cast<bool>(runtime), std::format("{} prepare requires initialized runtime state.", builderLabel));
            std::ranges::for_each(prepareCallbacks, [&](const ShaderVisiblePassPrepareCallback &callback) {
                if (callback)
                {
                    callback(prepareContext);
                }
            });

            auto &descriptorWriteCache = runtime->descriptorWriteCacheForFrame(prepareContext.frameIndex);
            nr::rhi::updateResourcesForBindingSnapshot(runtime->state().bindingPool, runtime->bindingSetsForFrame(prepareContext.frameIndex), descriptorWriteCache, bindingSnapshot, makeDefaultLogicalDescriptorResolver(prepareContext));

            std::ranges::for_each(dynamicBindingSnapshots, [&](const DynamicBindingSnapshotDesc &desc) {
                auto dynamicSnapshot = desc.snapshot(prepareContext);
                auto resolver = desc.resolver ? desc.resolver : makeDefaultLogicalDescriptorResolver(prepareContext);
                nr::rhi::updateResourcesForBindingSnapshot(runtime->state().bindingPool, runtime->bindingSetsForFrame(prepareContext.frameIndex), descriptorWriteCache, dynamicSnapshot, std::move(resolver));
            });
        };
    }

    static void bindPipelinePreparedResourcesAndPushConstants(const vk::raii::CommandBuffer &commandBuffer, const Runtime &runtime, const nr::rhi::ShaderBindingSnapshot &bindingSnapshot, std::uint32_t frameIndex)
    {
        commandBuffer.bindPipeline(BindPoint, runtime.pipeline().raw());

        nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer, BindPoint, runtime.state().layout, runtime.bindingSetsForFrame(frameIndex));

        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, runtime.state().layout, bindingSnapshot);
    }

    std::reference_wrapper<NodeBuildContext> context_;

  private:
    [[nodiscard]] static PassResourceUseDesc withOptionalShaderStages(PassResourceUseDesc resourceUse, vk::PipelineStageFlags2 shaderStages) noexcept
    {
        if (shaderStages != vk::PipelineStageFlags2{})
        {
            resourceUse.shaderStages = shaderStages;
        }
        return resourceUse;
    }

    [[nodiscard]] TDerived &derived() noexcept
    {
        return static_cast<TDerived &>(*this);
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

class RasterPassBuilder : public detail::ShaderVisiblePassBuilderBase<RasterPassBuilder, nr::rhi::GraphicsPipeline, vk::PipelineBindPoint::eGraphics>
{
    using Base = detail::ShaderVisiblePassBuilderBase<RasterPassBuilder, nr::rhi::GraphicsPipeline, vk::PipelineBindPoint::eGraphics>;

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

    RasterPassBuilder(NodeBuildContext &context, std::string_view debugName, std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime);

    RasterPassBuilder &viewport(vk::Extent2D extent);

    RasterPassBuilder &viewportYMode(RasterViewportYMode mode);

    RasterPassBuilder &colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue);

    RasterPassBuilder &depthAttachment(GraphResourceHandle resource);

    RasterPassBuilder &rasterState(nr::rhi::MeshRasterState state);

    RasterPassBuilder &primitiveTopology(vk::PrimitiveTopology topology);

    RasterPassBuilder &record(RasterPassRecordCallback callback);

    RasterPassBuilder &recordParallel(RasterPassItemCountCallback itemCountCallback, RasterPassRangeRecordCallback rangeRecordCallback);

    [[nodiscard]] GraphPassHandle build();

  private:
    friend class RasterPassPatchBuilder;

    struct RasterPassRenderingSetup
    {
        std::vector<PassImageResource> resolvedColors{};
        std::optional<PassImageResource> resolvedDepth{};
        vk::Extent2D targetExtent{1, 1};
        std::vector<nr::rhi::ops::RenderingAttachmentDesc> colorAttachments{};
        std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc> depthAttachment{};
        std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc> stencilAttachment{};
    };

    [[nodiscard]] static vk::Extent2D resolveTargetExtent(std::optional<vk::Extent2D> viewportExtent, std::span<const PassImageResource> resolvedColors, const std::optional<PassImageResource> &resolvedDepth);

    [[nodiscard]] static RasterPassRenderingSetup makeRenderingSetup(const PassRecordContext &recordContext, std::span<const RasterColorAttachment> colorAttachments, const std::optional<RasterDepthAttachment> &depthAttachment, std::optional<vk::Extent2D> viewportExtent, std::string_view debugName);

    [[nodiscard]] static PassPrimaryRecordScope makeDynamicRenderingSecondaryScope(const RasterPassRenderingSetup &setup, const PipelineRuntime<nr::rhi::GraphicsPipeline> &runtime, std::string_view debugName);

    static void bindGraphicsSetup(const vk::raii::CommandBuffer &commandBuffer, const PipelineRuntime<nr::rhi::GraphicsPipeline> &runtime, const nr::rhi::ShaderBindingSnapshot &bindingSnapshot, std::uint32_t frameIndex, vk::Extent2D targetExtent, RasterViewportYMode viewportYMode,
                                  nr::rhi::MeshRasterState rasterState, vk::PrimitiveTopology primitiveTopology);

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

class RasterPassPatchBuilder
{
  public:
    RasterPassPatchBuilder(RenderGraphSkeletonPatchContext& context, std::size_t passSlot,
                           std::string_view debugName,
                           std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime);
    RasterPassPatchBuilder& viewport(vk::Extent2D extent);
    RasterPassPatchBuilder& colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue);
    RasterPassPatchBuilder& rasterState(nr::rhi::MeshRasterState state);
    RasterPassPatchBuilder& prepare(RasterPassPrepareCallback callback);
    RasterPassPatchBuilder& dynamicBindingSnapshot(
        PassBindingSnapshotCallback callback,
        nr::rhi::LogicalDescriptorResolver resolver = {});
    RasterPassPatchBuilder& record(RasterPassRecordCallback callback);
    void patch();

  private:
    std::reference_wrapper<RenderGraphSkeletonPatchContext> context_;
    std::size_t passSlot_ = 0;
    std::string debugName_{};
    std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime_{};
    nr::rhi::ShaderCursor rootCursor_{};
    std::optional<vk::Extent2D> viewportExtent_{};
    std::vector<RasterColorAttachment> colorAttachments_{};
    nr::rhi::MeshRasterState rasterState_{};
    std::vector<RasterPassPrepareCallback> prepareCallbacks_{};
    std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots_{};
    RasterPassRecordCallback recordCallback_{};
};

struct ComputePassRecordContext
{
    const PassRecordContext &pass;
    const vk::raii::CommandBuffer &commandBuffer;
    const nr::rhi::ShaderDescriptorLayout &descriptorLayout;
    const nr::rhi::CursorPipelineLayout &pipelineLayout;

    template <typename TPayload> void pushConstants(std::string_view shaderPath, const TPayload &value) const
    {
        auto root = descriptorLayout.rootCursor();
        auto cursor = root.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));

        auto bindingSnapshot = root.snapshot();
        root.clearSnapshot();
        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipelineLayout, bindingSnapshot);
    }
};

using ComputePassRecordCallback = std::function<void(const ComputePassRecordContext &)>;

class ComputePassBuilder : public detail::ShaderVisiblePassBuilderBase<ComputePassBuilder, nr::rhi::ComputePipeline, vk::PipelineBindPoint::eCompute>
{
    using Base = detail::ShaderVisiblePassBuilderBase<ComputePassBuilder, nr::rhi::ComputePipeline, vk::PipelineBindPoint::eCompute>;

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

    ComputePassBuilder(NodeBuildContext &context, std::string_view debugName, std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime);

    ComputePassBuilder &record(ComputePassRecordCallback callback);

    [[nodiscard]] GraphPassHandle build();

  private:
    ComputePassRecordCallback recordCallback_{};
};

class ComputePassPatchBuilder
{
  public:
    ComputePassPatchBuilder(
        RenderGraphSkeletonPatchContext& context,
        std::size_t passSlot,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime);

    ComputePassPatchBuilder& sampledImage(
        std::string_view shaderPath,
        GraphResourceHandle resource,
        std::string_view debugName);

    ComputePassPatchBuilder& storageImage(
        std::string_view shaderPath,
        GraphResourceHandle resource,
        std::string_view debugName);

    template <typename TPayload>
    ComputePassPatchBuilder& pushConstants(std::string_view shaderPath, const TPayload& value)
    {
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));
        return *this;
    }

    ComputePassPatchBuilder& record(ComputePassRecordCallback callback);

    void patch();

  private:
    std::reference_wrapper<RenderGraphSkeletonPatchContext> context_;
    std::size_t passSlot_ = 0;
    std::string debugName_{};
    std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime_{};
    nr::rhi::ShaderCursor rootCursor_{};
    ComputePassRecordCallback recordCallback_{};
};

struct RayTracingPassRecordContext
{
    const PassRecordContext &pass;
    const vk::raii::CommandBuffer &commandBuffer;
    const nr::rhi::ShaderDescriptorLayout &descriptorLayout;
    const nr::rhi::CursorPipelineLayout &pipelineLayout;
};

using RayTracingPassRecordCallback = std::function<void(const RayTracingPassRecordContext &)>;

class RayTracingPassBuilder : public detail::ShaderVisiblePassBuilderBase<RayTracingPassBuilder, nr::rhi::RayTracingPipeline, vk::PipelineBindPoint::eRayTracingKHR>
{
    using Base = detail::ShaderVisiblePassBuilderBase<RayTracingPassBuilder, nr::rhi::RayTracingPipeline, vk::PipelineBindPoint::eRayTracingKHR>;

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

    RayTracingPassBuilder(NodeBuildContext &context, std::string_view debugName, std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime);

    RayTracingPassBuilder &record(RayTracingPassRecordCallback callback);

    [[nodiscard]] GraphPassHandle build();

  private:
    RayTracingPassRecordCallback recordCallback_{};
};

class RayTracingPassPatchBuilder
{
  public:
    RayTracingPassPatchBuilder(RenderGraphSkeletonPatchContext& context, std::size_t passSlot,
                               std::string_view debugName,
                               std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime);
    RayTracingPassPatchBuilder& accelerationStructure(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);
    RayTracingPassPatchBuilder& sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);
    RayTracingPassPatchBuilder& storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);
    RayTracingPassPatchBuilder& storageBuffer(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);
    RayTracingPassPatchBuilder& uniform(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName);
    RayTracingPassPatchBuilder& uniform(std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName);

    template <typename TPayload>
    RayTracingPassPatchBuilder& pushConstants(std::string_view shaderPath, const TPayload& value)
    {
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setData(value));
        return *this;
    }

    RayTracingPassPatchBuilder& prepare(ShaderVisiblePassPrepareCallback callback);
    RayTracingPassPatchBuilder& dynamicBindingSnapshot(
        PassBindingSnapshotCallback callback,
        nr::rhi::LogicalDescriptorResolver resolver = {});
    RayTracingPassPatchBuilder& record(RayTracingPassRecordCallback callback);
    void patch();

  private:
    RayTracingPassPatchBuilder& descriptor(
        std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName,
        vk::ImageLayout imageLayout = vk::ImageLayout::eGeneral,
        vk::DeviceSize offset = 0u,
        vk::DeviceSize range = std::numeric_limits<vk::DeviceSize>::max());

    std::reference_wrapper<RenderGraphSkeletonPatchContext> context_;
    std::size_t passSlot_ = 0;
    std::string debugName_{};
    std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime_{};
    nr::rhi::ShaderCursor rootCursor_{};
    std::vector<ShaderVisiblePassPrepareCallback> prepareCallbacks_{};
    std::vector<DynamicBindingSnapshotDesc> dynamicBindingSnapshots_{};
    RayTracingPassRecordCallback recordCallback_{};
};

class NodeRuntime
{
  public:
    virtual ~NodeRuntime() = default;

    // Non-empty semantic IDs identify actionable runtime roles that must be
    // unique in an installed graph.
    [[nodiscard]] virtual std::string_view actionableSemantic() const noexcept;

    // Pure graph registration. Implementations must not access Device or mutable runtime state.
    virtual void declareOptions(nr::options::OptionCatalogBuilder& builder) const;

    // Collect conservative live availability for this node's declared options.
    virtual void collectOptionAvailability(
        const nr::options::OptionFrameSnapshot& snapshot,
        nr::options::OptionAvailabilityMap& availability) const;

    // Stage 1 (initialize): create persistent node state.
    // Typical work: shader/pipeline creation and long-lived GPU allocations.
    virtual void initialize(NodeInitContext &);

    // Stage 2 (build): declare per-frame intents and register execute lambdas.
    // Canonical path: context.addPass(intentList, name, executeLambda[, isCopyPass]).
    // Build should capture stable per-pass snapshots used later by execute lambdas.
    virtual void build(NodeBuildContext &context, const NodeFrameParameters &frameParameters) = 0;

    struct StructuralSnapshot
    {
        std::uint64_t configurationRevision = 1;
        std::string branchKey{};
    };

    /// Opt-in for the generic Skeleton contract. Unsupported nodes force legacy graph build.
    [[nodiscard]] virtual bool supportsRenderGraphSkeleton() const noexcept;

    /// Captures the exact node-owned topology branch before materialization.
    [[nodiscard]] virtual std::optional<StructuralSnapshot> structuralSnapshot(
        const NodeFrameParameters& frameParameters) const;

    /// Materializes current frame references and callbacks for a cached structural variant.
    virtual bool materializeRenderGraphSkeleton(
        RenderGraphSkeletonPatchContext& context,
        const NodeFrameParameters& frameParameters,
        const StructuralSnapshot& snapshot);

    // Stage 3 (shutdown): release persistent node state.
    virtual void advanceContinuations(std::uint32_t frameSlot);
    virtual void flushContinuations();
    [[nodiscard]] virtual FrameEffectFinalizeDisposition finalizeFrameEffect(
        const nr::options::FrameEffect& effect,
        bool targetBatchSubmitted,
        std::uint32_t frameSlot);
    virtual void shutdown(NodeShutdownContext &);
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
    RendererCameraJitterConfig cameraJitter{};
    std::optional<FrameResolutionResolver> frameResolutionResolver{};
    std::vector<nr::options::OptionId> frameResolutionOptionRequirements{};
};

struct RendererGraphPreflightResult
{
    bool valid = false;
    std::string message{};
    std::shared_ptr<const nr::options::OptionCatalog> optionCatalog{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return valid;
    }
};

struct RendererFrameInput
{
    std::reference_wrapper<const nr::options::OptionFrameSnapshot> optionSnapshot;
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
    std::size_t replayedSecondaryCommandBufferCount = 0;
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

    void initialize(const RendererCreateInfo &info = {});

    [[nodiscard]] RendererGraphPreflightResult preflightGraph(
        const RendererGraphSpec& spec) const;

    [[nodiscard]] bool installGraph(const RendererGraphSpec &spec);

    void uninstallGraph();

    void shutdown();

    void setEnvironmentMap(nr::resource::EnvironmentMap environment);

    [[nodiscard]] bool initialized() const noexcept;

    [[nodiscard]] bool graphInstalled() const noexcept;

    void resize();

    void resetSceneBinding() noexcept;

    void collectOptionAvailability(
        const nr::options::OptionFrameSnapshot& snapshot,
        nr::options::OptionAvailabilityMap& availability) const;

    [[nodiscard]] RendererFrameResult renderFrame(const RendererFrameInput &input);

    [[nodiscard]] nr::rhi::Device &device();

    [[nodiscard]] const nr::rhi::Device &device() const;

    [[nodiscard]] RenderGraphExecutor &graphExecutor() noexcept;

    [[nodiscard]] const RenderGraphExecutor &graphExecutor() const noexcept;

    [[nodiscard]] const RendererCpuStatistics &cpuStatistics() const noexcept;

    [[nodiscard]] const RendererGpuPassStatistics &gpuPassStatistics() const noexcept;

    void configureBenchmark(RendererBenchmarkConfig config);

    void configureRenderGraphSkeletonMode(RenderGraphSkeletonMode mode) noexcept;

    [[nodiscard]] RenderGraphSkeletonMode renderGraphSkeletonMode() const noexcept;

    [[nodiscard]] RenderGraphSkeletonCacheStatistics renderGraphSkeletonStatistics() const noexcept;

    [[nodiscard]] bool benchmarkComplete() const noexcept;

    [[nodiscard]] bool finalizeBenchmark();

  private:
    struct InstalledNode
    {
        std::shared_ptr<NodeRuntime> runtime{};
        NodeConfig config{};
    };

    [[nodiscard]] RendererGraphBuildTimings buildInstalledGraph(const NodeFrameParameters &frameParameters, const nr::scene::SceneBridgeFrameConstants &frameConstants, const RendererCameraFrameState &cameraFrameState, std::uint64_t sampleFrameOrdinal,
                                                                std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame, const std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById);

    void teardownInstalledGraph();

    [[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> ensureSceneExtractProfile(nr::scene::Scene &scene);

    [[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> ensureSceneTlasExtractProfile(nr::scene::Scene &scene);

    void recordCpuTimingSample(const RendererCpuFrameTimings &timings) noexcept;

    void recordGpuPassTimingSample(const GpuPassTimingFrame &timings);

    void recordBenchmarkGpuPassTimings(const GpuPassTimingFrame &timings);

    void ensureSceneTextureFallback();

    void ensureEnvironmentMapFallback();

    [[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeSampledImageUploadPlan() const;

    void synchronizeSampledImageUpload(const nr::rhi::ops::ImageUploadTicket &uploadTicket, std::string_view debugName);

    void uploadSceneTextureFallback();

    [[nodiscard]] RendererSceneTextureDescriptorTable buildSceneTextureDescriptorTable(const NodeFrameParameters &frameParameters, const std::map<std::uint32_t, nr::resource::TextureHandle> &sceneTextureHandlesById);

    std::unique_ptr<nr::rhi::Device> device_{};
    RenderGraphBuilder builder_{};
    RenderGraphExecutor executor_{};
    RendererCacheSuite cacheSuite_{};
    RendererSubmissionTimelines submissionTimelines_{};
    FrameUniformArena frameUniformArena_{};
    nr::rhi::Image sceneTextureFallback_{};
    nr::rhi::Image environmentMapImage_{};
    RetainedImageState environmentMapState_{};
    EnvironmentMapParameters environmentMapParameters_{};

    bool graphInstalled_ = false;
    std::vector<InstalledNode> installedNodes_{};
    std::multimap<std::size_t, SubmitNodeSpec> submitNodesByAfterIndex_{};
    RendererCameraJitterConfig cameraJitter_{};
    std::optional<FrameResolutionResolver> frameResolutionResolver_{};
    std::uint64_t sampleFrameOrdinal_ = 0u;
    std::uint64_t installedGraphGeneration_ = 0u;
    std::uint64_t observedSwapchainRecreationGeneration_ = 0u;
    RenderGraphSkeletonMode renderGraphSkeletonMode_ = RenderGraphSkeletonMode::Enabled;
    bool temporalHistoryResetPending_ = false;

    std::optional<std::reference_wrapper<nr::scene::Scene>> activeScene_{};
    std::optional<RendererTlasTextureCollectionKey> tlasTextureCollectionKey_{};
    std::map<std::uint32_t, nr::resource::TextureHandle> tlasTextureHandlesById_{};
    std::optional<nr::scene::SceneExtractProfileHandle> sceneExtractProfile_{};
    std::optional<nr::scene::SceneExtractProfileHandle> sceneTlasExtractProfile_{};
    std::optional<nr::scene::SceneBridgeFrameConstants> previousGlobalFrameConstants_{};
    RendererCpuFrameTimings cpuTimingAccumulator_{};
    RendererCpuStatistics cpuStatistics_{};
    std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage> gpuPassTimingAccumulator_{};
    RendererGpuPassStatistics gpuPassStatistics_{};
    RendererBenchmarkConfig benchmarkConfig_{};
    RendererBenchmarkPhase benchmarkPhase_ = RendererBenchmarkPhase::disabled;
    std::uint32_t benchmarkWarmupAccepted_ = 0u;
    std::uint32_t benchmarkDrainRendered_ = 0u;
    std::vector<RendererBenchmarkFrame> benchmarkFrames_{};
    std::vector<RendererBenchmarkGpuPass> benchmarkGpuPasses_{};
    std::vector<RendererBenchmarkGpuFrameStatus> benchmarkGpuFrameStatuses_{};
    std::vector<std::string> benchmarkGpuPassNames_{};
    std::vector<std::string> benchmarkNodeNames_{};
    std::vector<double> benchmarkCurrentNodeBuildMilliseconds_{};
    std::vector<double> benchmarkNodeBuildMilliseconds_{};
    RendererBenchmarkAsTelemetry benchmarkCurrentAsTelemetry_{};
    std::vector<RendererBenchmarkAsTelemetry> benchmarkAsTelemetry_{};
    RenderGraphSkeletonCacheStatistics benchmarkSkeletonStatisticsBefore_{};
    std::chrono::system_clock::time_point benchmarkStartedAt_{};
    bool benchmarkFinalized_ = false;
    bool benchmarkSucceeded_ = false;
};
} // namespace nr::renderer
