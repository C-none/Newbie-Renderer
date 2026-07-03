import dependency;
import nr.renderer;
import nr.scene;
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

const nr::test::CaseRegistrar stabilityCase{
    "renderer camera stability key ignores jitter-only changes",
    [] {
        auto constants = nr::scene::SceneBridgeFrameConstants{};
        constants.view = glm::lookAtRH(
            glm::vec3{2.0f, 1.0f, 4.0f},
            glm::vec3{0.0f},
            glm::vec3{0.0f, 1.0f, 0.0f});
        constants.projection = glm::perspectiveRH_ZO(
            glm::radians(70.0f),
            16.0f / 9.0f,
            0.2f,
            500.0f);
        constants.viewProjection = constants.projection * constants.view;
        constants.cameraWorld = glm::vec3{2.0f, 1.0f, 4.0f};

        auto const extent = vk::Extent2D{1600u, 900u};
        auto const key = nr::renderer::makeRendererCameraStabilityKey(constants, extent);
        auto const sameUnjitteredKey = nr::renderer::makeRendererCameraStabilityKey(constants, extent);
        nr::test::require(nr::renderer::rendererCameraStabilityKeysEquivalent(key, sameUnjitteredKey));

        auto jitteredKey = key;
        jitteredKey.projection = nr::renderer::applyCameraProjectionJitter(
            constants.projection,
            nr::renderer::makeHalton23CameraJitterSample(7u, extent).ndcOffset);
        nr::test::require(
            !nr::renderer::rendererCameraStabilityKeysEquivalent(key, jitteredKey),
            "stability comparison would reject jittered projection keys, so renderer must key unjittered projection");

        auto movedKey = key;
        movedKey.cameraWorld.x += 0.01f;
        nr::test::require(!nr::renderer::rendererCameraStabilityKeysEquivalent(key, movedKey));

        auto resizedKey = key;
        resizedKey.viewportExtent.width += 1u;
        nr::test::require(!nr::renderer::rendererCameraStabilityKeysEquivalent(key, resizedKey));
    }};
} // namespace
