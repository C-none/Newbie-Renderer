module nr.renderer;
import :viewerCamera;
import dependency.math;
import nr.scene;
import std;
import :renderer;

namespace
{
[[nodiscard]] float clampPositive(float value, float fallback) noexcept
{
    if (std::isfinite(value) && value > 1e-6f)
    {
        return value;
    }
    return fallback;
}

[[nodiscard]] glm::vec3 forwardFromYawPitch(float yawRadians, float pitchRadians)
{
    auto const cosPitch = std::cos(pitchRadians);
    auto forward = glm::vec3{
        std::cos(yawRadians) * cosPitch,
        std::sin(pitchRadians),
        std::sin(yawRadians) * cosPitch,
    };

    auto const length = glm::length(forward);
    if (length <= 1e-6f || !std::isfinite(length))
    {
        return glm::vec3{0.0f, 0.0f, -1.0f};
    }

    return forward / length;
}

[[nodiscard]] nr::scene::SceneFrustum frustumFromViewProjection(const glm::mat4 &viewProjection)
{
    auto frustum = nr::scene::SceneFrustum{};
    auto const transposed = glm::transpose(viewProjection);
    auto const rawPlanes = std::array{
        transposed[3] + transposed[0], transposed[3] - transposed[0], transposed[3] + transposed[1],
        transposed[3] - transposed[1], transposed[3] + transposed[2], transposed[3] - transposed[2],
    };

    auto planeIndices = std::views::iota(std::size_t{0}, rawPlanes.size());
    std::ranges::for_each(planeIndices, [&](std::size_t planeIndex) {
        auto plane = rawPlanes[planeIndex];
        auto const normal = glm::vec3{plane.x, plane.y, plane.z};
        auto const length = glm::length(normal);
        if (std::isfinite(length) && length > 1e-6f)
        {
            plane /= length;
        }
        frustum.planes[planeIndex] = plane;
    });

    return frustum;
}
} // namespace

namespace nr::renderer
{
void ViewerPerspectiveCamera::setViewportExtent(glm::uvec2 extent) noexcept
{
    if (extent.x == 0u || extent.y == 0u)
    {
        viewportExtent_ = glm::uvec2{1u, 1u};
        return;
    }
    viewportExtent_ = extent;
}

[[nodiscard]] glm::uvec2 ViewerPerspectiveCamera::viewportExtent() const noexcept
{
    return viewportExtent_;
}

void ViewerPerspectiveCamera::setLens(const ViewerPerspectiveLens &lens) noexcept
{
    lens_ = lens;
}

[[nodiscard]] const ViewerPerspectiveLens &ViewerPerspectiveCamera::lens() const noexcept
{
    return lens_;
}

void ViewerPerspectiveCamera::setPose(const ViewerCameraPose &pose) noexcept
{
    pose_ = pose;
}

[[nodiscard]] const ViewerCameraPose &ViewerPerspectiveCamera::pose() const noexcept
{
    return pose_;
}

void ViewerPerspectiveCamera::setPoseFromLookAt(glm::vec3 position, glm::vec3 target, glm::vec3 worldUp) noexcept
{
    pose_.position = position;

    auto forward = target - position;
    auto const length = glm::length(forward);
    if (length <= 1e-6f || !std::isfinite(length))
    {
        pose_.yawRadians = -glm::half_pi<float>();
        pose_.pitchRadians = 0.0f;
        return;
    }

    forward /= length;
    pose_.yawRadians = std::atan2(forward.z, forward.x);
    pose_.pitchRadians = std::asin(std::clamp(forward.y, -1.0f, 1.0f));

    (void)worldUp;
}

void ViewerPerspectiveCamera::setControlConfig(const ViewerCameraControlConfig &config) noexcept
{
    controlConfig_ = config;
}

[[nodiscard]] const ViewerCameraControlConfig &ViewerPerspectiveCamera::controlConfig() const noexcept
{
    return controlConfig_;
}

void ViewerPerspectiveCamera::applyControl(const ViewerCameraControlInput &input) noexcept
{
    auto const dt = std::max(input.deltaSeconds, 0.0f);
    auto const moveSpeed = clampPositive(controlConfig_.movementSpeed, 3.5f);

    auto forward = forwardFromYawPitch(pose_.yawRadians, pose_.pitchRadians);
    auto right = glm::normalize(glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    if (!std::isfinite(glm::length(right)) || glm::length(right) <= 1e-6f)
    {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    }
    auto up = glm::normalize(glm::cross(right, forward));

    auto movement = glm::vec3{0.0f};
    if (input.moveForward)
    {
        movement += forward;
    }
    if (input.moveBackward)
    {
        movement -= forward;
    }
    if (input.moveRight)
    {
        movement += right;
    }
    if (input.moveLeft)
    {
        movement -= right;
    }
    if (input.moveUp)
    {
        movement += up;
    }
    if (input.moveDown)
    {
        movement -= up;
    }

    auto const movementLength = glm::length(movement);
    if (movementLength > 1e-6f && std::isfinite(movementLength))
    {
        pose_.position += (movement / movementLength) * moveSpeed * dt;
    }

    if (input.rotateActive)
    {
        auto const lookSpeed = clampPositive(controlConfig_.lookRadiansPerPixel, 0.0035f);
        pose_.yawRadians += input.cursorDelta.x * lookSpeed;
        pose_.pitchRadians -= input.cursorDelta.y * lookSpeed;

        auto const pitchLimit = clampPositive(controlConfig_.pitchLimitRadians, glm::radians(89.0f));
        pose_.pitchRadians = std::clamp(pose_.pitchRadians, -pitchLimit, pitchLimit);
    }
}

[[nodiscard]] ViewerPerspectiveCameraFrame ViewerPerspectiveCamera::frame() const noexcept
{
    auto result = ViewerPerspectiveCameraFrame{};
    result.position = pose_.position;
    result.forward = forwardFromYawPitch(pose_.yawRadians, pose_.pitchRadians);

    result.right = glm::normalize(glm::cross(result.forward, glm::vec3{0.0f, 1.0f, 0.0f}));
    if (!std::isfinite(glm::length(result.right)) || glm::length(result.right) <= 1e-6f)
    {
        result.right = glm::vec3{1.0f, 0.0f, 0.0f};
    }
    result.up = glm::normalize(glm::cross(result.right, result.forward));

    result.view = glm::lookAtRH(result.position, result.position + result.forward, result.up);
    result.world = glm::inverse(result.view);

    auto const aspect =
        static_cast<float>(std::max(viewportExtent_.x, 1u)) / static_cast<float>(std::max(viewportExtent_.y, 1u));
    auto const fov = clampPositive(lens_.verticalFovRadians, glm::radians(60.0f));
    auto const nearPlane = clampPositive(lens_.nearPlane, 0.1f);
    auto farPlane = clampPositive(lens_.farPlane, nearPlane + 1.0f);
    if (farPlane <= nearPlane + 1e-4f)
    {
        farPlane = nearPlane + 1000.0f;
    }

    result.projection = glm::perspectiveRH_ZO(fov, aspect, nearPlane, farPlane);
    result.viewProjection = result.projection * result.view;
    result.frustum = frustumFromViewProjection(result.viewProjection);
    return result;
}

[[nodiscard]] RendererCameraOverride ViewerPerspectiveCamera::buildRendererCameraOverride() const noexcept
{
    auto const cameraFrame = frame();
    return RendererCameraOverride{
        .frameConstants =
            nr::scene::SceneBridgeFrameConstants{
                .view = cameraFrame.view,
                .projection = cameraFrame.projection,
                .viewProjection = cameraFrame.viewProjection,
                .cameraWorld = cameraFrame.position,
            },
        .frustum = cameraFrame.frustum,
    };
}
} // namespace nr::renderer
