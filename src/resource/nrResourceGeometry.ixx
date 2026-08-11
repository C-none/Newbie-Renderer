export module nr.resource:geometry;
import dependency.math;

import std;
import :math;

export namespace nr::resource
{
struct Aabb
{
    DirectX::XMFLOAT3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max()};
    DirectX::XMFLOAT3 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest()};

    constexpr Aabb() noexcept = default;
    constexpr Aabb(DirectX::XMFLOAT3 inMin, DirectX::XMFLOAT3 inMax) noexcept : min(inMin), max(inMax)
    {
    }
    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] DirectX::XMFLOAT3 center() const noexcept;

    [[nodiscard]] DirectX::XMFLOAT3 extent() const noexcept;

    void expand(DirectX::XMFLOAT3 p) noexcept;

    void merge(const Aabb &rhs) noexcept;
};

struct BoundingSphere
{
    DirectX::XMFLOAT3 center{};
    float radius = 0.0f;

    constexpr BoundingSphere() noexcept = default;
    constexpr BoundingSphere(DirectX::XMFLOAT3 inCenter, float inRadius) noexcept : center(inCenter), radius(inRadius)
    {
    }
    [[nodiscard]] bool valid(float eps = 1e-6f) const noexcept;
};

struct Triangle
{
    DirectX::XMFLOAT3 p0{};
    DirectX::XMFLOAT3 p1{};
    DirectX::XMFLOAT3 p2{};

    constexpr Triangle() noexcept = default;
    constexpr Triangle(DirectX::XMFLOAT3 inP0, DirectX::XMFLOAT3 inP1, DirectX::XMFLOAT3 inP2) noexcept
        : p0(inP0), p1(inP1), p2(inP2)
    {
    }
    [[nodiscard]] DirectX::XMFLOAT3 edge01() const noexcept;

    [[nodiscard]] DirectX::XMFLOAT3 edge02() const noexcept;

    [[nodiscard]] DirectX::XMFLOAT3 computeFaceNormal() const noexcept;

    [[nodiscard]] float computeArea() const noexcept;

    [[nodiscard]] DirectX::XMFLOAT3 centroid() const noexcept;

    [[nodiscard]] bool isDegenerate(float eps = 1e-6f) const noexcept;
};

} // namespace nr::resource
