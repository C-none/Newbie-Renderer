import std;
import dependency;
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
        auto const alphaMaskFeature = static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaMask);
        auto const alphaBlendFeature = static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaBlend);

        auto firstInstanceFeatures = std::array{0u, alphaMaskFeature};
        auto secondInstanceFeatures = std::array{0u, alphaBlendFeature};
        auto const firstBase = nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 3u, firstInstanceFeatures);
        auto const secondBase = nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 4u, secondInstanceFeatures);
        nr::renderPasses::finalizeSceneRtHitSbtPlan(plan);

        nr::test::require(plan.valid(), "RT hit SBT plan should validate after finalization");
        nr::test::requireEqual(firstBase, 0u);
        nr::test::requireEqual(secondBase, 2u);
        nr::test::requireEqual(plan.records.size(), std::size_t{4u});
        nr::test::requireEqual(plan.instances.size(), std::size_t{2u});
        nr::test::requireEqual(plan.permutations.size(), std::size_t{3u});
        nr::test::requireEqual(plan.records[0].permutationIndex, plan.records[2].permutationIndex);
        nr::test::require(nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[1].permutationIndex].key));
        nr::test::require(!nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[3].permutationIndex].key));
    }};
} // namespace
