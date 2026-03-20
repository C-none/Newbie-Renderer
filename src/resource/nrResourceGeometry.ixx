module;
export module nr.resource:geometry;

import dependency;
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
    ~Aabb() = default;

    [[nodiscard]] bool valid() const noexcept
    {
         return math::finiteVec(min) &&
             math::finiteVec(max) &&
               glm::all(glm::lessThanEqual(min, max));
    }

    [[nodiscard]] glm::vec3 center() const noexcept
    {
        return (min + max) * 0.5f;
    }

    [[nodiscard]] glm::vec3 extent() const noexcept
    {
        return max - min;
    }

    void expand(glm::vec3 p) noexcept
    {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    void merge(const Aabb &rhs) noexcept
    {
        if (!rhs.valid())
        {
            return;
        }

        if (!valid())
        {
            *this = rhs;
            return;
        }

        min = glm::min(min, rhs.min);
        max = glm::max(max, rhs.max);
    }
};

struct BoundingSphere
{
    glm::vec3 center{};
    float radius = 0.0f;

    constexpr BoundingSphere() noexcept = default;
    constexpr BoundingSphere(glm::vec3 inCenter, float inRadius) noexcept : center(inCenter), radius(inRadius)
    {
    }
    ~BoundingSphere() = default;

    [[nodiscard]] bool valid(float eps = 1e-6f) const noexcept
    {
        return math::finiteVec(center) && math::finiteFloat(radius) && radius >= eps;
    }
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
    ~Triangle() = default;

    [[nodiscard]] glm::vec3 edge01() const noexcept
    {
        return p1 - p0;
    }

    [[nodiscard]] glm::vec3 edge02() const noexcept
    {
        return p2 - p0;
    }

    [[nodiscard]] glm::vec3 computeFaceNormal() const noexcept
    {
        auto normal = glm::cross(edge01(), edge02());
        auto length = glm::length(normal);
        if (length <= 1e-6f)
        {
            return glm::vec3{0.0f};
        }

        return normal / length;
    }

    [[nodiscard]] float computeArea() const noexcept
    {
        return 0.5f * glm::length(glm::cross(edge01(), edge02()));
    }

    [[nodiscard]] glm::vec3 centroid() const noexcept
    {
        return (p0 + p1 + p2) / 3.0f;
    }

    [[nodiscard]] bool isDegenerate(float eps = 1e-6f) const noexcept
    {
        return computeArea() <= eps;
    }
};

} // namespace nr::resource
