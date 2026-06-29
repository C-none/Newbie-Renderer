export module nr.pipeline;
import dependency.vulkan;

import nr.app;
import nr.renderer;
import nr.scene;
import std;

export namespace nr::pipeline
{
inline constexpr std::string_view normalViewPipelineId = "normalview";
inline constexpr std::string_view rtObjectPipelineId = "rtobject";
inline constexpr std::size_t defaultModelHistoryLimit = 32u;

struct PipelineBuildContext
{
    vk::Format swapchainFormat = vk::Format::eUndefined;
    vk::Extent2D swapchainExtent{1u, 1u};
};

using PipelineGraphFactory = std::function<nr::renderer::RendererGraphSpec(const PipelineBuildContext&)>;

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
    [[nodiscard]] std::optional<std::reference_wrapper<const RenderPipelineDesc>> find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const RenderPipelineDesc> pipelines() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool contains(std::string_view id) const noexcept;

  private:
    std::vector<RenderPipelineDesc> pipelines_{};
    std::map<std::string, std::size_t> indexById_{};
};

void registerDefaultPipelines(RenderPipelineRegistry& registry);
[[nodiscard]] RenderPipelineRegistry makeDefaultPipelineRegistry();

[[nodiscard]] std::filesystem::path defaultModelPath();
[[nodiscard]] std::filesystem::path modelHistoryFilePath();
[[nodiscard]] std::filesystem::path normalizeModelPathForStorage(const std::filesystem::path& path);
[[nodiscard]] std::string displayPathLeafFirst(const std::filesystem::path& path);

class ModelHistory
{
  public:
    explicit ModelHistory(
        std::filesystem::path storagePath = modelHistoryFilePath(),
        std::size_t maxEntries = defaultModelHistoryLimit);

    void load();
    void save() const;
    void noteLoaded(const std::filesystem::path& path);
    [[nodiscard]] std::span<const std::filesystem::path> entries() const noexcept;
    [[nodiscard]] const std::filesystem::path& storagePath() const noexcept;

  private:
    [[nodiscard]] bool sameStoredPath(
        const std::filesystem::path& lhs,
        const std::filesystem::path& rhs) const;
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

class SceneModelController
{
  public:
    [[nodiscard]] ModelLoadReport loadModel(
        nr::app::AppSession& app,
        const std::filesystem::path& modelPath,
        std::optional<std::reference_wrapper<ModelHistory>> history = {});

    [[nodiscard]] const std::optional<std::filesystem::path>& currentModelPath() const noexcept;

  private:
    std::optional<std::filesystem::path> currentModelPath_{};
};

struct ViewerCommandLineOptions
{
    bool showHelp = false;
    std::filesystem::path modelPath{};
    std::string pipelineId{std::string{normalViewPipelineId}};
    std::string errorMessage{};
};

struct ViewerRunConfig
{
    std::filesystem::path initialModelPath{};
    std::string initialPipelineId{std::string{normalViewPipelineId}};
    std::string appName{"NewbieRenderer"};
    std::string engineName{"NewbieRenderer"};
};

[[nodiscard]] ViewerCommandLineOptions parseViewerCommandLine(std::span<char*> args);
void printViewerUsage(std::string_view executableName = "main");
[[nodiscard]] int runViewer(ViewerRunConfig config);
[[nodiscard]] int runViewerFromCommandLine(std::span<char*> args);
} // namespace nr::pipeline
