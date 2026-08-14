export module nr.pipeline;
import dependency.vulkan;

import nr.app;
import nr.load;
import nr.neuralAppearanceAsset;
import nr.renderer;
import nr.resource;
import nr.scene;
import std;

export namespace nr::pipeline
{
inline constexpr std::string_view normalViewPipelineId = "normalview";
inline constexpr std::string_view rtObjectPipelineId = "rtobject";
inline constexpr std::string_view defaultPipelineId = rtObjectPipelineId;
inline constexpr std::size_t defaultModelHistoryLimit = 32u;
#if defined(NDEBUG)
inline constexpr bool benchmarkExecutionSupported = true;
#else
inline constexpr bool benchmarkExecutionSupported = false;
#endif

enum class RtPostProcessingMode : std::uint8_t
{
    accumulate,
    dlssRayReconstruction,
};

enum class RtDlssQuality : std::uint8_t
{
    dlaa,
    ultraPerformance,
};

enum class ViewerInteractionMode : std::uint8_t
{
    human,
    agent,
    offlineLua,
};

struct PipelineBuildContext
{
    vk::Format swapchainFormat = vk::Format::eUndefined;
    vk::Extent2D swapchainExtent{1u, 1u};
    RtPostProcessingMode rtPostProcessingMode = RtPostProcessingMode::dlssRayReconstruction;
    RtDlssQuality rtDlssQuality = RtDlssQuality::dlaa;
    std::string captureSessionId{"session"};
};

using PipelineGraphFactory = std::function<nr::renderer::RendererGraphSpec(const PipelineBuildContext &)>;

struct RenderPipelineDesc
{
    std::string id{};
    std::string displayName{};
    PipelineGraphFactory buildGraph{};
};

class RenderPipelineRegistry
{
  public:
    [[nodiscard]] bool registerPipeline(RenderPipelineDesc desc);
    [[nodiscard]] std::optional<std::reference_wrapper<const RenderPipelineDesc>> find(
        std::string_view id) const noexcept;
    [[nodiscard]] std::span<const RenderPipelineDesc> pipelines() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool contains(std::string_view id) const noexcept;

  private:
    std::vector<RenderPipelineDesc> pipelines_{};
    std::map<std::string, std::size_t> indexById_{};
};

void registerDefaultPipelines(RenderPipelineRegistry &registry);
[[nodiscard]] RenderPipelineRegistry makeDefaultPipelineRegistry();

[[nodiscard]] std::filesystem::path defaultModelPath();
[[nodiscard]] std::filesystem::path modelAssetRootPath();
[[nodiscard]] std::string_view defaultEnvironmentMapName() noexcept;
[[nodiscard]] std::expected<std::vector<std::string>, std::string> discoverEnvironmentMapNames();
[[nodiscard]] std::filesystem::path modelHistoryFilePath();
[[nodiscard]] std::expected<std::filesystem::path, std::string> resolveModelAssetPath(
    const std::filesystem::path &path);
[[nodiscard]] std::filesystem::path normalizeModelPathForStorage(const std::filesystem::path &path);
[[nodiscard]] std::string displayPathLeafFirst(const std::filesystem::path &path);

class ModelHistory
{
  public:
    explicit ModelHistory(std::filesystem::path storagePath = modelHistoryFilePath(),
                          std::size_t maxEntries = defaultModelHistoryLimit);

    void load();
    void save() const;
    void noteLoaded(const std::filesystem::path &path);
    [[nodiscard]] std::span<const std::filesystem::path> entries() const noexcept;
    [[nodiscard]] const std::filesystem::path &storagePath() const noexcept;

  private:
    [[nodiscard]] bool sameStoredPath(const std::filesystem::path &lhs, const std::filesystem::path &rhs) const;
    void trimToLimit();

    std::filesystem::path storagePath_{};
    std::size_t maxEntries_ = defaultModelHistoryLimit;
    std::vector<std::filesystem::path> entries_{};
};

struct ModelLoadReport
{
    bool loaded = false;
    std::filesystem::path modelPath{};
    std::string message{};
};

struct ViewerRunConfig;

class SceneModelController
{
  public:
    [[nodiscard]] ModelLoadReport loadModel(nr::app::AppSession &app, const std::filesystem::path &modelPath,
                                            std::optional<std::reference_wrapper<ModelHistory>> history = {});

    [[nodiscard]] const std::optional<std::filesystem::path> &currentModelPath() const noexcept;

  private:
    class ModelCpuLoad
    {
      public:
        ModelCpuLoad(const ModelCpuLoad &) = delete;
        ModelCpuLoad &operator=(const ModelCpuLoad &) = delete;
        ModelCpuLoad(ModelCpuLoad &&) = default;
        ModelCpuLoad &operator=(ModelCpuLoad &&) = default;
        ~ModelCpuLoad() = default;

      private:
        friend class SceneModelController;

        ModelCpuLoad(std::filesystem::path normalizedModelPath, nr::load::SceneAsset sceneAsset,
                     std::optional<nr::neuralAppearance::BindingRequest> neuralMaterialBinding)
            : normalizedModelPath_{std::move(normalizedModelPath)}, sceneAsset_{std::move(sceneAsset)},
              neuralMaterialBinding_{std::move(neuralMaterialBinding)}
        {
        }

        std::filesystem::path normalizedModelPath_{};
        nr::load::SceneAsset sceneAsset_{};
        std::optional<nr::neuralAppearance::BindingRequest> neuralMaterialBinding_{};
    };

    using ModelCpuLoadResult = std::expected<ModelCpuLoad, ModelLoadReport>;

    [[nodiscard]] static ModelCpuLoadResult loadModelCpu(const std::filesystem::path &modelPath);
    [[nodiscard]] ModelLoadReport commitModel(nr::app::AppSession &app, ModelCpuLoad &&loadedModel,
                                              std::optional<std::reference_wrapper<ModelHistory>> history = {});

    friend int runViewer(ViewerRunConfig config);

    std::optional<std::filesystem::path> currentModelPath_{};
};

struct ViewerCommandLineOptions
{
    bool showHelp = false;
    std::filesystem::path modelPath{};
    std::string pipelineId{std::string{defaultPipelineId}};
    bool benchmark = false;
    std::uint32_t warmupFrames = 0u;
    std::uint32_t measureFrames = 0u;
    std::filesystem::path outputDirectory{};
    RtDlssQuality dlssQuality = RtDlssQuality::dlaa;
    ViewerInteractionMode interactionMode = ViewerInteractionMode::human;
    std::filesystem::path automationScript{};
    std::string errorMessage{};
};

struct ViewerRunConfig
{
    std::filesystem::path initialModelPath{};
    std::string initialEnvironmentMapName{};
    std::string initialPipelineId{std::string{defaultPipelineId}};
    std::string appName{"NewbieRenderer"};
    std::string engineName{"NewbieRenderer"};
    bool benchmark = false;
    std::uint32_t warmupFrames = 0u;
    std::uint32_t measureFrames = 0u;
    std::filesystem::path outputDirectory{};
    RtDlssQuality dlssQuality = RtDlssQuality::dlaa;
    ViewerInteractionMode interactionMode = ViewerInteractionMode::human;
    std::filesystem::path automationScript{};
    std::string commandLine{};
};

[[nodiscard]] ViewerCommandLineOptions parseViewerCommandLine(std::span<char *> args);
void printViewerUsage(std::string_view executableName = "main");
[[nodiscard]] int runViewer(ViewerRunConfig config);
[[nodiscard]] int runViewerFromCommandLine(std::span<char *> args);
} // namespace nr::pipeline

namespace nr::pipeline::detail
{
struct EnvironmentMapCpuLoad
{
    std::filesystem::path sourcePath{};
    std::string name{};
    nr::resource::EnvironmentMap environmentMap{};
};

void registerNormalViewPipeline(RenderPipelineRegistry &registry);
void registerRtObjectPipeline(RenderPipelineRegistry &registry);

[[nodiscard]] std::string normalizedModelPathKey(const std::filesystem::path &path);
[[nodiscard]] std::expected<EnvironmentMapCpuLoad, std::string> loadEnvironmentMapCpu(
    std::string_view environmentMapName);
void commitEnvironmentMap(nr::renderer::Renderer &renderer, EnvironmentMapCpuLoad &&loadedEnvironmentMap);
[[nodiscard]] std::expected<void, std::string> loadEnvironmentMap(nr::renderer::Renderer &renderer,
                                                                  std::string_view environmentMapName);
} // namespace nr::pipeline::detail
