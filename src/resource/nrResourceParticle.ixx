export module nr.resource:particle;
import dependency.math;

import std;
import :geometry;
import :math;

export namespace nr::resource
{
struct FluidParticleSet
{
    std::string name{};
    std::vector<DirectX::XMFLOAT4> positionRadius{};
    std::vector<DirectX::XMFLOAT4> velocityLifetime{};
    std::vector<DirectX::XMFLOAT4> colorDensity{};

    [[nodiscard]] std::size_t count() const noexcept
    {
        return std::ranges::min({positionRadius.size(), velocityLifetime.size(), colorDensity.size()});
    }

    void reserve(std::size_t n)
    {
        positionRadius.reserve(n);
        velocityLifetime.reserve(n);
        colorDensity.reserve(n);
    }

    void resize(std::size_t n)
    {
        positionRadius.resize(n);
        velocityLifetime.resize(n);
        colorDensity.resize(n);
    }

    [[nodiscard]] Aabb computeBounds() const noexcept
    {
        auto bounds = Aabb{};
        std::ranges::for_each(positionRadius, [&](const DirectX::XMFLOAT4 &particle) {
            auto radius = std::max(particle.w, 0.0f);
            auto center = math::float3(particle.x, particle.y, particle.z);
            auto radiusVector = math::float3(radius, radius, radius);
            bounds.expand(math::subtract(center, radiusVector));
            bounds.expand(math::add(center, radiusVector));
        });
        return bounds;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        auto n = positionRadius.size();
        return velocityLifetime.size() == n && colorDensity.size() == n;
    }
};

} // namespace nr::resource
