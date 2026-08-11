module nr.resource;
import :geometry;
import dependency.math;
import std;
import :math;

namespace nr::resource
{
[[nodiscard]] bool Aabb::valid() const noexcept
{
    return math::finiteVec(min) && math::finiteVec(max) && min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

[[nodiscard]] DirectX::XMFLOAT3 Aabb::center() const noexcept
{
    return math::scale(math::add(min, max), 0.5f);
}

[[nodiscard]] DirectX::XMFLOAT3 Aabb::extent() const noexcept
{
    return math::subtract(max, min);
}

void Aabb::expand(DirectX::XMFLOAT3 p) noexcept
{
    min = math::min(min, p);
    max = math::max(max, p);
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

    min = math::min(min, rhs.min);
    max = math::max(max, rhs.max);
}

[[nodiscard]] bool BoundingSphere::valid(float eps) const noexcept
{
    return math::finiteVec(center) && math::finiteFloat(radius) && radius >= eps;
}

[[nodiscard]] DirectX::XMFLOAT3 Triangle::edge01() const noexcept
{
    return math::subtract(p1, p0);
}

[[nodiscard]] DirectX::XMFLOAT3 Triangle::edge02() const noexcept
{
    return math::subtract(p2, p0);
}

[[nodiscard]] DirectX::XMFLOAT3 Triangle::computeFaceNormal() const noexcept
{
    auto normal = math::cross(edge01(), edge02());
    auto length = math::length(normal);
    if (length <= 1e-6f)
    {
        return math::float3();
    }

    return math::scale(normal, 1.0f / length);
}

[[nodiscard]] float Triangle::computeArea() const noexcept
{
    return 0.5f * math::length(math::cross(edge01(), edge02()));
}

[[nodiscard]] DirectX::XMFLOAT3 Triangle::centroid() const noexcept
{
    return math::scale(math::add(math::add(p0, p1), p2), 1.0f / 3.0f);
}

[[nodiscard]] bool Triangle::isDegenerate(float eps) const noexcept
{
    return computeArea() <= eps;
}
} // namespace nr::resource
