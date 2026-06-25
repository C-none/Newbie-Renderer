export module nr.app:session;

import nr.renderer;
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
    AppSession(const AppSession&) = delete;
    AppSession& operator=(const AppSession&) = delete;
    AppSession(AppSession&&) = delete;
    AppSession& operator=(AppSession&&) = delete;

    ~AppSession();

    void initialize(const nr::renderer::RendererCreateInfo& createInfo = {});
    void shutdown();
    void destroyScene();
    [[nodiscard]] nr::scene::Scene& createScene(const SceneSessionCreateInfo& createInfo = {});
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool hasScene() const noexcept;
    void resetCameraFromSceneOrDefault(const AppCameraDefaultView& defaults = {});

    [[nodiscard]] nr::renderer::Renderer& renderer() noexcept;
    [[nodiscard]] const nr::renderer::Renderer& renderer() const noexcept;
    [[nodiscard]] AppCamera& camera() noexcept;
    [[nodiscard]] const AppCamera& camera() const noexcept;
    [[nodiscard]] UiSystem& ui() noexcept;
    [[nodiscard]] const UiSystem& ui() const noexcept;
    [[nodiscard]] nr::renderer::FrameServices makeFrameServices() noexcept;
    [[nodiscard]] nr::scene::Scene& scene() noexcept;
    [[nodiscard]] const nr::scene::Scene& scene() const noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<nr::scene::Scene>> tryScene() noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<const nr::scene::Scene>> tryScene() const noexcept;

  private:
    // Declaration order matters: scene_ and ui_ must be destroyed before renderer_.
    nr::renderer::Renderer renderer_{};
    AppCamera camera_{};
    UiSystem ui_{};
    std::optional<nr::scene::Scene> scene_{};
};
} // namespace nr::app
