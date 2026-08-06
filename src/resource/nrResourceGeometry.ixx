export module nr.resource:geometry;
import dependency.math;

import std;
import :math;

export namespace nr::resource
{
struct Aabb
{
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    constexpr Aabb() noexcept = default;
    constexpr Aabb(glm::vec3 inMin, glm::vec3 inMax) noexcept : min(inMin), max(inMax)
    {
    }
    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] glm::vec3 center() const noexcept;

    [[nodiscard]] glm::vec3 extent() const noexcept;

    void expand(glm::vec3 p) noexcept;

    void merge(const Aabb &rhs) noexcept;
};

struct BoundingSphere
{
    glm::vec3 center{};
    float radius = 0.0f;

    constexpr BoundingSphere() noexcept = default;
    constexpr BoundingSphere(glm::vec3 inCenter, float inRadius) noexcept : center(inCenter), radius(inRadius)
    {
    }
    [[nodiscard]] bool valid(float eps = 1e-6f) const noexcept;
};

struct Triangle
{
    glm::vec3 p0{};
    glm::vec3 p1{};
    glm::vec3 p2{};

    constexpr Triangle() noexcept = default;
    constexpr Triangle(glm::vec3 inP0, glm::vec3 inP1, glm::vec3 inP2) noexcept : p0(inP0), p1(inP1), p2(inP2)
    {
    }
    [[nodiscard]] glm::vec3 edge01() const noexcept;

    [[nodiscard]] glm::vec3 edge02() const noexcept;

    [[nodiscard]] glm::vec3 computeFaceNormal() const noexcept;

    [[nodiscard]] float computeArea() const noexcept;

    [[nodiscard]] glm::vec3 centroid() const noexcept;

    [[nodiscard]] bool isDegenerate(float eps = 1e-6f) const noexcept;
};

} // namespace nr::resource
