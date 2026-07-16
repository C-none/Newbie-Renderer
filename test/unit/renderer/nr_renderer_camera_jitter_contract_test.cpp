import dependency;
import nr.renderer;
import nr.test;
import std;

namespace
{
[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon = 1.0e-5f)
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] glm::vec3 ndcFromClip(const glm::vec4& clip)
{
    return glm::vec3{clip.x, clip.y, clip.z} / clip.w;
}

const nr::test::CaseRegistrar haltonCase{
    "renderer camera jitter uses wrapped Halton 2,3 samples",
    [] {
        auto sample = nr::renderer::makeHalton23CameraJitterSample(
            0u,
            vk::Extent2D{1280u, 720u});
        nr::test::requireEqual(sample.sampleIndex, 1u);
        nr::test::require(nearlyEqual(sample.pixelOffset.x, 0.0f));
        nr::test::require(nearlyEqual(sample.pixelOffset.y, -1.0f / 6.0f));
        nr::test::require(nearlyEqual(nr::renderer::haltonSequenceValue(2u, 2u), 0.25f));
        nr::test::require(nearlyEqual(nr::renderer::haltonSequenceValue(2u, 3u), 2.0f / 3.0f));

        auto wrapped = nr::renderer::makeHalton23CameraJitterSample(
            256u,
            vk::Extent2D{1280u, 720u});
        nr::test::requireEqual(wrapped.sampleIndex, 1u);
        nr::test::require(nearlyEqual(wrapped.pixelOffset.x, sample.pixelOffset.x));
        nr::test::require(nearlyEqual(wrapped.pixelOffset.y, sample.pixelOffset.y));
    }};

const nr::test::CaseRegistrar projectionJitterCase{
    "renderer projection jitter shifts projected NDC by requested offset",
    [] {
        auto const projection = glm::perspectiveRH_ZO(
            glm::radians(60.0f),
            16.0f / 9.0f,
            0.1f,
            100.0f);
        auto const ndcOffset = glm::vec2{0.01f, -0.02f};
        auto const jitteredProjection = nr::renderer::applyCameraProjectionJitter(projection, ndcOffset);

        auto const viewSpacePoint = glm::vec4{0.25f, -0.5f, -3.0f, 1.0f};
        auto const baseNdc = ndcFromClip(projection * viewSpacePoint);
        auto const jitteredNdc = ndcFromClip(jitteredProjection * viewSpacePoint);

        nr::test::require(nearlyEqual(jitteredNdc.x - baseNdc.x, ndcOffset.x));
        nr::test::require(nearlyEqual(jitteredNdc.y - baseNdc.y, ndcOffset.y));
    }};

const nr::test::CaseRegistrar frameStateCase{
    "renderer camera reset stays false and Halton phase follows frame ordinal",
    [] {
        auto const extent = vk::Extent2D{1600u, 900u};
        auto const jitterConfig = nr::renderer::RendererCameraJitterConfig{
            .sequence = nr::renderer::RendererCameraJitterSequence::Halton23,
        };
        auto const frame41 = nr::renderer::makeRendererCameraFrameState(jitterConfig, 41u, extent);
        auto const frame42 = nr::renderer::makeRendererCameraFrameState(jitterConfig, 42u, extent);

        nr::test::require(frame41.jitterEnabled);
        nr::test::require(!frame41.reset);
        nr::test::require(!frame42.reset);
        nr::test::requireEqual(frame41.jitter.sampleIndex, 42u);
        nr::test::requireEqual(frame42.jitter.sampleIndex, 43u);
        nr::test::requireEqual(frame42.viewportExtent, extent);

        auto const disabled = nr::renderer::makeRendererCameraFrameState(
            nr::renderer::RendererCameraJitterConfig{},
            99u,
            vk::Extent2D{0u, 0u});
        nr::test::require(!disabled.jitterEnabled);
        nr::test::require(!disabled.reset);
        nr::test::requireEqual(disabled.jitter.sampleIndex, 0u);
        nr::test::requireEqual(disabled.viewportExtent, vk::Extent2D{1u, 1u});
    }};
} // namespace
