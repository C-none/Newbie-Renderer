module nr.app;
import dependency.math;

import :camera;
import :ui;
import nr.options;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.utils;
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

[[nodiscard]] bool finiteVec3(const DirectX::XMFLOAT3 &value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finiteMat4(const DirectX::XMFLOAT4X4 &value) noexcept
{
    return std::isfinite(value._11) && std::isfinite(value._12) && std::isfinite(value._13) &&
           std::isfinite(value._14) && std::isfinite(value._21) && std::isfinite(value._22) &&
           std::isfinite(value._23) && std::isfinite(value._24) && std::isfinite(value._31) &&
           std::isfinite(value._32) && std::isfinite(value._33) && std::isfinite(value._34) &&
           std::isfinite(value._41) && std::isfinite(value._42) && std::isfinite(value._43) &&
           std::isfinite(value._44);
}

[[nodiscard]] DirectX::XMUINT2 viewportExtentFrom(const nr::rhi::PresentationContext &presentation) noexcept
{
    auto const extent = presentation.swapchainExtent();
    if (extent.width == 0u || extent.height == 0u)
    {
        return DirectX::XMUINT2{1u, 1u};
    }

    return DirectX::XMUINT2{extent.width, extent.height};
}

[[nodiscard]] DirectX::XMFLOAT3 cameraForwardFromWorld(const DirectX::XMFLOAT4X4 &world) noexcept
{
    auto const forward = DirectX::XMFLOAT3{-world._31, -world._32, -world._33};
    auto const length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (!std::isfinite(length) || length <= 1e-6f)
    {
        return DirectX::XMFLOAT3{0.0f, 0.0f, -1.0f};
    }

    return DirectX::XMFLOAT3{forward.x / length, forward.y / length, forward.z / length};
}

[[nodiscard]] nr::renderer::ViewerPerspectiveLens sanitizeLens(const nr::renderer::ViewerPerspectiveLens &lens) noexcept
{
    auto result = lens;
    if (!std::isfinite(result.verticalFovRadians) || result.verticalFovRadians <= 1e-3f ||
        result.verticalFovRadians >= (nr::math::pi - 1e-3f))
    {
        result.verticalFovRadians = nr::math::radians(60.0f);
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
    const nr::scene::Scene &scene, const nr::scene::SceneResolvedCamera &resolvedCamera,
    const nr::renderer::ViewerPerspectiveLens &fallback) noexcept
{
    auto lens = sanitizeLens(fallback);

    auto const cameraRecord = scene.tryGetCameraAsset(resolvedCamera.camera);
    if (!cameraRecord.has_value() || !cameraRecord->get().cpu.perspective())
    {
        return lens;
    }

    auto const &cameraAsset = cameraRecord->get().cpu;
    lens.verticalFovRadians = cameraAsset.verticalFovRadians;
    lens.nearPlane = cameraAsset.nearPlane;
    lens.farPlane = cameraAsset.farPlane;
    return sanitizeLens(lens);
}

[[nodiscard]] nr::renderer::ViewerCameraControlInput sampleControlInput(
    const nr::rhi::PresentationContext &presentation, float deltaSeconds,
    nr::app::AppCameraCursorTrackingState &cursorTracking, const nr::app::UiCaptureState &captureState) noexcept
{
    auto const &input = presentation.windowInput();
    auto const cursor = input.cursorPosition();
    auto const rotateActive = !captureState.wantsMouse && (input.mouseButtonDown(kMouseButtonLeft) ||
                                                           input.mouseButtonDown(kMouseButtonRight));

    auto cursorDelta = DirectX::XMFLOAT2{0.0f, 0.0f};
    if (rotateActive)
    {
        if (cursorTracking.hasPrevious)
        {
            cursorDelta = DirectX::XMFLOAT2{
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
        .moveForward = !captureState.wantsKeyboard && input.keyDown(kKeyW),
        .moveBackward = !captureState.wantsKeyboard && input.keyDown(kKeyS),
        .moveLeft = !captureState.wantsKeyboard && input.keyDown(kKeyA),
        .moveRight = !captureState.wantsKeyboard && input.keyDown(kKeyD),
        .moveUp = !captureState.wantsKeyboard && input.keyDown(kKeyE),
        .moveDown = !captureState.wantsKeyboard && input.keyDown(kKeyQ),
        .rotateActive = rotateActive,
        .cursorDelta = cursorDelta,
    };
}

[[nodiscard]] float normalizedYawRadians(float yawRadians) noexcept
{
    auto const turn = nr::math::twoPi;
    auto normalized = std::fmod(yawRadians + nr::math::pi, turn);
    if (normalized < 0.0f)
    {
        normalized += turn;
    }
    return normalized - nr::math::pi;
}

[[nodiscard]] nr::options::OptionWireValue::Object poseValue(const nr::renderer::ViewerCameraPose &pose)
{
    return {
        {"position",
         nr::options::OptionWireValue::Array{
             static_cast<double>(pose.position.x),
             static_cast<double>(pose.position.y),
             static_cast<double>(pose.position.z),
         }},
        {"yaw_degrees", static_cast<double>(nr::math::degrees(normalizedYawRadians(pose.yawRadians)))},
        {"pitch_degrees",
         static_cast<double>(nr::math::degrees(std::clamp(pose.pitchRadians, -nr::math::radians(89.0f),
                                                          nr::math::radians(89.0f))))},
    };
}

[[nodiscard]] nr::options::OptionWireValue::Object clipValue(const nr::renderer::ViewerPerspectiveLens &lens)
{
    return {
        {"near", static_cast<double>(lens.nearPlane)},
        {"far", static_cast<double>(lens.farPlane)},
    };
}

[[nodiscard]] const nr::options::OptionWireValue &requiredValue(const nr::options::OptionFrameSnapshot &snapshot,
                                                                std::string_view id) noexcept
{
    auto const *value = snapshot.findValue(id);
    nrAssert(value != nullptr, "Camera snapshot is missing required option '{}'.", id);
    return *value;
}

[[nodiscard]] nr::renderer::ViewerCameraPose poseFromSnapshot(const nr::options::OptionFrameSnapshot &snapshot) noexcept
{
    auto const &wire = requiredValue(snapshot, nr::options::keys::viewerCameraPose.id());
    auto const *object = std::get_if<nr::options::OptionWireValue::Object>(&wire.storage);
    nrAssert(object != nullptr, "viewer.camera.pose must be an object.");
    auto const *position = std::get_if<nr::options::OptionWireValue::Array>(&object->at("position").storage);
    nrAssert(position != nullptr && position->size() == 3u, "viewer.camera.pose.position must contain three numbers.");

    auto numberAt = [](const nr::options::OptionWireValue &value) noexcept {
        auto const *number = std::get_if<double>(&value.storage);
        nrAssert(number != nullptr, "Camera snapshot number has an invalid wire type.");
        return static_cast<float>(*number);
    };
    return nr::renderer::ViewerCameraPose{
        .position =
            DirectX::XMFLOAT3{
                numberAt((*position)[0]),
                numberAt((*position)[1]),
                numberAt((*position)[2]),
            },
        .yawRadians = nr::math::radians(numberAt(object->at("yaw_degrees"))),
        .pitchRadians = nr::math::radians(numberAt(object->at("pitch_degrees"))),
    };
}

[[nodiscard]] nr::renderer::ViewerPerspectiveLens lensFromSnapshot(
    const nr::options::OptionFrameSnapshot &snapshot) noexcept
{
    auto const *fov = snapshot.find(nr::options::keys::viewerCameraVerticalFovDegrees);
    nrAssert(fov != nullptr, "Camera snapshot is missing viewer.camera.vertical_fov_degrees.");
    auto const &clipWire = requiredValue(snapshot, nr::options::keys::viewerCameraClipPlanes.id());
    auto const *clip = std::get_if<nr::options::OptionWireValue::Object>(&clipWire.storage);
    nrAssert(clip != nullptr, "viewer.camera.clip_planes must be an object.");
    auto const *nearPlane = std::get_if<double>(&clip->at("near").storage);
    auto const *farPlane = std::get_if<double>(&clip->at("far").storage);
    nrAssert(nearPlane != nullptr && farPlane != nullptr, "viewer.camera.clip_planes fields must be numbers.");
    return nr::renderer::ViewerPerspectiveLens{
        .verticalFovRadians = nr::math::radians(static_cast<float>(*fov)),
        .nearPlane = static_cast<float>(*nearPlane),
        .farPlane = static_cast<float>(*farPlane),
    };
}

[[nodiscard]] float movementSpeedFromSnapshot(const nr::options::OptionFrameSnapshot &snapshot) noexcept
{
    auto const &wire = requiredValue(snapshot, nr::options::keys::viewerCameraMovementSpeed.id());
    auto const *movementSpeed = std::get_if<double>(&wire.storage);
    nrAssert(movementSpeed != nullptr, "viewer.camera.movement_speed must be a number.");
    nrAssert(std::isfinite(*movementSpeed) && *movementSpeed >= 0.01 && *movementSpeed <= 1000.0,
             "viewer.camera.movement_speed must be finite and within [0.01, 1000].");
    return static_cast<float>(*movementSpeed);
}

[[nodiscard]] bool hasCameraInput(const nr::renderer::ViewerCameraControlInput &input) noexcept
{
    auto const moves =
        input.moveForward || input.moveBackward || input.moveLeft || input.moveRight || input.moveUp || input.moveDown;
    auto const rotates =
        input.rotateActive && (std::abs(input.cursorDelta.x) > 0.0f || std::abs(input.cursorDelta.y) > 0.0f);
    return moves || rotates;
}
} // namespace nr::app::detail

namespace nr::app
{
void AppCamera::initializeDefault(const nr::rhi::PresentationContext &presentation,
                                  const AppCameraDefaultView &defaults) noexcept
{
    auto position = detail::finiteVec3(defaults.position) ? defaults.position : DirectX::XMFLOAT3{0.0f, 0.0f, 3.0f};
    auto target = detail::finiteVec3(defaults.target) ? defaults.target : DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f};
    auto const targetDelta = DirectX::XMFLOAT3{target.x - position.x, target.y - position.y, target.z - position.z};
    if (std::sqrt(targetDelta.x * targetDelta.x + targetDelta.y * targetDelta.y + targetDelta.z * targetDelta.z) <= 1e-6f)
    {
        target = DirectX::XMFLOAT3{position.x, position.y, position.z - 1.0f};
    }

    viewer_.setViewportExtent(detail::viewportExtentFrom(presentation));
    viewer_.setLens(detail::sanitizeLens(defaults.lens));
    viewer_.setPoseFromLookAt(position, target);
    cursorTracking_ = {};
}

void AppCamera::initializeFromSceneOrDefault(const nr::scene::Scene &scene,
                                             const nr::rhi::PresentationContext &presentation,
                                             const AppCameraDefaultView &defaults) noexcept
{
    auto const viewportExtent = detail::viewportExtentFrom(presentation);
    if (auto primaryCamera = scene.tryGetPrimaryCamera(std::optional<DirectX::XMUINT2>{viewportExtent});
        primaryCamera.has_value() && !primaryCamera->fallback && detail::finiteMat4(primaryCamera->world))
    {
        auto const position = DirectX::XMFLOAT3{primaryCamera->world._41, primaryCamera->world._42,
                                                primaryCamera->world._43};
        auto const forward = detail::cameraForwardFromWorld(primaryCamera->world);

        if (detail::finiteVec3(position))
        {
            viewer_.setViewportExtent(viewportExtent);
            viewer_.setLens(detail::lensFromSceneCamera(scene, *primaryCamera, defaults.lens));
            viewer_.setPoseFromLookAt(position, DirectX::XMFLOAT3{position.x + forward.x, position.y + forward.y,
                                                                   position.z + forward.z});
            cursorTracking_ = {};
            return;
        }
    }

    initializeDefault(presentation, defaults);
}

void AppCamera::syncViewportExtent(const nr::rhi::PresentationContext &presentation) noexcept
{
    viewer_.setViewportExtent(detail::viewportExtentFrom(presentation));
}

void AppCamera::syncFromSnapshot(const nr::options::OptionFrameSnapshot &snapshot,
                                 const nr::rhi::PresentationContext &presentation) noexcept
{
    syncViewportExtent(presentation);
    viewer_.setPose(detail::poseFromSnapshot(snapshot));
    viewer_.setLens(detail::lensFromSnapshot(snapshot));
    auto controlConfig = viewer_.controlConfig();
    controlConfig.movementSpeed = detail::movementSpeedFromSnapshot(snapshot);
    viewer_.setControlConfig(controlConfig);
}

bool AppCamera::tryScheduleFromPresentation(nr::options::OptionSystem &options,
                                            const nr::options::OptionFrameSnapshot &snapshot,
                                            const nr::rhi::PresentationContext &presentation, float deltaSeconds,
                                            const UiCaptureState &captureState) noexcept
{
    auto const control = detail::sampleControlInput(presentation, deltaSeconds, cursorTracking_, captureState);
    if (!detail::hasCameraInput(control))
    {
        return false;
    }

    auto candidate = nr::renderer::ViewerPerspectiveCamera{};
    candidate.setViewportExtent(viewer_.viewportExtent());
    candidate.setControlConfig(viewer_.controlConfig());
    candidate.setLens(detail::lensFromSnapshot(snapshot));
    candidate.setPose(detail::poseFromSnapshot(snapshot));
    candidate.applyControl(control);

    auto result = options.trySchedule(nr::options::OptionMutationRequest{
        .id = nr::options::optionId(nr::options::keys::viewerCameraPose),
        .value = detail::poseValue(candidate.pose()),
        .binding = nr::options::BindingProof{.bindingEpoch = snapshot.bindingEpoch},
        .origin = nr::options::MutationOrigin::camera,
    });
    return result.started;
}

void AppCamera::discardPresentationInput(const nr::rhi::PresentationContext &presentation, float deltaSeconds,
                                         const UiCaptureState &captureState) noexcept
{
    static_cast<void>(detail::sampleControlInput(presentation, deltaSeconds, cursorTracking_, captureState));
}

nr::options::CameraResetValues AppCamera::optionResetValues() const
{
    return nr::options::CameraResetValues{
        .pose = detail::poseValue(viewer_.pose()),
        .verticalFovDegrees = static_cast<std::uint64_t>(
            std::clamp(std::lround(nr::math::degrees(viewer_.lens().verticalFovRadians)), 1l, 179l)),
        .clipPlanes = detail::clipValue(viewer_.lens()),
    };
}

const nr::renderer::ViewerPerspectiveCamera &AppCamera::viewer() const noexcept
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
