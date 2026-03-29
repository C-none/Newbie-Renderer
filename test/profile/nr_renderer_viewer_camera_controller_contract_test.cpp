import std;
import dependency;
import nr.renderer;

namespace
{
[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }

    return true;
}

[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon = 1e-4f)
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] bool vec3Near(const glm::vec3& left, const glm::vec3& right, float epsilon = 1e-4f)
{
    return nearlyEqual(left.x, right.x, epsilon) &&
           nearlyEqual(left.y, right.y, epsilon) &&
           nearlyEqual(left.z, right.z, epsilon);
}

[[nodiscard]] bool mat4Near(const glm::mat4& left, const glm::mat4& right, float epsilon = 1e-4f)
{
    auto rows = std::views::iota(0, 4);
    auto columns = std::views::iota(0, 4);

    return std::ranges::all_of(columns, [&](int column) {
        return std::ranges::all_of(rows, [&](int row) {
            return nearlyEqual(left[column][row], right[column][row], epsilon);
        });
    });
}

[[nodiscard]] bool testMovementUsesDeltaSeconds()
{
    auto camera = nr::renderer::ViewerPerspectiveCamera{};
    camera.setPose(nr::renderer::ViewerCameraPose{
        .position = glm::vec3{0.0f, 0.0f, 0.0f},
        .yawRadians = -glm::half_pi<float>(),
        .pitchRadians = 0.0f,
    });
    camera.setControlConfig(nr::renderer::ViewerCameraControlConfig{
        .movementSpeed = 4.0f,
        .lookRadiansPerPixel = 0.0035f,
        .pitchLimitRadians = glm::radians(89.0f),
    });

    camera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 0.5f,
        .moveForward = true,
    });

    auto const expected = glm::vec3{0.0f, 0.0f, -2.0f};
    return require(vec3Near(camera.pose().position, expected),
                   "Forward movement should be scaled by deltaSeconds and movementSpeed.");
}

[[nodiscard]] bool testRotationRequiresActiveDrag()
{
    auto camera = nr::renderer::ViewerPerspectiveCamera{};
    auto const before = camera.pose();

    camera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 1.0f / 60.0f,
        .rotateActive = false,
        .cursorDelta = glm::vec2{160.0f, -80.0f},
    });

    auto const after = camera.pose();
    return require(nearlyEqual(before.yawRadians, after.yawRadians) && nearlyEqual(before.pitchRadians, after.pitchRadians),
                   "Yaw/pitch should not change unless rotateActive is true.");
}

[[nodiscard]] bool testPitchClamp()
{
    auto camera = nr::renderer::ViewerPerspectiveCamera{};
    auto config = nr::renderer::ViewerCameraControlConfig{};
    config.lookRadiansPerPixel = 0.01f;
    config.pitchLimitRadians = glm::radians(45.0f);
    camera.setControlConfig(config);

    camera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 1.0f / 60.0f,
        .rotateActive = true,
        .cursorDelta = glm::vec2{0.0f, -1000.0f},
    });

    if (!require(nearlyEqual(camera.pose().pitchRadians, config.pitchLimitRadians),
                 "Pitch should clamp at +pitchLimitRadians."))
    {
        return false;
    }

    camera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 1.0f / 60.0f,
        .rotateActive = true,
        .cursorDelta = glm::vec2{0.0f, 2000.0f},
    });

    return require(nearlyEqual(camera.pose().pitchRadians, -config.pitchLimitRadians),
                   "Pitch should clamp at -pitchLimitRadians.");
}

[[nodiscard]] bool testLocalAxisMovement()
{
    auto camera = nr::renderer::ViewerPerspectiveCamera{};
    camera.setPose(nr::renderer::ViewerCameraPose{
        .position = glm::vec3{0.0f, 0.0f, 0.0f},
        .yawRadians = 0.0f,
        .pitchRadians = 0.0f,
    });
    camera.setControlConfig(nr::renderer::ViewerCameraControlConfig{
        .movementSpeed = 2.0f,
        .lookRadiansPerPixel = 0.0035f,
        .pitchLimitRadians = glm::radians(89.0f),
    });

    camera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 1.0f,
        .moveRight = true,
    });

    if (!require(vec3Near(camera.pose().position, glm::vec3{0.0f, 0.0f, 2.0f}),
                 "With yaw=0, moveRight should move along +Z local-right axis."))
    {
        return false;
    }

    camera.setPose(nr::renderer::ViewerCameraPose{
        .position = glm::vec3{0.0f, 0.0f, 0.0f},
        .yawRadians = 0.0f,
        .pitchRadians = 0.0f,
    });

    camera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 1.0f,
        .moveUp = true,
    });

    return require(vec3Near(camera.pose().position, glm::vec3{0.0f, 2.0f, 0.0f}),
                   "moveUp should move along local up axis.");
}

[[nodiscard]] bool testOverrideMatchesComputedFrame()
{
    auto camera = nr::renderer::ViewerPerspectiveCamera{};
    camera.setViewportExtent(glm::uvec2{1600u, 900u});
    camera.setLens(nr::renderer::ViewerPerspectiveLens{
        .verticalFovRadians = glm::radians(70.0f),
        .nearPlane = 0.2f,
        .farPlane = 500.0f,
    });
    camera.setPoseFromLookAt(glm::vec3{2.0f, 1.0f, 4.0f}, glm::vec3{0.0f, 0.0f, 0.0f});

    auto const frame = camera.frame();
    auto const override = camera.buildRendererCameraOverride();

    if (!require(mat4Near(override.frameConstants.view, frame.view),
                 "Override frame constants view matrix should match computed camera frame."))
    {
        return false;
    }

    if (!require(mat4Near(override.frameConstants.projection, frame.projection),
                 "Override frame constants projection matrix should match computed camera frame."))
    {
        return false;
    }

    if (!require(mat4Near(override.frameConstants.viewProjection, frame.viewProjection),
                 "Override frame constants viewProjection matrix should match computed camera frame."))
    {
        return false;
    }

    if (!require(vec3Near(override.frameConstants.cameraWorld, frame.position),
                 "Override cameraWorld should match computed camera frame position."))
    {
        return false;
    }

    auto planeIndices = std::views::iota(std::size_t{0}, override.frustum.planes.size());
    auto allFinite = std::ranges::all_of(planeIndices, [&](std::size_t index) {
        auto const& plane = override.frustum.planes[index];
        return std::isfinite(plane.x) && std::isfinite(plane.y) && std::isfinite(plane.z) && std::isfinite(plane.w);
    });

    return require(allFinite, "Override frustum planes should contain finite coefficients.");
}
} // namespace

int main()
{
    if (!testMovementUsesDeltaSeconds())
    {
        std::println("[FAIL] viewer camera movement delta-time contract failed");
        return 1;
    }

    if (!testRotationRequiresActiveDrag())
    {
        std::println("[FAIL] viewer camera rotate activation contract failed");
        return 2;
    }

    if (!testPitchClamp())
    {
        std::println("[FAIL] viewer camera pitch clamp contract failed");
        return 3;
    }

    if (!testLocalAxisMovement())
    {
        std::println("[FAIL] viewer camera local-axis movement contract failed");
        return 4;
    }

    if (!testOverrideMatchesComputedFrame())
    {
        std::println("[FAIL] viewer camera override frame contract failed");
        return 5;
    }

    std::println("[OK] viewer camera controller contract tests passed");
    return 0;
}
