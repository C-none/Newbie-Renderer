export module nr.resource:camera;
import dependency;

import std;
import :type;

export namespace nr::resource
{
struct CameraAsset
{
    std::string name{};
    CameraProjection projection = CameraProjection::perspective;
    std::optional<float> authoredAspectRatio{};
    float verticalFovRadians = glm::radians(60.0f);
    float orthoHeight = 10.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    CameraAsset() = default;
    ~CameraAsset() = default;

    [[nodiscard]] bool perspective() const noexcept
    {
        return projection == CameraProjection::perspective;
    }
};

} // namespace nr::resource
