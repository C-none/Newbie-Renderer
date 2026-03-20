module;
export module nr.resource:light;

import dependency;
import std;
import :type;

export namespace nr::resource
{
struct LightAsset
{
    std::string name{};
    LightType type = LightType::directional;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 0.0f;
    float innerConeRadians = 0.0f;
    float outerConeRadians = glm::radians(45.0f);
    bool castShadow = false;

    LightAsset() = default;
    ~LightAsset() = default;

    [[nodiscard]] bool finiteRange() const noexcept
    {
        return range > 0.0f;
    }
};

} // namespace nr::resource