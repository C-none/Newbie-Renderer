import std;
import dependency.math;
import dependency.vulkan;
import nr.renderer;
import nr.test;

namespace
{
[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon = 1.0e-5f)
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] DirectX::XMFLOAT4 transformRowVector(const DirectX::XMFLOAT4 &vector,
                                                    const DirectX::XMFLOAT4X4 &matrix) noexcept
{
    return DirectX::XMFLOAT4{
        vector.x * matrix._11 + vector.y * matrix._21 + vector.z * matrix._31 + vector.w * matrix._41,
        vector.x * matrix._12 + vector.y * matrix._22 + vector.z * matrix._32 + vector.w * matrix._42,
        vector.x * matrix._13 + vector.y * matrix._23 + vector.z * matrix._33 + vector.w * matrix._43,
        vector.x * matrix._14 + vector.y * matrix._24 + vector.z * matrix._34 + vector.w * matrix._44,
    };
}

[[nodiscard]] DirectX::XMFLOAT4X4 matrixProduct(const DirectX::XMFLOAT4X4 &left,
                                                 const DirectX::XMFLOAT4X4 &right) noexcept
{
    auto result = DirectX::XMFLOAT4X4{};
    DirectX::XMStoreFloat4x4(&result,
                              DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&left), DirectX::XMLoadFloat4x4(&right)));
    return result;
}

[[nodiscard]] DirectX::XMFLOAT3 ndcFromClip(const DirectX::XMFLOAT4 &clip)
{
    return DirectX::XMFLOAT3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

[[nodiscard]] DirectX::XMFLOAT2 topLeftPixelFromClip(const DirectX::XMFLOAT4 &clip, vk::Extent2D extent)
{
    return DirectX::XMFLOAT2{
        (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(extent.width),
        (0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(extent.height),
    };
}

[[nodiscard]] DirectX::XMFLOAT4X4 perspective(float verticalFovRadians, float aspect, float nearPlane,
                                               float farPlane) noexcept
{
    auto result = DirectX::XMFLOAT4X4{};
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixPerspectiveFovRH(verticalFovRadians, aspect, nearPlane, farPlane));
    return result;
}

[[nodiscard]] DirectX::XMFLOAT4X4 lookAt(const DirectX::XMFLOAT3 &eye, const DirectX::XMFLOAT3 &target,
                                          const DirectX::XMFLOAT3 &up) noexcept
{
    auto result = DirectX::XMFLOAT4X4{};
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixLookAtRH(DirectX::XMLoadFloat3(&eye),
                                                                 DirectX::XMLoadFloat3(&target),
                                                                 DirectX::XMLoadFloat3(&up)));
    return result;
}

const nr::test::CaseRegistrar haltonCase{
    "renderer camera jitter uses wrapped Halton 2,3 samples", [] {
        auto sample = nr::renderer::makeHalton23CameraJitterSample(0u, vk::Extent2D{1280u, 720u});
        nr::test::requireEqual(sample.sampleIndex, 1u);
        nr::test::require(nearlyEqual(sample.pixelOffset.x, 0.0f));
        nr::test::require(nearlyEqual(sample.pixelOffset.y, -1.0f / 6.0f));
        nr::test::require(nearlyEqual(nr::renderer::haltonSequenceValue(2u, 2u), 0.25f));
        nr::test::require(nearlyEqual(nr::renderer::haltonSequenceValue(2u, 3u), 2.0f / 3.0f));

        auto wrapped = nr::renderer::makeHalton23CameraJitterSample(256u, vk::Extent2D{1280u, 720u});
        nr::test::requireEqual(wrapped.sampleIndex, 1u);
        nr::test::require(nearlyEqual(wrapped.pixelOffset.x, sample.pixelOffset.x));
        nr::test::require(nearlyEqual(wrapped.pixelOffset.y, sample.pixelOffset.y));
    }};

const nr::test::CaseRegistrar projectionJitterCase{
    "renderer projection jitter shifts projected NDC by requested offset", [] {
        auto const projection = perspective(nr::math::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        auto const ndcOffset = DirectX::XMFLOAT2{0.01f, -0.02f};
        auto const jitteredProjection = nr::renderer::applyCameraProjectionJitter(projection, ndcOffset);

        auto const viewSpacePoint = DirectX::XMFLOAT4{0.25f, -0.5f, -3.0f, 1.0f};
        auto const baseNdc = ndcFromClip(transformRowVector(viewSpacePoint, projection));
        auto const jitteredNdc = ndcFromClip(transformRowVector(viewSpacePoint, jitteredProjection));

        nr::test::require(nearlyEqual(jitteredNdc.x - baseNdc.x, ndcOffset.x));
        nr::test::require(nearlyEqual(jitteredNdc.y - baseNdc.y, ndcOffset.y));
    }};

const nr::test::CaseRegistrar unjitteredMotionCase{
    "renderer temporal motion excludes current camera jitter in top-left pixel units", [] {
        auto const extent = vk::Extent2D{1280u, 720u};
        auto const projection = perspective(nr::math::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        auto const view = lookAt(DirectX::XMFLOAT3{0.0f, 0.0f, 3.0f}, DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f},
                                 DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f});
        auto const sample = nr::renderer::makeHalton23CameraJitterSample(0u, extent);
        auto const jitteredProjection = nr::renderer::applyCameraProjectionJitter(projection, sample.ndcOffset);
        auto const worldPosition = DirectX::XMFLOAT4{0.25f, -0.5f, 0.0f, 1.0f};

        auto const previousPixel = topLeftPixelFromClip(transformRowVector(worldPosition, matrixProduct(view, projection)), extent);
        auto const unjitteredCurrentPixel =
            topLeftPixelFromClip(transformRowVector(worldPosition, matrixProduct(view, projection)), extent);
        auto const jitteredCurrentPixel =
            topLeftPixelFromClip(transformRowVector(worldPosition, matrixProduct(view, jitteredProjection)), extent);
        auto const temporalMotion = DirectX::XMFLOAT2{previousPixel.x - unjitteredCurrentPixel.x,
                                                       previousPixel.y - unjitteredCurrentPixel.y};
        auto const jitterContaminatedMotion = DirectX::XMFLOAT2{previousPixel.x - jitteredCurrentPixel.x,
                                                                 previousPixel.y - jitteredCurrentPixel.y};

        nr::test::require(nearlyEqual(temporalMotion.x, 0.0f) && nearlyEqual(temporalMotion.y, 0.0f),
                          "a stable camera must have zero motion despite projection jitter");
        nr::test::require(nearlyEqual(jitterContaminatedMotion.x, -sample.pixelOffset.x, 1.0e-3f) &&
                              nearlyEqual(jitterContaminatedMotion.y, -sample.pixelOffset.y, 1.0e-3f),
                          "previousPixel-currentPixel should expose exactly the removed jitter in top-left pixels");
    }};

const nr::test::CaseRegistrar frameStateCase{
    "renderer camera jitter uses render extent", [] {
        auto const renderExtent = vk::Extent2D{800u, 450u};
        auto const jitterConfig = nr::renderer::RendererCameraJitterConfig{
            .sequence = nr::renderer::RendererCameraJitterSequence::Halton23,
        };
        auto const frame41 = nr::renderer::makeRendererCameraFrameState(jitterConfig, 41u, renderExtent);
        auto const frame42 = nr::renderer::makeRendererCameraFrameState(jitterConfig, 42u, renderExtent);

        nr::test::require(frame41.jitterEnabled);
        nr::test::requireEqual(frame41.jitter.sampleIndex, 42u);
        nr::test::requireEqual(frame42.jitter.sampleIndex, 43u);
        nr::test::requireEqual(frame42.viewportExtent, renderExtent);
        nr::test::require(nearlyEqual(frame42.jitter.ndcOffset.x,
                                      2.0f * frame42.jitter.pixelOffset.x / static_cast<float>(renderExtent.width)));
        nr::test::require(nearlyEqual(frame42.jitter.ndcOffset.y,
                                      -2.0f * frame42.jitter.pixelOffset.y / static_cast<float>(renderExtent.height)));

        auto const disabled = nr::renderer::makeRendererCameraFrameState(nr::renderer::RendererCameraJitterConfig{},
                                                                         99u, vk::Extent2D{0u, 0u});
        nr::test::require(!disabled.jitterEnabled);
        nr::test::requireEqual(disabled.jitter.sampleIndex, 0u);
        nr::test::requireEqual(disabled.viewportExtent, vk::Extent2D{1u, 1u});
    }};
} // namespace
