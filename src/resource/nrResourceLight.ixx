export module nr.resource:light;
import dependency.math;

import std;
import :type;

export namespace nr::resource
{
struct LightAsset
{
    std::string name{};
    LightType type = LightType::directional;
    // glTF KHR_lights_punctual contract:
    // color is a unitless linear RGB multiplier. intensity is lux for directional
    // lights and candela for point/spot lights.
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    // Point/spot influence range in meters. 0 means the glTF default: infinite.
    // Directional lights ignore this field.
    float range = 0.0f;
    float innerConeRadians = 0.0f;
    float outerConeRadians = glm::radians(45.0f);
    bool castShadow = false;

    LightAsset() = default;
    ~LightAsset() = default;
};

} // namespace nr::resource
