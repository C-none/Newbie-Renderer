module nr.app;

import :camera;
import :session;
import :ui;
import nr.options;
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
    if (auto abandoned = options_.shutdown(); abandoned.has_value())
    {
        auto const snapshot = options_.snapshot();
        nr::options::emitMachineRecord<nr::LogLevel::warning>(
            nr::options::OptionMachineRecord{
                .sequence = abandoned->sequence,
                .id = abandoned->id,
                .phase = nr::options::OptionLogPhase::terminal,
                .status = nr::options::OptionLogStatus::abandoned,
                .frameIndex = snapshot != nullptr ? snapshot->frameIndex : 0u,
                .origin = abandoned->origin,
                .requestId = abandoned->requestId,
                .reason = "shutdown",
            });
    }

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
    if (!scene_)
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

std::unique_ptr<nr::scene::Scene> AppSession::makeSceneCandidate(
    const SceneSessionCreateInfo& createInfo)
{
    nrAssert(renderer_.initialized(), "AppSession::makeSceneCandidate requires initialize() first.");

    return std::make_unique<nr::scene::Scene>(nr::scene::SceneCreateInfo{
        .device = renderer_.device(),
        .uploadBudgetBytesPerFrame = createInfo.uploadBudgetBytesPerFrame,
        .cpuRetention = createInfo.cpuRetention,
    });
}

void AppSession::commitScene(std::unique_ptr<nr::scene::Scene> candidate)
{
    nrAssert(renderer_.initialized(), "AppSession::commitScene requires initialize() first.");
    nrAssert(candidate != nullptr, "AppSession::commitScene requires a valid candidate.");
    nrAssert(
        &candidate->device() == &renderer_.device(),
        "AppSession::commitScene candidate belongs to a different renderer device.");

    renderer_.device().waitIdle();
    renderer_.resetSceneBinding();
    scene_.swap(candidate);
    candidate.reset();
}

nr::scene::Scene& AppSession::createScene(const SceneSessionCreateInfo& createInfo)
{
    auto candidate = makeSceneCandidate(createInfo);
    commitScene(std::move(candidate));
    return *scene_;
}

bool AppSession::initialized() const noexcept
{
    return renderer_.initialized();
}

bool AppSession::hasScene() const noexcept
{
    return scene_ != nullptr;
}

void AppSession::resetCameraFromSceneOrDefault(const AppCameraDefaultView& defaults)
{
    nrAssert(renderer_.initialized(),
             "AppSession::resetCameraFromSceneOrDefault requires initialize() first.");

    if (scene_)
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

nr::options::OptionSystem& AppSession::options() noexcept
{
    return options_;
}

const nr::options::OptionSystem& AppSession::options() const noexcept
{
    return options_;
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
    if (renderer_.initialized())
    {
        services.set(std::ref(renderer_.device().presentationContext));
    }
    return services;
}

nr::scene::Scene& AppSession::scene() noexcept
{
    nrAssert(scene_ != nullptr, "AppSession::scene requires an active scene.");
    return *scene_;
}

const nr::scene::Scene& AppSession::scene() const noexcept
{
    nrAssert(scene_ != nullptr, "AppSession::scene requires an active scene.");
    return *scene_;
}

std::optional<std::reference_wrapper<nr::scene::Scene>> AppSession::tryScene() noexcept
{
    if (!scene_)
    {
        return std::nullopt;
    }

    return std::ref(*scene_);
}

std::optional<std::reference_wrapper<const nr::scene::Scene>> AppSession::tryScene() const noexcept
{
    if (!scene_)
    {
        return std::nullopt;
    }

    return std::cref(*scene_);
}
} // namespace nr::app
