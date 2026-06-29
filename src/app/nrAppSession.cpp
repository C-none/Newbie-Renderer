module nr.app;

import :camera;
import :session;
import :ui;
import nr.renderer;
import nr.scene;
import nr.utils;
import std;

namespace nr::app
{
AppSession::~AppSession()
{
    shutdown();
}

void AppSession::initialize(const nr::renderer::RendererCreateInfo& createInfo)
{
    renderer_.initialize(createInfo);
    ui_.initialize();
    camera_.initializeDefault(renderer_.device().presentationContext);
}

void AppSession::shutdown()
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

void AppSession::destroyScene()
{
    if (!scene_.has_value())
    {
        return;
    }

    if (renderer_.initialized())
    {
        renderer_.device().waitIdle();
        renderer_.resetSceneBinding();
    }

    scene_.reset();
}

nr::scene::Scene& AppSession::createScene(const SceneSessionCreateInfo& createInfo)
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

bool AppSession::initialized() const noexcept
{
    return renderer_.initialized();
}

bool AppSession::hasScene() const noexcept
{
    return scene_.has_value();
}

void AppSession::resetCameraFromSceneOrDefault(const AppCameraDefaultView& defaults)
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

nr::renderer::Renderer& AppSession::renderer() noexcept
{
    return renderer_;
}

const nr::renderer::Renderer& AppSession::renderer() const noexcept
{
    return renderer_;
}

AppCamera& AppSession::camera() noexcept
{
    return camera_;
}

const AppCamera& AppSession::camera() const noexcept
{
    return camera_;
}

UiSystem& AppSession::ui() noexcept
{
    return ui_;
}

const UiSystem& AppSession::ui() const noexcept
{
    return ui_;
}

nr::renderer::FrameServices AppSession::makeFrameServices() noexcept
{
    auto services = nr::renderer::FrameServices{};
    services.set(std::ref(ui_));
    return services;
}

nr::scene::Scene& AppSession::scene() noexcept
{
    nrAssert(scene_.has_value(), "AppSession::scene requires an active scene.");
    return *scene_;
}

const nr::scene::Scene& AppSession::scene() const noexcept
{
    nrAssert(scene_.has_value(), "AppSession::scene requires an active scene.");
    return *scene_;
}

std::optional<std::reference_wrapper<nr::scene::Scene>> AppSession::tryScene() noexcept
{
    if (!scene_.has_value())
    {
        return std::nullopt;
    }

    return std::ref(*scene_);
}

std::optional<std::reference_wrapper<const nr::scene::Scene>> AppSession::tryScene() const noexcept
{
    if (!scene_.has_value())
    {
        return std::nullopt;
    }

    return std::cref(*scene_);
}
} // namespace nr::app
