import std;
import dependency.dlss;
import dependency.math;
import dependency.vulkan;
import nr.renderPasses;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
static_assert(sizeof(DirectX::XMFLOAT4X4) == 64u);
static_assert(std::is_standard_layout_v<DirectX::XMFLOAT4X4>);
static_assert(std::is_trivially_copyable_v<DirectX::XMFLOAT4X4>);

[[nodiscard]] nr::dependency::dlss::OptimalSettings makeOptimalSettings(nr::dependency::dlss::Dimensions targetSize,
                                                               nr::dependency::dlss::Quality quality)
{
    auto divisor = std::uint32_t{2u};
    switch (quality)
    {
    case nr::dependency::dlss::Quality::Performance:
        divisor = 4u;
        break;
    case nr::dependency::dlss::Quality::Balanced:
        divisor = 3u;
        break;
    case nr::dependency::dlss::Quality::Quality:
        divisor = 2u;
        break;
    case nr::dependency::dlss::Quality::UltraPerformance:
        divisor = 5u;
        break;
    case nr::dependency::dlss::Quality::Dlaa:
        divisor = 1u;
        break;
    case nr::dependency::dlss::Quality::Count:
        std::unreachable();
    }
    return nr::dependency::dlss::OptimalSettings{
        .optimalRenderSize = nr::dependency::dlss::Dimensions{targetSize.width / divisor, targetSize.height / divisor},
        .minimumRenderSize = nr::dependency::dlss::Dimensions{1u, 1u},
        .maximumRenderSize = targetSize,
    };
}

const nr::test::CaseRegistrar dlssRrPublicContractCase{
    "renderpasses DLSS RR keeps its complete disabled-by-default contract", [] {
        auto input = nr::renderPasses::makeDefaultDlssRayReconstructionNodeInput();
        nr::test::require(!input.enabled, "DLSS RR must be disabled by default");
        nr::test::requireEqual(nr::dependency::dlss::rayReconstructionResourceSlotCount, std::size_t{64u});
        nr::test::requireEqual(nr::dependency::dlss::rayReconstructionSubrectSlotCount, std::size_t{39u});
        nr::test::require(input.create.flags.hdr, "RR default creation must request HDR");
        nr::test::require(!input.create.flags.motionVectorsJittered,
                          "RR defaults must continue to consume unjittered motion vectors");
        nr::test::requireEqual(input.evaluate.motionVectorScale, std::array{1.0f, 1.0f});
        nr::test::require(!input.evaluate.visualizeMotionVectors);
        nr::test::requireEqual(input.create.quality, nr::dependency::dlss::Quality::Quality);
        nr::test::requireEqual(input.create.roughnessMode, nr::dependency::dlss::RoughnessMode::Packed);
        nr::test::requireEqual(std::ranges::count(input.includeResources, true), std::ptrdiff_t{7});
        nr::test::require(input.includeResources[static_cast<std::size_t>(
                              nr::dependency::dlss::RayReconstructionResourceSlot::SpecularHitDistance)]);
        nr::test::require(!input.includeResources[static_cast<std::size_t>(
                              nr::dependency::dlss::RayReconstructionResourceSlot::DisocclusionMask)]);
        nr::test::require(nr::renderPasses::dlssRayReconstructionResourceRequired(
                              nr::dependency::dlss::RayReconstructionResourceSlot::Color, input.create.roughnessMode, false));
        nr::test::require(!nr::renderPasses::dlssRayReconstructionResourceRequired(
                              nr::dependency::dlss::RayReconstructionResourceSlot::Roughness,
                              nr::dependency::dlss::RoughnessMode::Packed, false));
        nr::test::require(nr::renderPasses::dlssRayReconstructionResourceRequired(
                              nr::dependency::dlss::RayReconstructionResourceSlot::Roughness,
                              nr::dependency::dlss::RoughnessMode::Unpacked, false));
    }};

const nr::test::CaseRegistrar dlssRrResolutionControllerCase{
    "renderpasses DLSS RR resolution controller caches quality queries and resets temporal history", [] {
        auto controller = nr::renderPasses::DlssRayReconstructionResolutionController{};
        auto queryCount = std::size_t{0u};
        auto query = nr::renderPasses::DlssRayReconstructionResolutionController::OptimalSettingsQuery{
            [&](nr::dependency::dlss::Dimensions targetSize, nr::dependency::dlss::Quality quality) {
                ++queryCount;
                return makeOptimalSettings(targetSize, quality);
            }};

        auto const display = vk::Extent2D{1600u, 900u};
        auto disabled = controller.resolve({}, display, query);
        nr::test::requireEqual(disabled.displayExtent, display);
        nr::test::requireEqual(disabled.renderExtent, display);
        nr::test::require(!disabled.resetHistory);
        nr::test::requireEqual(queryCount, std::size_t{0u});

        auto qualityRequest = nr::renderPasses::DlssRayReconstructionResolutionRequest{
            .enabled = true,
            .quality = nr::dependency::dlss::Quality::Quality,
        };
        auto quality = controller.resolve(qualityRequest, display, query);
        nr::test::requireEqual(quality.renderExtent, vk::Extent2D{800u, 450u});
        nr::test::require(quality.resetHistory);
        nr::test::requireEqual(queryCount, std::size_t{1u});

        auto qualityCacheHit = controller.resolve(qualityRequest, display, query);
        nr::test::require(!qualityCacheHit.resetHistory);
        nr::test::requireEqual(queryCount, std::size_t{1u});

        qualityRequest.quality = nr::dependency::dlss::Quality::Performance;
        auto performance = controller.resolve(qualityRequest, display, query);
        nr::test::requireEqual(performance.renderExtent, vk::Extent2D{400u, 225u});
        nr::test::require(performance.resetHistory);
        nr::test::requireEqual(queryCount, std::size_t{2u});

        auto const resizedDisplay = vk::Extent2D{2000u, 1000u};
        auto resized = controller.resolve(qualityRequest, resizedDisplay, query);
        nr::test::requireEqual(resized.renderExtent, vk::Extent2D{500u, 250u});
        nr::test::require(resized.resetHistory);
        nr::test::requireEqual(queryCount, std::size_t{3u});

        auto dlaaRequest = nr::renderPasses::DlssRayReconstructionResolutionRequest{
            .enabled = true,
            .quality = nr::dependency::dlss::Quality::Dlaa,
        };
        auto dlaa = controller.resolve(dlaaRequest, resizedDisplay, query);
        nr::test::requireEqual(dlaa.renderExtent, resizedDisplay);
        nr::test::require(dlaa.resetHistory);
        nr::test::requireEqual(queryCount, std::size_t{4u});
    }};

const nr::test::CaseRegistrar dlssRrMatrixConventionCase{
    "renderpasses DLSS RR preserves DirectX row-vector matrices as NGX row-major payloads", [] {
        auto const transform = DirectX::XMFLOAT4X4{
            0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            4.0f, -2.0f, 7.0f, 1.0f,
        };
        auto const converted = nr::renderPasses::detail::toDlssRowVectorMatrix(transform);
        auto const expected = std::array{
            0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 4.0f, -2.0f, 7.0f, 1.0f,
        };
        nr::test::requireEqual(converted, expected);
    }};
} // namespace
