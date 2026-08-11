module;
#include <DirectXMath.h>

export module dependency.math;

// Curated DirectXMath surface available to project modules without raw headers.
export namespace DirectX
{
using ::DirectX::XMFLOAT2;
using ::DirectX::XMFLOAT3;
using ::DirectX::XMFLOAT4;
using ::DirectX::XMFLOAT3X3;
using ::DirectX::XMFLOAT4X3;
using ::DirectX::XMFLOAT4X4;
using ::DirectX::XMINT2;
using ::DirectX::XMINT3;
using ::DirectX::XMINT4;
using ::DirectX::XMUINT2;
using ::DirectX::XMUINT3;
using ::DirectX::XMUINT4;

// SIMD types are transient computation values. Do not place them in persistent ABI records.
using ::DirectX::XMMATRIX;
using ::DirectX::XMVECTOR;

using ::DirectX::XMLoadFloat2;
using ::DirectX::XMLoadFloat3;
using ::DirectX::XMLoadFloat4;
using ::DirectX::XMLoadFloat3x3;
using ::DirectX::XMLoadFloat4x3;
using ::DirectX::XMLoadFloat4x4;
using ::DirectX::XMMatrixAffineTransformation;
using ::DirectX::XMMatrixDecompose;
using ::DirectX::XMMatrixDeterminant;
using ::DirectX::XMMatrixIdentity;
using ::DirectX::XMMatrixInverse;
using ::DirectX::XMMatrixLookAtRH;
using ::DirectX::XMMatrixMultiply;
using ::DirectX::XMMatrixOrthographicRH;
using ::DirectX::XMMatrixPerspectiveFovRH;
using ::DirectX::XMMatrixRotationQuaternion;
using ::DirectX::XMMatrixRotationRollPitchYaw;
using ::DirectX::XMMatrixScaling;
using ::DirectX::XMMatrixTranslation;
using ::DirectX::XMMatrixTranspose;
using ::DirectX::XMStoreFloat2;
using ::DirectX::XMStoreFloat3;
using ::DirectX::XMStoreFloat4;
using ::DirectX::XMStoreFloat3x3;
using ::DirectX::XMStoreFloat4x3;
using ::DirectX::XMStoreFloat4x4;
using ::DirectX::XMQuaternionIdentity;
using ::DirectX::XMQuaternionMultiply;
using ::DirectX::XMQuaternionNormalize;
using ::DirectX::XMQuaternionRotationRollPitchYaw;
using ::DirectX::XMQuaternionSlerp;
using ::DirectX::XMVector3AngleBetweenNormals;
using ::DirectX::XMVector3Cross;
using ::DirectX::XMVector3Dot;
using ::DirectX::XMVector3Length;
using ::DirectX::XMVector3Normalize;
using ::DirectX::XMVector3TransformCoord;
using ::DirectX::XMVector3TransformNormal;
using ::DirectX::XMVector4IsInfinite;
using ::DirectX::XMVector4IsNaN;
using ::DirectX::XMVector4Length;
using ::DirectX::XMVector4Transform;
using ::DirectX::XMVectorAdd;
using ::DirectX::XMVectorGetW;
using ::DirectX::XMVectorGetX;
using ::DirectX::XMVectorGetY;
using ::DirectX::XMVectorGetZ;
using ::DirectX::XMVectorLerp;
using ::DirectX::XMVectorMax;
using ::DirectX::XMVectorMin;
using ::DirectX::XMVectorMultiply;
using ::DirectX::XMVectorNegate;
using ::DirectX::XMVectorReplicate;
using ::DirectX::XMVectorScale;
using ::DirectX::XMVectorSet;
using ::DirectX::XMVectorSetW;
using ::DirectX::XMVectorSubtract;
using ::DirectX::XMVectorZero;
} // namespace DirectX

export namespace nr::math
{
struct Double2
{
    double x = 0.0;
    double y = 0.0;

    constexpr Double2() noexcept = default;
    constexpr Double2(double value) noexcept : x(value), y(value) {}
    constexpr Double2(double xValue, double yValue) noexcept : x(xValue), y(yValue) {}
};

inline constexpr float pi = 3.141592654f;
inline constexpr float halfPi = 1.570796327f;
inline constexpr float twoPi = 6.283185307f;

[[nodiscard]] constexpr float radians(float degrees) noexcept
{
    return degrees * (pi / 180.0f);
}

[[nodiscard]] constexpr float degrees(float radiansValue) noexcept
{
    return radiansValue * (180.0f / pi);
}
} // namespace nr::math
