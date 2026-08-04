export module nr.app:session;

import nr.renderer;
import nr.options;
import nr.scene;
import std;
import :camera;
import :ui;

export namespace nr::app
{
struct SceneSessionCreateInfo
{
    std::size_t uploadBudgetBytesPerFrame = 128ull * 1024ull * 1024ull;
    nr::scene::CpuRetentionPolicy cpuRetention = nr::scene::CpuRetentionPolicy::keepAll;
};

class AppSession
{
  public:
    AppSession() = default;
    AppSession(const AppSession &) = delete;
    AppSession &operator=(const AppSession &) = delete;
    AppSession(AppSession &&) = delete;
    AppSession &operator=(AppSession &&) = delete;

    ~AppSession();

    void initialize(const nr::renderer::RendererCreateInfo &createInfo = {});
    void shutdown();
    void destroyScene();
    [[nodiscard]] std::unique_ptr<nr::scene::Scene> makeSceneCandidate(const SceneSessionCreateInfo &createInfo = {});
    void commitScene(std::unique_ptr<nr::scene::Scene> candidate);
    [[nodiscard]] nr::scene::Scene &createScene(const SceneSessionCreateInfo &createInfo = {});
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool hasScene() const noexcept;
    void resetCameraFromSceneOrDefault(const AppCameraDefaultView &defaults = {});

    [[nodiscard]] nr::renderer::Renderer &renderer() noexcept;
    [[nodiscard]] const nr::renderer::Renderer &renderer() const noexcept;
    [[nodiscard]] nr::options::OptionSystem &options() noexcept;
    [[nodiscard]] const nr::options::OptionSystem &options() const noexcept;
    [[nodiscard]] AppCamera &camera() noexcept;
    [[nodiscard]] const AppCamera &camera() const noexcept;
    [[nodiscard]] UiSystem &ui() noexcept;
    [[nodiscard]] const UiSystem &ui() const noexcept;
    [[nodiscard]] nr::renderer::FrameServices makeFrameServices() noexcept;
    [[nodiscard]] nr::scene::Scene &scene() noexcept;
    [[nodiscard]] const nr::scene::Scene &scene() const noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<nr::scene::Scene>> tryScene() noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<const nr::scene::Scene>> tryScene() const noexcept;

  private:
    // Declaration order matters: scene_, ui_, and renderer_ must be destroyed before options_.
    nr::options::OptionSystem options_{};
    nr::renderer::Renderer renderer_{};
    AppCamera camera_{};
    UiSystem ui_{};
    std::unique_ptr<nr::scene::Scene> scene_{};
};
} // namespace nr::app
