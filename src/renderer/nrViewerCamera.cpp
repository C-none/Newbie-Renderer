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
    return std::isfinite(value) && value > 1e-6f ? value : fallback;
}

[[nodiscard]] float vector3Length(const DirectX::XMFLOAT3 &value) noexcept
{
    auto result = DirectX::XMFLOAT4{};
    DirectX::XMStoreFloat4(&result, DirectX::XMVector3Length(DirectX::XMLoadFloat3(&value)));
    return result.x;
}

[[nodiscard]] DirectX::XMFLOAT3 normalizedVector3(const DirectX::XMFLOAT3 &value) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&value)));
    return result;
}

[[nodiscard]] DirectX::XMFLOAT3 crossProduct(const DirectX::XMFLOAT3 &left, const DirectX::XMFLOAT3 &right) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result, DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&left), DirectX::XMLoadFloat3(&right)));
    return result;
}

[[nodiscard]] DirectX::XMFLOAT3 subtract(const DirectX::XMFLOAT3 &left, const DirectX::XMFLOAT3 &right) noexcept
{
    return DirectX::XMFLOAT3{left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] DirectX::XMFLOAT3 add(const DirectX::XMFLOAT3 &left, const DirectX::XMFLOAT3 &right) noexcept
{
    return DirectX::XMFLOAT3{left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] DirectX::XMFLOAT3 scale(const DirectX::XMFLOAT3 &value, float factor) noexcept
{
    return DirectX::XMFLOAT3{value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] DirectX::XMFLOAT3 forwardFromYawPitch(float yawRadians, float pitchRadians) noexcept
{
    auto const cosPitch = std::cos(pitchRadians);
    auto const forward = DirectX::XMFLOAT3{
        std::cos(yawRadians) * cosPitch,
        std::sin(pitchRadians),
        std::sin(yawRadians) * cosPitch,
    };

    auto const length = vector3Length(forward);
    return length <= 1e-6f || !std::isfinite(length) ? DirectX::XMFLOAT3{0.0f, 0.0f, -1.0f}
                                                      : scale(forward, 1.0f / length);
}

[[nodiscard]] nr::scene::SceneFrustum frustumFromViewProjection(const DirectX::XMFLOAT4X4 &viewProjection) noexcept
{
    auto frustum = nr::scene::SceneFrustum{};
    auto const rawPlanes = std::array{
        DirectX::XMFLOAT4{viewProjection._14 + viewProjection._11, viewProjection._24 + viewProjection._21,
                          viewProjection._34 + viewProjection._31, viewProjection._44 + viewProjection._41},
        DirectX::XMFLOAT4{viewProjection._14 - viewProjection._11, viewProjection._24 - viewProjection._21,
                          viewProjection._34 - viewProjection._31, viewProjection._44 - viewProjection._41},
        DirectX::XMFLOAT4{viewProjection._14 + viewProjection._12, viewProjection._24 + viewProjection._22,
                          viewProjection._34 + viewProjection._32, viewProjection._44 + viewProjection._42},
        DirectX::XMFLOAT4{viewProjection._14 - viewProjection._12, viewProjection._24 - viewProjection._22,
                          viewProjection._34 - viewProjection._32, viewProjection._44 - viewProjection._42},
        DirectX::XMFLOAT4{viewProjection._14 + viewProjection._13, viewProjection._24 + viewProjection._23,
                          viewProjection._34 + viewProjection._33, viewProjection._44 + viewProjection._43},
        DirectX::XMFLOAT4{viewProjection._14 - viewProjection._13, viewProjection._24 - viewProjection._23,
                          viewProjection._34 - viewProjection._33, viewProjection._44 - viewProjection._43},
    };

    std::ranges::for_each(std::views::iota(std::size_t{0}, rawPlanes.size()), [&](std::size_t planeIndex) {
        auto plane = rawPlanes[planeIndex];
        auto const length = vector3Length(DirectX::XMFLOAT3{plane.x, plane.y, plane.z});
        if (std::isfinite(length) && length > 1e-6f)
        {
            plane.x /= length;
            plane.y /= length;
            plane.z /= length;
            plane.w /= length;
        }
        frustum.planes[planeIndex] = plane;
    });

    return frustum;
}
} // namespace

namespace nr::renderer
{
void ViewerPerspectiveCamera::setViewportExtent(DirectX::XMUINT2 extent) noexcept
{
    viewportExtent_ = extent.x == 0u || extent.y == 0u ? DirectX::XMUINT2{1u, 1u} : extent;
}

[[nodiscard]] DirectX::XMUINT2 ViewerPerspectiveCamera::viewportExtent() const noexcept
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

void ViewerPerspectiveCamera::setPoseFromLookAt(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target,
                                                 DirectX::XMFLOAT3 worldUp) noexcept
{
    pose_.position = position;
    auto const forward = subtract(target, position);
    auto const length = vector3Length(forward);
    if (length <= 1e-6f || !std::isfinite(length))
    {
        pose_.yawRadians = -nr::math::halfPi;
        pose_.pitchRadians = 0.0f;
        return;
    }

    auto const normalizedForward = scale(forward, 1.0f / length);
    pose_.yawRadians = std::atan2(normalizedForward.z, normalizedForward.x);
    pose_.pitchRadians = std::asin(std::clamp(normalizedForward.y, -1.0f, 1.0f));
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
    auto const forward = forwardFromYawPitch(pose_.yawRadians, pose_.pitchRadians);
    auto right = normalizedVector3(crossProduct(forward, DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f}));
    if (!std::isfinite(vector3Length(right)) || vector3Length(right) <= 1e-6f)
    {
        right = DirectX::XMFLOAT3{1.0f, 0.0f, 0.0f};
    }
    auto const up = normalizedVector3(crossProduct(right, forward));

    auto movement = DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f};
    if (input.moveForward)
    {
        movement = add(movement, forward);
    }
    if (input.moveBackward)
    {
        movement = subtract(movement, forward);
    }
    if (input.moveRight)
    {
        movement = add(movement, right);
    }
    if (input.moveLeft)
    {
        movement = subtract(movement, right);
    }
    if (input.moveUp)
    {
        movement = add(movement, up);
    }
    if (input.moveDown)
    {
        movement = subtract(movement, up);
    }

    auto const movementLength = vector3Length(movement);
    if (movementLength > 1e-6f && std::isfinite(movementLength))
    {
        pose_.position = add(pose_.position, scale(movement, moveSpeed * dt / movementLength));
    }

    if (input.rotateActive)
    {
        auto const lookSpeed = clampPositive(controlConfig_.lookRadiansPerPixel, 0.0035f);
        pose_.yawRadians += input.cursorDelta.x * lookSpeed;
        pose_.pitchRadians -= input.cursorDelta.y * lookSpeed;
        auto const pitchLimit = clampPositive(controlConfig_.pitchLimitRadians, nr::math::radians(89.0f));
        pose_.pitchRadians = std::clamp(pose_.pitchRadians, -pitchLimit, pitchLimit);
    }
}

[[nodiscard]] ViewerPerspectiveCameraFrame ViewerPerspectiveCamera::frame() const noexcept
{
    auto result = ViewerPerspectiveCameraFrame{};
    result.position = pose_.position;
    result.forward = forwardFromYawPitch(pose_.yawRadians, pose_.pitchRadians);
    result.right = normalizedVector3(crossProduct(result.forward, DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f}));
    if (!std::isfinite(vector3Length(result.right)) || vector3Length(result.right) <= 1e-6f)
    {
        result.right = DirectX::XMFLOAT3{1.0f, 0.0f, 0.0f};
    }
    result.up = normalizedVector3(crossProduct(result.right, result.forward));

    auto const target = add(result.position, result.forward);
    DirectX::XMStoreFloat4x4(&result.view, DirectX::XMMatrixLookAtRH(DirectX::XMLoadFloat3(&result.position),
                                                                       DirectX::XMLoadFloat3(&target),
                                                                       DirectX::XMLoadFloat3(&result.up)));
    DirectX::XMStoreFloat4x4(&result.world, DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&result.view)));

    auto const aspect =
        static_cast<float>(std::max(viewportExtent_.x, 1u)) / static_cast<float>(std::max(viewportExtent_.y, 1u));
    auto const fov = clampPositive(lens_.verticalFovRadians, nr::math::radians(60.0f));
    auto const nearPlane = clampPositive(lens_.nearPlane, 0.1f);
    auto farPlane = clampPositive(lens_.farPlane, nearPlane + 1.0f);
    if (farPlane <= nearPlane + 1e-4f)
    {
        farPlane = nearPlane + 1000.0f;
    }

    DirectX::XMStoreFloat4x4(&result.projection, DirectX::XMMatrixPerspectiveFovRH(fov, aspect, nearPlane, farPlane));
    DirectX::XMStoreFloat4x4(&result.viewProjection,
                              DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&result.view),
                                                        DirectX::XMLoadFloat4x4(&result.projection)));
    result.frustum = frustumFromViewProjection(result.viewProjection);
    return result;
}

[[nodiscard]] RendererCameraOverride ViewerPerspectiveCamera::buildRendererCameraOverride() const noexcept
{
    auto const cameraFrame = frame();
    return RendererCameraOverride{
        .frameConstants = nr::scene::SceneBridgeFrameConstants{
            .view = cameraFrame.view,
            .projection = cameraFrame.projection,
            .viewProjection = cameraFrame.viewProjection,
            .cameraWorld = cameraFrame.position,
        },
        .frustum = cameraFrame.frustum,
    };
}
} // namespace nr::renderer
