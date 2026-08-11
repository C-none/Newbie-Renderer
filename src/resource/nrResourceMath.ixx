export module nr.resource:math;
import dependency.math;

import std;

export namespace nr::resource::math
{
[[nodiscard]] inline bool finiteFloat(float value) noexcept
{
    return std::isfinite(value);
}

template <typename... TValues> [[nodiscard]] inline bool finiteComponents(TValues... values) noexcept
{
    return (... && finiteFloat(static_cast<float>(values)));
}

[[nodiscard]] constexpr DirectX::XMFLOAT2 float2(float x = 0.0f, float y = 0.0f) noexcept
{
    return {x, y};
}

[[nodiscard]] constexpr DirectX::XMFLOAT3 float3(float x = 0.0f, float y = 0.0f, float z = 0.0f) noexcept
{
    return {x, y, z};
}

[[nodiscard]] constexpr DirectX::XMFLOAT4 float4(float x = 0.0f, float y = 0.0f, float z = 0.0f,
                                                   float w = 0.0f) noexcept
{
    return {x, y, z, w};
}

[[nodiscard]] constexpr DirectX::XMFLOAT4X4 identity4x4() noexcept
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] inline bool finiteVec(const DirectX::XMFLOAT2 &value) noexcept
{
    return finiteComponents(value.x, value.y);
}

[[nodiscard]] inline bool finiteVec(const DirectX::XMFLOAT3 &value) noexcept
{
    return finiteComponents(value.x, value.y, value.z);
}

[[nodiscard]] inline bool finiteVec(const DirectX::XMFLOAT4 &value) noexcept
{
    return finiteComponents(value.x, value.y, value.z, value.w);
}

[[nodiscard]] inline bool finiteQuat(const DirectX::XMFLOAT4 &value) noexcept
{
    return finiteVec(value);
}

[[nodiscard]] inline DirectX::XMFLOAT3 add(const DirectX::XMFLOAT3 &lhs, const DirectX::XMFLOAT3 &rhs) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result,
                           DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
    return result;
}

[[nodiscard]] inline DirectX::XMFLOAT3 subtract(const DirectX::XMFLOAT3 &lhs, const DirectX::XMFLOAT3 &rhs) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result,
                           DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
    return result;
}

[[nodiscard]] inline DirectX::XMFLOAT3 scale(const DirectX::XMFLOAT3 &value, float factor) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result, DirectX::XMVectorScale(DirectX::XMLoadFloat3(&value), factor));
    return result;
}

[[nodiscard]] inline DirectX::XMFLOAT4 scale(const DirectX::XMFLOAT4 &value, float factor) noexcept
{
    auto result = DirectX::XMFLOAT4{};
    DirectX::XMStoreFloat4(&result, DirectX::XMVectorScale(DirectX::XMLoadFloat4(&value), factor));
    return result;
}

[[nodiscard]] inline DirectX::XMFLOAT3 min(const DirectX::XMFLOAT3 &lhs, const DirectX::XMFLOAT3 &rhs) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result,
                           DirectX::XMVectorMin(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
    return result;
}

[[nodiscard]] inline DirectX::XMFLOAT3 max(const DirectX::XMFLOAT3 &lhs, const DirectX::XMFLOAT3 &rhs) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result,
                           DirectX::XMVectorMax(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
    return result;
}

[[nodiscard]] inline float dot(const DirectX::XMFLOAT3 &lhs, const DirectX::XMFLOAT3 &rhs) noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
}

[[nodiscard]] inline DirectX::XMFLOAT3 cross(const DirectX::XMFLOAT3 &lhs, const DirectX::XMFLOAT3 &rhs) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result,
                           DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
    return result;
}

[[nodiscard]] inline float length(const DirectX::XMFLOAT3 &value) noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&value)));
}

[[nodiscard]] inline float length(const DirectX::XMFLOAT4 &value) noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMVector4Length(DirectX::XMLoadFloat4(&value)));
}

[[nodiscard]] inline DirectX::XMFLOAT3 normalize(const DirectX::XMFLOAT3 &value) noexcept
{
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result, DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&value)));
    return result;
}
} // namespace nr::resource::math
