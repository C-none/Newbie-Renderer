module nr.app;
import dependency.math;

import :camera;
import :ui;
import nr.renderer;
import nr.rhi;
import nr.scene;
import std;

namespace nr::app::detail
{
inline constexpr int kMouseButtonLeft = 0;
inline constexpr int kMouseButtonRight = 1;
inline constexpr int kKeyW = 'W';
inline constexpr int kKeyS = 'S';
inline constexpr int kKeyA = 'A';
inline constexpr int kKeyD = 'D';
inline constexpr int kKeyQ = 'Q';
inline constexpr int kKeyE = 'E';

[[nodiscard]] float sanitizeCameraDeltaSeconds(float deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
    {
        return 1.0f / 60.0f;
    }

    return std::clamp(deltaSeconds, 1.0f / 240.0f, 0.1f);
}

[[nodiscard]] bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finiteVec4(const glm::vec4& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool finiteMat4(const glm::mat4& value) noexcept
{
    return finiteVec4(value[0]) && finiteVec4(value[1]) && finiteVec4(value[2]) && finiteVec4(value[3]);
}

[[nodiscard]] glm::uvec2 viewportExtentFrom(const nr::rhi::PresentationContext& presentation) noexcept
{
    auto const extent = presentation.swapchainExtent();
    if (extent.width == 0u || extent.height == 0u)
    {
        return glm::uvec2{1u, 1u};
    }

    return glm::uvec2{extent.width, extent.height};
}

[[nodiscard]] glm::vec3 cameraForwardFromWorld(const glm::mat4& world) noexcept
{
    auto const forward = glm::vec3{-world[2].x, -world[2].y, -world[2].z};
    auto const length = glm::length(forward);
    if (!std::isfinite(length) || length <= 1e-6f)
    {
        return glm::vec3{0.0f, 0.0f, -1.0f};
    }

    return forward / length;
}

[[nodiscard]] nr::renderer::ViewerPerspectiveLens sanitizeLens(const nr::renderer::ViewerPerspectiveLens& lens) noexcept
{
    auto result = lens;
    if (!std::isfinite(result.verticalFovRadians) || result.verticalFovRadians <= 1e-3f ||
        result.verticalFovRadians >= (glm::pi<float>() - 1e-3f))
    {
        result.verticalFovRadians = glm::radians(60.0f);
    }

    if (!std::isfinite(result.nearPlane) || result.nearPlane <= 1e-3f)
    {
        result.nearPlane = 0.1f;
    }

    if (!std::isfinite(result.farPlane) || result.farPlane <= result.nearPlane + 1e-3f)
    {
        result.farPlane = result.nearPlane + 1000.0f;
    }

    return result;
}

[[nodiscard]] nr::renderer::ViewerPerspectiveLens lensFromSceneCamera(
    const nr::scene::Scene& scene,
    const nr::scene::SceneResolvedCamera& resolvedCamera,
    const nr::renderer::ViewerPerspectiveLens& fallback) noexcept
{
    auto lens = sanitizeLens(fallback);

    auto const cameraRecord = scene.tryGetCameraAsset(resolvedCamera.camera);
    if (!cameraRecord.has_value() || !cameraRecord->get().cpu.perspective())
    {
        return lens;
    }

    auto const& cameraAsset = cameraRecord->get().cpu;
    lens.verticalFovRadians = cameraAsset.verticalFovRadians;
    lens.nearPlane = cameraAsset.nearPlane;
    lens.farPlane = cameraAsset.farPlane;
    return sanitizeLens(lens);
}

[[nodiscard]] nr::renderer::ViewerCameraControlInput sampleControlInput(
    const nr::rhi::PresentationContext& presentation,
    float deltaSeconds,
    nr::app::AppCameraCursorTrackingState& cursorTracking,
    const nr::app::UiCaptureState& captureState) noexcept
{
    auto const cursor = presentation.cursorPosition();
    auto const rotateActive = !captureState.wantsMouse &&
                              (presentation.mouseButtonDown(kMouseButtonLeft) ||
                               presentation.mouseButtonDown(kMouseButtonRight));

    auto cursorDelta = glm::vec2{0.0f, 0.0f};
    if (rotateActive)
    {
        if (cursorTracking.hasPrevious)
        {
            cursorDelta = glm::vec2{
                static_cast<float>(cursor.x - cursorTracking.previous.x),
                static_cast<float>(cursor.y - cursorTracking.previous.y),
            };
        }

        cursorTracking.previous = cursor;
        cursorTracking.hasPrevious = true;
    }
    else
    {
        cursorTracking.previous = cursor;
        cursorTracking.hasPrevious = false;
    }

    return nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = sanitizeCameraDeltaSeconds(deltaSeconds),
        .moveForward = !captureState.wantsKeyboard && presentation.keyDown(kKeyW),
        .moveBackward = !captureState.wantsKeyboard && presentation.keyDown(kKeyS),
        .moveLeft = !captureState.wantsKeyboard && presentation.keyDown(kKeyA),
        .moveRight = !captureState.wantsKeyboard && presentation.keyDown(kKeyD),
        .moveUp = !captureState.wantsKeyboard && presentation.keyDown(kKeyE),
        .moveDown = !captureState.wantsKeyboard && presentation.keyDown(kKeyQ),
        .rotateActive = rotateActive,
        .cursorDelta = cursorDelta,
    };
}
} // namespace nr::app::detail

namespace nr::app
{
void AppCamera::initializeDefault(const nr::rhi::PresentationContext& presentation,
                                  const AppCameraDefaultView& defaults) noexcept
{
    auto position = detail::finiteVec3(defaults.position) ? defaults.position : glm::vec3{0.0f, 0.0f, 3.0f};
    auto target = detail::finiteVec3(defaults.target) ? defaults.target : glm::vec3{0.0f, 0.0f, 0.0f};
    if (glm::length(target - position) <= 1e-6f)
    {
        target = position + glm::vec3{0.0f, 0.0f, -1.0f};
    }

    viewer_.setViewportExtent(detail::viewportExtentFrom(presentation));
    viewer_.setLens(detail::sanitizeLens(defaults.lens));
    viewer_.setPoseFromLookAt(position, target);
    cursorTracking_ = {};
}

void AppCamera::initializeFromSceneOrDefault(const nr::scene::Scene& scene,
                                             const nr::rhi::PresentationContext& presentation,
                                             const AppCameraDefaultView& defaults) noexcept
{
    auto const viewportExtent = detail::viewportExtentFrom(presentation);
    if (auto primaryCamera = scene.tryGetPrimaryCamera(std::optional<glm::uvec2>{viewportExtent});
        primaryCamera.has_value() && !primaryCamera->fallback && detail::finiteMat4(primaryCamera->world))
    {
        auto const translation = primaryCamera->world[3];
        auto const position = glm::vec3{translation.x, translation.y, translation.z};
        auto const forward = detail::cameraForwardFromWorld(primaryCamera->world);

        if (detail::finiteVec3(position))
        {
            viewer_.setViewportExtent(viewportExtent);
            viewer_.setLens(detail::lensFromSceneCamera(scene, *primaryCamera, defaults.lens));
            viewer_.setPoseFromLookAt(position, position + forward);
            cursorTracking_ = {};
            return;
        }
    }

    initializeDefault(presentation, defaults);
}

void AppCamera::syncViewportExtent(const nr::rhi::PresentationContext& presentation) noexcept
{
    viewer_.setViewportExtent(detail::viewportExtentFrom(presentation));
}

void AppCamera::updateFromPresentation(const nr::rhi::PresentationContext& presentation,
                                       float deltaSeconds,
                                       const UiCaptureState& captureState) noexcept
{
    syncViewportExtent(presentation);
    viewer_.applyControl(detail::sampleControlInput(presentation, deltaSeconds, cursorTracking_, captureState));
}

nr::renderer::ViewerPerspectiveCamera& AppCamera::viewer() noexcept
{
    return viewer_;
}

const nr::renderer::ViewerPerspectiveCamera& AppCamera::viewer() const noexcept
{
    return viewer_;
}

nr::renderer::ViewerPerspectiveCameraFrame AppCamera::frame() const noexcept
{
    return viewer_.frame();
}

nr::renderer::RendererCameraOverride AppCamera::buildRendererCameraOverride() const noexcept
{
    return viewer_.buildRendererCameraOverride();
}
} // namespace nr::app
