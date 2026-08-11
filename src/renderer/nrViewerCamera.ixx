export module nr.renderer:viewerCamera;
import dependency.math;

import nr.scene;
import std;
import :renderer;

export namespace nr::renderer
{
struct ViewerPerspectiveLens
{
    float verticalFovRadians = nr::math::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct ViewerCameraPose
{
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 3.0f};
    float yawRadians = -nr::math::halfPi;
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
    DirectX::XMFLOAT2 cursorDelta{0.0f, 0.0f};
};

struct ViewerCameraControlConfig
{
    float movementSpeed = 3.5f;
    float lookRadiansPerPixel = 0.0035f;
    float pitchLimitRadians = nr::math::radians(89.0f);
};

struct ViewerPerspectiveCameraFrame
{
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 right{1.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 forward{0.0f, 0.0f, -1.0f};
    DirectX::XMFLOAT4X4 world{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 view{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 projection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4X4 viewProjection{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    nr::scene::SceneFrustum frustum{};
};

class ViewerPerspectiveCamera
{
  public:
    ViewerPerspectiveCamera() = default;

    void setViewportExtent(DirectX::XMUINT2 extent) noexcept;

    [[nodiscard]] DirectX::XMUINT2 viewportExtent() const noexcept;

    void setLens(const ViewerPerspectiveLens &lens) noexcept;

    [[nodiscard]] const ViewerPerspectiveLens &lens() const noexcept;

    void setPose(const ViewerCameraPose &pose) noexcept;

    [[nodiscard]] const ViewerCameraPose &pose() const noexcept;

    void setPoseFromLookAt(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target,
                           DirectX::XMFLOAT3 worldUp = DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f}) noexcept;

    void setControlConfig(const ViewerCameraControlConfig &config) noexcept;

    [[nodiscard]] const ViewerCameraControlConfig &controlConfig() const noexcept;

    void applyControl(const ViewerCameraControlInput &input) noexcept;

    [[nodiscard]] ViewerPerspectiveCameraFrame frame() const noexcept;

    [[nodiscard]] RendererCameraOverride buildRendererCameraOverride() const noexcept;

  private:
    DirectX::XMUINT2 viewportExtent_{1280u, 720u};
    ViewerPerspectiveLens lens_{};
    ViewerCameraPose pose_{};
    ViewerCameraControlConfig controlConfig_{};
};
} // namespace nr::renderer
