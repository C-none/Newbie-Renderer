import std;
import dependency.math;
import nr.renderer;
import nr.test;

namespace
{
[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon = 1e-4f)
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] bool vec3Near(const DirectX::XMFLOAT3 &left, const DirectX::XMFLOAT3 &right,
                            float epsilon = 1e-4f)
{
    return nearlyEqual(left.x, right.x, epsilon) && nearlyEqual(left.y, right.y, epsilon) &&
           nearlyEqual(left.z, right.z, epsilon);
}

[[nodiscard]] bool mat4Near(const DirectX::XMFLOAT4X4 &left, const DirectX::XMFLOAT4X4 &right,
                            float epsilon = 1e-4f)
{
    auto const leftValues = std::array{left._11, left._12, left._13, left._14, left._21, left._22, left._23, left._24,
                                       left._31, left._32, left._33, left._34, left._41, left._42, left._43, left._44};
    auto const rightValues = std::array{right._11, right._12, right._13, right._14, right._21, right._22, right._23,
                                        right._24, right._31, right._32, right._33, right._34, right._41, right._42,
                                        right._43, right._44};
    return std::ranges::equal(leftValues, rightValues, [epsilon](float leftValue, float rightValue) {
        return nearlyEqual(leftValue, rightValue, epsilon);
    });
}

const nr::test::CaseRegistrar movementCase{
    "viewer camera movement uses delta seconds", [] {
        auto camera = nr::renderer::ViewerPerspectiveCamera{};
        camera.setPose(nr::renderer::ViewerCameraPose{
            .position = DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f},
            .yawRadians = -nr::math::halfPi,
        });
        camera.setControlConfig(nr::renderer::ViewerCameraControlConfig{
            .movementSpeed = 4.0f,
            .lookRadiansPerPixel = 0.0035f,
            .pitchLimitRadians = nr::math::radians(89.0f),
        });

        camera.applyControl(nr::renderer::ViewerCameraControlInput{
            .deltaSeconds = 0.5f,
            .moveForward = true,
        });

        nr::test::require(vec3Near(camera.pose().position, DirectX::XMFLOAT3{0.0f, 0.0f, -2.0f}),
                          "forward movement should be scaled by deltaSeconds and movementSpeed");
    }};

const nr::test::CaseRegistrar rotationActivationCase{
    "viewer camera rotation requires active drag", [] {
        auto camera = nr::renderer::ViewerPerspectiveCamera{};
        auto const before = camera.pose();

        camera.applyControl(nr::renderer::ViewerCameraControlInput{
            .deltaSeconds = 1.0f / 60.0f,
            .cursorDelta = DirectX::XMFLOAT2{160.0f, -80.0f},
        });

        auto const after = camera.pose();
        nr::test::require(nearlyEqual(before.yawRadians, after.yawRadians) &&
                              nearlyEqual(before.pitchRadians, after.pitchRadians),
                          "yaw and pitch should not change unless rotateActive is true");
    }};

const nr::test::CaseRegistrar pitchClampCase{
    "viewer camera clamps pitch", [] {
        auto camera = nr::renderer::ViewerPerspectiveCamera{};
        auto config = nr::renderer::ViewerCameraControlConfig{};
        config.lookRadiansPerPixel = 0.01f;
        config.pitchLimitRadians = nr::math::radians(45.0f);
        camera.setControlConfig(config);

        camera.applyControl(nr::renderer::ViewerCameraControlInput{
            .deltaSeconds = 1.0f / 60.0f,
            .rotateActive = true,
            .cursorDelta = DirectX::XMFLOAT2{0.0f, -1000.0f},
        });
        nr::test::require(nearlyEqual(camera.pose().pitchRadians, config.pitchLimitRadians),
                          "pitch should clamp to positive limit");

        camera.applyControl(nr::renderer::ViewerCameraControlInput{
            .deltaSeconds = 1.0f / 60.0f,
            .rotateActive = true,
            .cursorDelta = DirectX::XMFLOAT2{0.0f, 2000.0f},
        });
        nr::test::require(nearlyEqual(camera.pose().pitchRadians, -config.pitchLimitRadians),
                          "pitch should clamp to negative limit");
    }};

const nr::test::CaseRegistrar localAxisCase{
    "viewer camera moves along local axes", [] {
        auto camera = nr::renderer::ViewerPerspectiveCamera{};
        camera.setPose(nr::renderer::ViewerCameraPose{
            .position = DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f},
            .yawRadians = 0.0f,
        });
        camera.setControlConfig(nr::renderer::ViewerCameraControlConfig{
            .movementSpeed = 2.0f,
            .lookRadiansPerPixel = 0.0035f,
            .pitchLimitRadians = nr::math::radians(89.0f),
        });

        camera.applyControl(nr::renderer::ViewerCameraControlInput{
            .deltaSeconds = 1.0f,
            .moveRight = true,
        });
        nr::test::require(vec3Near(camera.pose().position, DirectX::XMFLOAT3{0.0f, 0.0f, 2.0f}),
                          "yaw=0 moveRight should move along +Z local-right axis");

        camera.setPose(nr::renderer::ViewerCameraPose{
            .position = DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f},
        });
        camera.applyControl(nr::renderer::ViewerCameraControlInput{
            .deltaSeconds = 1.0f,
            .moveUp = true,
        });
        nr::test::require(vec3Near(camera.pose().position, DirectX::XMFLOAT3{0.0f, 2.0f, 0.0f}),
                          "moveUp should move along local up axis");
    }};

const nr::test::CaseRegistrar overrideFrameCase{
    "viewer camera override matches computed frame", [] {
        auto camera = nr::renderer::ViewerPerspectiveCamera{};
        camera.setViewportExtent(DirectX::XMUINT2{1600u, 900u});
        camera.setLens(nr::renderer::ViewerPerspectiveLens{
            .verticalFovRadians = nr::math::radians(70.0f),
            .nearPlane = 0.2f,
            .farPlane = 500.0f,
        });
        camera.setPoseFromLookAt(DirectX::XMFLOAT3{2.0f, 1.0f, 4.0f}, DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f});

        auto const frame = camera.frame();
        auto const override = camera.buildRendererCameraOverride();

        nr::test::require(mat4Near(override.frameConstants.view, frame.view), "override view should match frame");
        nr::test::require(mat4Near(override.frameConstants.projection, frame.projection),
                          "override projection should match frame");
        nr::test::require(mat4Near(override.frameConstants.viewProjection, frame.viewProjection),
                          "override viewProjection should match frame");
        nr::test::require(vec3Near(override.frameConstants.cameraWorld, frame.position),
                          "override cameraWorld should match frame");

        auto const allFinite = std::ranges::all_of(override.frustum.planes, [](const DirectX::XMFLOAT4 &plane) {
            return std::isfinite(plane.x) && std::isfinite(plane.y) && std::isfinite(plane.z) && std::isfinite(plane.w);
        });
        nr::test::require(allFinite, "override frustum should contain finite planes");
    }};
} // namespace
