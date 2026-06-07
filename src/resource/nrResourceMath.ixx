export module nr.resource:math;
import dependency;

import std;

export namespace nr::resource::math
{
[[nodiscard]] inline bool finiteFloat(float value) noexcept
{
    return std::isfinite(value);
}

template <typename... TValues>
[[nodiscard]] inline bool finiteComponents(TValues... values) noexcept
{
    return (... && finiteFloat(static_cast<float>(values)));
}

template <glm::length_t L, typename TValue, glm::qualifier Q, std::size_t... Indices>
[[nodiscard]] inline bool finiteVecImpl(const glm::vec<L, TValue, Q> &value, std::index_sequence<Indices...>) noexcept
{
    return finiteComponents(value[Indices]...);
}

template <glm::length_t L, typename TValue, glm::qualifier Q>
[[nodiscard]] inline bool finiteVec(const glm::vec<L, TValue, Q> &value) noexcept
{
    return finiteVecImpl(value, std::make_index_sequence<static_cast<std::size_t>(L)>{});
}

[[nodiscard]] inline bool finiteQuat(const glm::quat &value) noexcept
{
    return finiteComponents(value.x, value.y, value.z, value.w);
}
} // namespace nr::resource::math
