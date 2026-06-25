export module nr.renderer:viewerCamera;
import dependency.math;

import nr.scene;
import std;
import :renderer;

export namespace nr::renderer
{
struct ViewerPerspectiveLens
{
    float verticalFovRadians = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct ViewerCameraPose
{
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    float yawRadians = -glm::half_pi<float>();
    float pitchRadians = 0.0f;
};

struct ViewerCameraControlInput
{
    float deltaSeconds = 0.0f;
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool moveUp = false;
    bool moveDown = false;
    bool rotateActive = false;
    glm::vec2 cursorDelta{0.0f};
};

struct ViewerCameraControlConfig
{
    float movementSpeed = 3.5f;
    float lookRadiansPerPixel = 0.0035f;
    float pitchLimitRadians = glm::radians(89.0f);
};

struct ViewerPerspectiveCameraFrame
{
    glm::vec3 position{0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::mat4 world{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    nr::scene::SceneFrustum frustum{};
};

class ViewerPerspectiveCamera
{
  public:
    ViewerPerspectiveCamera() = default;

    void setViewportExtent(glm::uvec2 extent) noexcept;

    [[nodiscard]] glm::uvec2 viewportExtent() const noexcept;

    void setLens(const ViewerPerspectiveLens& lens) noexcept;

    [[nodiscard]] const ViewerPerspectiveLens& lens() const noexcept;

    void setPose(const ViewerCameraPose& pose) noexcept;

    [[nodiscard]] const ViewerCameraPose& pose() const noexcept;

    void setPoseFromLookAt(glm::vec3 position, glm::vec3 target, glm::vec3 worldUp = glm::vec3{0.0f, 1.0f, 0.0f}) noexcept;

    void setControlConfig(const ViewerCameraControlConfig& config) noexcept;

    [[nodiscard]] const ViewerCameraControlConfig& controlConfig() const noexcept;

    void applyControl(const ViewerCameraControlInput& input) noexcept;

    [[nodiscard]] ViewerPerspectiveCameraFrame frame() const noexcept;

    [[nodiscard]] RendererCameraOverride buildRendererCameraOverride() const noexcept;

  private:
    glm::uvec2 viewportExtent_{1280u, 720u};
    ViewerPerspectiveLens lens_{};
    ViewerCameraPose pose_{};
    ViewerCameraControlConfig controlConfig_{};
};
} // namespace nr::renderer

namespace nr::renderer::detail
{
void viewerCameraModuleAnchor() noexcept
{
}
} // namespace nr::renderer::detail
