export module nr.app:camera;
import dependency.math;

import nr.renderer;
import nr.rhi;
import nr.scene;
import :ui;

export namespace nr::app
{
struct AppCameraCursorTrackingState
{
    bool hasPrevious = false;
    glm::dvec2 previous{0.0, 0.0};
};

struct AppCameraDefaultView
{
    nr::renderer::ViewerPerspectiveLens lens{};
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
};

class AppCamera
{
  public:
    AppCamera() = default;

    void initializeDefault(const nr::rhi::PresentationContext& presentation,
                           const AppCameraDefaultView& defaults = {}) noexcept;
    void initializeFromSceneOrDefault(const nr::scene::Scene& scene,
                                      const nr::rhi::PresentationContext& presentation,
                                      const AppCameraDefaultView& defaults = {}) noexcept;
    void syncViewportExtent(const nr::rhi::PresentationContext& presentation) noexcept;
    void updateFromPresentation(const nr::rhi::PresentationContext& presentation,
                                float deltaSeconds,
                                const UiCaptureState& captureState = {}) noexcept;

    [[nodiscard]] nr::renderer::ViewerPerspectiveCamera& viewer() noexcept;
    [[nodiscard]] const nr::renderer::ViewerPerspectiveCamera& viewer() const noexcept;
    [[nodiscard]] nr::renderer::ViewerPerspectiveCameraFrame frame() const noexcept;
    [[nodiscard]] nr::renderer::RendererCameraOverride buildRendererCameraOverride() const noexcept;

  private:
    nr::renderer::ViewerPerspectiveCamera viewer_{};
    AppCameraCursorTrackingState cursorTracking_{};
};
} // namespace nr::app
