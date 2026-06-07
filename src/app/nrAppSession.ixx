export module nr.app:session;

import nr.renderer;
import nr.scene;
import nr.utils;
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

    ~AppSession()
    {
        shutdown();
    }

    void initialize(const nr::renderer::RendererCreateInfo& createInfo = {})
    {
        renderer_.initialize(createInfo);
        ui_.initialize();
        camera_.initializeDefault(renderer_.device().presentationContext);
    }

    void shutdown()
    {
        if (!renderer_.initialized())
        {
            scene_.reset();
            return;
        }

        destroyScene();
        ui_.shutdown();
        renderer_.shutdown();
    }

    void destroyScene()
    {
        if (!scene_.has_value())
        {
            return;
        }

        if (renderer_.initialized())
        {
            renderer_.device().waitIdle();
        }

        scene_.reset();
    }

    [[nodiscard]] nr::scene::Scene& createScene(const SceneSessionCreateInfo& createInfo = {})
    {
        nrAssert(renderer_.initialized(), "AppSession::createScene requires initialize() first.");

        destroyScene();
        scene_.emplace(nr::scene::SceneCreateInfo{
            .device = renderer_.device(),
            .uploadBudgetBytesPerFrame = createInfo.uploadBudgetBytesPerFrame,
            .cpuRetention = createInfo.cpuRetention,
        });
        return *scene_;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return renderer_.initialized();
    }

    [[nodiscard]] bool hasScene() const noexcept
    {
        return scene_.has_value();
    }

    void resetCameraFromSceneOrDefault(const AppCameraDefaultView& defaults = {})
    {
        nrAssert(renderer_.initialized(),
                 "AppSession::resetCameraFromSceneOrDefault requires initialize() first.");

        if (scene_.has_value())
        {
            camera_.initializeFromSceneOrDefault(*scene_, renderer_.device().presentationContext, defaults);
            return;
        }

        camera_.initializeDefault(renderer_.device().presentationContext, defaults);
    }

    [[nodiscard]] nr::renderer::Renderer& renderer() noexcept
    {
        return renderer_;
    }

    [[nodiscard]] const nr::renderer::Renderer& renderer() const noexcept
    {
        return renderer_;
    }

    [[nodiscard]] AppCamera& camera() noexcept
    {
        return camera_;
    }

    [[nodiscard]] const AppCamera& camera() const noexcept
    {
        return camera_;
    }

    [[nodiscard]] UiSystem& ui() noexcept
    {
        return ui_;
    }

    [[nodiscard]] const UiSystem& ui() const noexcept
    {
        return ui_;
    }

    [[nodiscard]] nr::renderer::FrameServices makeFrameServices() noexcept
    {
        auto services = nr::renderer::FrameServices{};
        services.set(std::ref(ui_));
        return services;
    }

    [[nodiscard]] nr::scene::Scene& scene() noexcept
    {
        nrAssert(scene_.has_value(), "AppSession::scene requires an active scene.");
        return *scene_;
    }

    [[nodiscard]] const nr::scene::Scene& scene() const noexcept
    {
        nrAssert(scene_.has_value(), "AppSession::scene requires an active scene.");
        return *scene_;
    }

    [[nodiscard]] std::optional<std::reference_wrapper<nr::scene::Scene>> tryScene() noexcept
    {
        if (!scene_.has_value())
        {
            return std::nullopt;
        }

        return std::ref(*scene_);
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const nr::scene::Scene>> tryScene() const noexcept
    {
        if (!scene_.has_value())
        {
            return std::nullopt;
        }

        return std::cref(*scene_);
    }

  private:
    // Declaration order matters: scene_ and ui_ must be destroyed before renderer_.
    nr::renderer::Renderer renderer_{};
    AppCamera camera_{};
    UiSystem ui_{};
    std::optional<nr::scene::Scene> scene_{};
};
} // namespace nr::app
