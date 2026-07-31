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
        auto const anisotropicBaseLayer = static_cast<nr::scene::RtMaterialLayerFlag>(
            static_cast<std::uint32_t>(baseLayer) |
            static_cast<std::uint32_t>(nr::scene::RtMaterialLayerFlag::anisotropicBaseLobe));

        auto const opaqueFeatures = nr::scene::RtMaterialFeatureFlag::none;
        auto const alphaMaskFeatures = nr::scene::RtMaterialFeatureFlag::alphaMask;
        auto const runtimeOnlyFeatures = static_cast<nr::scene::RtMaterialFeatureFlag>(
            static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaBlend) |
            static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::doubleSided) |
            static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::emissive));
        auto const mixedInstancePolicyKey =
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, runtimeOnlyFeatures, true);
        auto const anisotropicBaseKey =
            nr::renderPasses::makeRtHitPermutationKey(
                anisotropicBaseLayer,
                opaqueFeatures,
                false);

        nr::test::requireEqual(nr::renderPasses::kRtBsdfVariantHardUpperBound, 17u);
        nr::test::requireEqual(nr::renderPasses::kRtHitGroupVariantHardUpperBound, 34u);
        nr::test::require(
            nr::renderPasses::rtHitPermutationUsesAnyHit(mixedInstancePolicyKey),
            "mixed-instance single-sided geometry should select the shared any-hit material policy");

        // Per-geometry permutation keys are built from (layerFlags, featureFlags): the CHS variant is
        // keyed only by layerFlags, while featureFlags contribute only the alpha hit-group policy.
        auto firstInstanceKeys = std::array{
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, opaqueFeatures),
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, alphaMaskFeatures),
        };
        auto secondInstanceKeys = std::array{
            nr::renderPasses::makeRtHitPermutationKey(baseLayer, runtimeOnlyFeatures),
            anisotropicBaseKey,
            nr::renderPasses::makeRtHitPermutationKey(clearcoatLayer, alphaMaskFeatures),
        };
        auto const firstBase = nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 3u, firstInstanceKeys);
        auto const secondBase = nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 4u, secondInstanceKeys);
        nr::renderPasses::finalizeSceneRtHitSbtPlan(plan);

        nr::test::require(plan.valid(), "RT hit SBT plan should validate after finalization");
        nr::test::requireEqual(firstBase, 0u);
        nr::test::requireEqual(secondBase, 2u);
        nr::test::requireEqual(plan.records.size(), std::size_t{5u});
        nr::test::requireEqual(plan.instances.size(), std::size_t{2u});
        nr::test::requireEqual(plan.permutations.size(), std::size_t{4u});
        nr::test::requireEqual(plan.records[0].permutationIndex, plan.records[2].permutationIndex);
        nr::test::require(!nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[0].permutationIndex].key));
        nr::test::require(nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[1].permutationIndex].key));
        nr::test::require(nr::renderPasses::rtHitPermutationUsesAnyHit(plan.permutations[plan.records[4].permutationIndex].key));
        nr::test::requireEqual(
            plan.permutations[plan.records[0].permutationIndex].key.bsdf.layerFlags,
            baseLayer,
            "runtime-only material flags should not enter the BSDF variant key");
        nr::test::requireEqual(
            plan.permutations[plan.records[4].permutationIndex].key.bsdf.layerFlags,
            clearcoatLayer,
            "layer flags should enter the BSDF variant key");
        nr::test::requireEqual(
            plan.permutations[plan.records[3].permutationIndex].key.bsdf.layerFlags,
            anisotropicBaseLayer,
            "anisotropy should enter the combined material layer flag key");
        nr::test::require(
            nr::renderPasses::rtHitClosestHitEntryPointName(plan.permutations[plan.records[3].permutationIndex].key) !=
                nr::renderPasses::rtHitClosestHitEntryPointName(plan.permutations[plan.records[0].permutationIndex].key),
            "anisotropic and isotropic base lobes should use distinct closest-hit stages");
        nr::test::requireEqual(
            nr::renderPasses::rtHitClosestHitEntryPointName(plan.permutations[plan.records[1].permutationIndex].key),
            nr::renderPasses::rtHitClosestHitEntryPointName(plan.permutations[plan.records[0].permutationIndex].key),
            "alpha-mask and opaque hit groups should reuse the same closest-hit shader when the BSDF key matches");
    }};

const nr::test::CaseRegistrar rtHitSbtExactDomainCase{
    "renderpasses RT hit SBT domain has exactly 17 BSDF and 34 hit-group keys",
    [] {
        using Layer = nr::scene::RtMaterialLayerFlag;
        using AnyHit = nr::renderPasses::RtHitAnyHitPolicy;

        auto const litLayerMasks = std::array{
            Layer::baseSurface,
            static_cast<Layer>(1u | 2u),
            static_cast<Layer>(1u | 4u),
            static_cast<Layer>(1u | 2u | 4u),
            static_cast<Layer>(1u | 8u),
            static_cast<Layer>(1u | 2u | 8u),
            static_cast<Layer>(1u | 4u | 8u),
            static_cast<Layer>(1u | 2u | 4u | 8u),
        };

        auto bsdfKeys = std::vector<nr::renderPasses::RtBsdfVariantKey>{
            nr::renderPasses::RtBsdfVariantKey{
                .layerFlags = Layer::none,
            },
        };
        std::ranges::for_each(litLayerMasks, [&](Layer layerFlags) {
            bsdfKeys.push_back(nr::renderPasses::RtBsdfVariantKey{
                .layerFlags = layerFlags,
            });
            bsdfKeys.push_back(nr::renderPasses::RtBsdfVariantKey{
                .layerFlags = static_cast<Layer>(
                    static_cast<std::uint32_t>(layerFlags) |
                    static_cast<std::uint32_t>(Layer::anisotropicBaseLobe)),
            });
        });

        nr::test::requireEqual(bsdfKeys.size(), std::size_t{17u});
        nr::test::require(
            std::ranges::all_of(bsdfKeys, nr::renderPasses::rtBsdfVariantKeyValid),
            "all 17 enumerated BSDF keys must be valid");
        nr::test::requireEqual(
            std::ranges::to<std::set>(bsdfKeys).size(),
            std::size_t{17u},
            "the BSDF domain must contain 17 unique keys");

        auto hitKeys =
            bsdfKeys |
            std::views::transform([](const nr::renderPasses::RtBsdfVariantKey& bsdf) {
                return std::array{
                    nr::renderPasses::RtHitPermutationKey{
                        .bsdf = bsdf,
                        .anyHitPolicy = AnyHit::none,
                    },
                    nr::renderPasses::RtHitPermutationKey{
                        .bsdf = bsdf,
                        .anyHitPolicy = AnyHit::materialPolicy,
                    },
                };
            }) |
            std::views::join |
            std::ranges::to<std::vector>();
        nr::test::requireEqual(hitKeys.size(), std::size_t{34u});
        nr::test::require(
            std::ranges::all_of(hitKeys, nr::renderPasses::rtHitPermutationKeyValid),
            "all 34 BSDF/any-hit keys must be valid");
        nr::test::requireEqual(
            std::ranges::to<std::set>(hitKeys).size(),
            std::size_t{34u},
            "the hit-group domain must contain 34 unique keys");

        auto const invalidLayers = std::array{
            static_cast<Layer>(2u),
            static_cast<Layer>(4u),
            static_cast<Layer>(8u),
            static_cast<Layer>(16u),
            static_cast<Layer>(32u),
            static_cast<Layer>(33u),
        };
        nr::test::require(
            std::ranges::none_of(invalidLayers, nr::renderPasses::rtMaterialLayerFlagsValid),
            "missing-base and unknown-bit layer masks must be rejected");
        nr::test::require(
            !nr::renderPasses::rtBsdfVariantKeyValid(
                nr::renderPasses::RtBsdfVariantKey{
                    .layerFlags = Layer::anisotropicBaseLobe,
                }),
            "unlit anisotropic BSDF key must be rejected");
        nr::test::require(
            !nr::renderPasses::rtHitAnyHitPolicyValid(static_cast<AnyHit>(2u)),
            "unknown any-hit enum values must be rejected");

        auto const isotropicOpaque = nr::renderPasses::RtHitPermutationKey{
            .bsdf = {.layerFlags = Layer::baseSurface},
            .anyHitPolicy = AnyHit::none,
        };
        auto const isotropicAnyHit = nr::renderPasses::RtHitPermutationKey{
            .bsdf = isotropicOpaque.bsdf,
            .anyHitPolicy = AnyHit::materialPolicy,
        };
        auto const anisotropicOpaque = nr::renderPasses::RtHitPermutationKey{
            .bsdf = {
                .layerFlags = static_cast<Layer>(
                    static_cast<std::uint32_t>(Layer::baseSurface) |
                    static_cast<std::uint32_t>(Layer::anisotropicBaseLobe)),
            },
            .anyHitPolicy = AnyHit::none,
        };
        auto const anisotropicAnyHit = nr::renderPasses::RtHitPermutationKey{
            .bsdf = anisotropicOpaque.bsdf,
            .anyHitPolicy = AnyHit::materialPolicy,
        };
        nr::test::requireEqual(
            nr::renderPasses::rtHitClosestHitEntryPointName(isotropicOpaque),
            nr::renderPasses::rtHitClosestHitEntryPointName(isotropicAnyHit),
            "opaque and any-hit isotropic groups must share their CHS name");
        nr::test::requireEqual(
            nr::renderPasses::rtHitClosestHitEntryPointName(anisotropicOpaque),
            nr::renderPasses::rtHitClosestHitEntryPointName(anisotropicAnyHit),
            "opaque and any-hit anisotropic groups must share their CHS name");
        nr::test::require(
            nr::renderPasses::rtHitClosestHitEntryPointName(isotropicOpaque) !=
                nr::renderPasses::rtHitClosestHitEntryPointName(anisotropicOpaque),
            "isotropic and anisotropic groups must have distinct CHS names");
    }};

const nr::test::CaseRegistrar rtHitSbtPlanValidationCase{
    "renderpasses RT hit SBT plan rejects duplicate mutated and stale plans",
    [] {
        using Layer = nr::scene::RtMaterialLayerFlag;
        auto plan = nr::renderPasses::SceneRtHitSbtPlan{};
        auto lookup = std::map<nr::renderPasses::RtHitPermutationKey, std::uint32_t>{};
        auto keys = std::array{
            nr::renderPasses::makeRtHitPermutationKey(
                Layer::baseSurface,
                nr::scene::RtMaterialFeatureFlag::none),
            nr::renderPasses::makeRtHitPermutationKey(
                static_cast<Layer>(1u | 2u),
                nr::scene::RtMaterialFeatureFlag::alphaMask),
        };
        static_cast<void>(
            nr::renderPasses::appendRtHitSbtPlanInstance(plan, lookup, 7u, keys));
        nr::renderPasses::finalizeSceneRtHitSbtPlan(plan);
        nr::test::require(plan.valid(), "freshly finalized plan must validate");

        auto duplicate = plan;
        duplicate.permutations[1].key = duplicate.permutations[0].key;
        nr::renderPasses::finalizeSceneRtHitSbtPlan(duplicate);
        nr::test::require(!duplicate.valid(), "duplicate permutation keys must be rejected");

        auto wrongGeometry = plan;
        wrongGeometry.records[1].geometryIndex = 0u;
        nr::renderPasses::finalizeSceneRtHitSbtPlan(wrongGeometry);
        nr::test::require(!wrongGeometry.valid(), "record geometry indices must match owning ranges");

        auto wrongInstance = plan;
        wrongInstance.records[0].instanceRecordIndex = 1u;
        nr::renderPasses::finalizeSceneRtHitSbtPlan(wrongInstance);
        nr::test::require(!wrongInstance.valid(), "record instance indices must reference owning ranges");

        auto nonContiguous = plan;
        nonContiguous.instances[0].hitRecordBase = 1u;
        nr::renderPasses::finalizeSceneRtHitSbtPlan(nonContiguous);
        nr::test::require(!nonContiguous.valid(), "instance ranges must start at zero and cover records exactly");

        auto stalePermutationHash = plan;
        stalePermutationHash.permutationSetHash ^= 1u;
        nr::test::require(!stalePermutationHash.valid(), "stale permutation-set hashes must be rejected");

        auto staleRecordHash = plan;
        staleRecordHash.recordPlanHash ^= 1u;
        nr::test::require(!staleRecordHash.valid(), "stale record-plan hashes must be rejected");
    }};
} // namespace
