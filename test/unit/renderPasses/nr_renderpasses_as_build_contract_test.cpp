import std;
import nr.renderPasses;
import nr.scene;
import nr.test;

namespace
{
const nr::test::CaseRegistrar asBuildInputCase{
    "renderpasses AS build input keeps cache retirement latency positive",
    [] {
        auto input = nr::renderPasses::AccelerationStructureBuildNodeInput{};
        nr::test::require(input.unusedFrameRetireLatency > 0u, "AS build cache retirement latency should stay positive");
    }};

const nr::test::CaseRegistrar rtHitSbtPlanCase{
    "renderpasses RT hit SBT plan keeps per-geometry records while deduping permutations",
    [] {
        auto plan = nr::renderPasses::SceneRtHitSbtPlan{};
        auto lookup = std::map<nr::renderPasses::RtHitPermutationKey, std::uint32_t>{};

        auto const baseLayer = nr::scene::RtMaterialLayerFlag::baseSurface;
        auto const clearcoatLayer = static_cast<nr::scene::RtMaterialLayerFlag>(
            static_cast<std::uint32_t>(nr::scene::RtMaterialLayerFlag::baseSurface) |
            static_cast<std::uint32_t>(nr::scene::RtMaterialLayerFlag::clearcoat));

        auto const opaqueFeatures = nr::scene::RtMaterialFeatureFlag::none;
        auto const alphaMaskFeatures = nr::scene::RtMaterialFeatureFlag::alphaMask;
        auto const runtimeOnlyFeatures = static_cast<nr::scene::RtMaterialFeatureFlag>(
            static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaBlend) |
            static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::doubleSided) |
            static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::emissive));

        nr::test::requireEqual(nr::renderPasses::kRtBsdfVariantHardUpperBound, 9u);
        nr::test::requireEqual(nr::renderPasses::kRtHitGroupVariantHardUpperBound, 18u);

        // Per-geometry permutation keys are built from (layerFlags, featureFlags): the CHS variant is
        // keyed only by layerFlags, while featureFlags contribute only the alpha hit-group policy.
        auto firstInstanceKeys = std::array{
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, opaqueFeatures),
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, alphaMaskFeatures),
        };
        auto secondInstanceKeys = std::array{
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, runtimeOnlyFeatures),
            nr::renderPasses::makeRtHitPermutationKey(clearcoatLayer, alphaMaskFeatures),
        };
        auto const firstBase = nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 3u, firstInstanceKeys);
        auto const secondBase = nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 4u, secondInstanceKeys);
        nr::renderPasses::finalizeSceneRtHitSbtPlan(plan);

        nr::test::require(plan.valid(), "RT hit SBT plan should validate after finalization");
        nr::test::requireEqual(firstBase, 0u);
        nr::test::requireEqual(secondBase, 2u);
        nr::test::requireEqual(plan.records.size(), std::size_t{4u});
        nr::test::requireEqual(plan.instances.size(), std::size_t{2u});
        nr::test::requireEqual(plan.permutations.size(), std::size_t{3u});
        nr::test::requireEqual(plan.records[0].permutationIndex, plan.records[2].permutationIndex);
        nr::test::require(nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[1].permutationIndex].key));
        nr::test::require(nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[3].permutationIndex].key));
        nr::test::requireEqual(
            plan.permutations[plan.records[0].permutationIndex].key.bsdf.layerFlags,
            baseLayer,
            "runtime-only material flags should not enter the BSDF variant key");
        nr::test::requireEqual(
            plan.permutations[plan.records[3].permutationIndex].key.bsdf.layerFlags,
            clearcoatLayer,
            "layer flags should enter the BSDF variant key");
        nr::test::requireEqual(
            nr::renderPasses::rtHitClosestHitEntryPointName(plan.permutations[plan.records[1].permutationIndex].key),
            nr::renderPasses::rtHitClosestHitEntryPointName(plan.permutations[plan.records[0].permutationIndex].key),
            "alpha-mask and opaque hit groups should reuse the same closest-hit shader when the BSDF key matches");
    }};
} // namespace
