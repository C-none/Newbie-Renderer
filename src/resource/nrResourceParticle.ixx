export module nr.resource:particle;
import dependency.math;

import std;
import :geometry;

export namespace nr::resource
{
struct FluidParticleSet
{
    std::string name{};
    std::vector<glm::vec4> positionRadius{};
    std::vector<glm::vec4> velocityLifetime{};
    std::vector<glm::vec4> colorDensity{};

    FluidParticleSet() = default;
    ~FluidParticleSet() = default;

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
        std::ranges::for_each(positionRadius, [&](const glm::vec4 &particle) {
            auto radius = std::max(particle.w, 0.0f);
            auto center = glm::vec3{particle.x, particle.y, particle.z};
            bounds.expand(center - glm::vec3{radius});
            bounds.expand(center + glm::vec3{radius});
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
