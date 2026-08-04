module nr.resource;
import :geometry;
import dependency.math;
import std;
import :math;

namespace nr::resource
{
[[nodiscard]] bool Aabb::valid() const noexcept
{
    return math::finiteVec(min) && math::finiteVec(max) && glm::all(glm::lessThanEqual(min, max));
}

[[nodiscard]] glm::vec3 Aabb::center() const noexcept
{
    return (min + max) * 0.5f;
}

[[nodiscard]] glm::vec3 Aabb::extent() const noexcept
{
    return max - min;
}

void Aabb::expand(glm::vec3 p) noexcept
{
    min = glm::min(min, p);
    max = glm::max(max, p);
}

void Aabb::merge(const Aabb &rhs) noexcept
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

[[nodiscard]] bool BoundingSphere::valid(float eps) const noexcept
{
    return math::finiteVec(center) && math::finiteFloat(radius) && radius >= eps;
}

[[nodiscard]] glm::vec3 Triangle::edge01() const noexcept
{
    return p1 - p0;
}

[[nodiscard]] glm::vec3 Triangle::edge02() const noexcept
{
    return p2 - p0;
}

[[nodiscard]] glm::vec3 Triangle::computeFaceNormal() const noexcept
{
    auto normal = glm::cross(edge01(), edge02());
    auto length = glm::length(normal);
    if (length <= 1e-6f)
    {
        return glm::vec3{0.0f};
    }

    return normal / length;
}

[[nodiscard]] float Triangle::computeArea() const noexcept
{
    return 0.5f * glm::length(glm::cross(edge01(), edge02()));
}

[[nodiscard]] glm::vec3 Triangle::centroid() const noexcept
{
    return (p0 + p1 + p2) / 3.0f;
}

[[nodiscard]] bool Triangle::isDegenerate(float eps) const noexcept
{
    return computeArea() <= eps;
}
} // namespace nr::resource
