import dependency.math;
import dependency.vulkan;
import nr.renderer;
import nr.test;
import std;

namespace
{
[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon = 1.0e-5f)
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] glm::vec3 ndcFromClip(const glm::vec4 &clip)
{
    return glm::vec3{clip.x, clip.y, clip.z} / clip.w;
}

[[nodiscard]] glm::vec2 topLeftPixelFromClip(const glm::vec4 &clip, vk::Extent2D extent)
{
    auto const ndc = glm::vec2{clip.x, clip.y} / clip.w;
    auto const uv = glm::vec2{ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f};
    return uv * glm::vec2{extent.width, extent.height};
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
        auto const projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        auto const ndcOffset = glm::vec2{0.01f, -0.02f};
        auto const jitteredProjection = nr::renderer::applyCameraProjectionJitter(projection, ndcOffset);

        auto const viewSpacePoint = glm::vec4{0.25f, -0.5f, -3.0f, 1.0f};
        auto const baseNdc = ndcFromClip(projection * viewSpacePoint);
        auto const jitteredNdc = ndcFromClip(jitteredProjection * viewSpacePoint);

        nr::test::require(nearlyEqual(jitteredNdc.x - baseNdc.x, ndcOffset.x));
        nr::test::require(nearlyEqual(jitteredNdc.y - baseNdc.y, ndcOffset.y));
    }};

const nr::test::CaseRegistrar unjitteredMotionCase{
    "renderer temporal motion excludes current camera jitter in top-left pixel units", [] {
        auto const extent = vk::Extent2D{1280u, 720u};
        auto const projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        auto const view = glm::lookAtRH(glm::vec3{0.0f, 0.0f, 3.0f}, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
        auto const sample = nr::renderer::makeHalton23CameraJitterSample(0u, extent);
        auto const jitteredProjection = nr::renderer::applyCameraProjectionJitter(projection, sample.ndcOffset);
        auto const worldPosition = glm::vec4{0.25f, -0.5f, 0.0f, 1.0f};

        auto const previousPixel = topLeftPixelFromClip(projection * view * worldPosition, extent);
        auto const unjitteredCurrentPixel = topLeftPixelFromClip(projection * view * worldPosition, extent);
        auto const jitteredCurrentPixel = topLeftPixelFromClip(jitteredProjection * view * worldPosition, extent);
        auto const temporalMotion = previousPixel - unjitteredCurrentPixel;
        auto const jitterContaminatedMotion = previousPixel - jitteredCurrentPixel;

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
