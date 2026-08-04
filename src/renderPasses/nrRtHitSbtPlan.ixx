export module nr.renderPasses:rtHitSbtPlan;

import dependency.shaderShare;

import nr.scene;
import nr.utils;
import std;

export namespace nr::renderPasses
{
using nr::shader::share::RtHitAnyHitPolicy;
using nr::shader::share::RtRayType;

inline constexpr std::uint32_t kRtRayTypeCount = static_cast<std::uint32_t>(RtRayType::count);

static_assert(std::same_as<std::underlying_type_t<RtRayType>, std::uint32_t>);
static_assert(static_cast<std::uint32_t>(RtRayType::material) == 0u);
static_assert(static_cast<std::uint32_t>(RtRayType::shadow) == 1u);
static_assert(kRtRayTypeCount == 2u);
static_assert(kRtRayTypeCount <= 16u);

[[nodiscard]] constexpr bool rtRayTypeValid(RtRayType rayType) noexcept
{
    return rayType == RtRayType::material || rayType == RtRayType::shadow;
}

[[nodiscard]] constexpr std::uint64_t rtPhysicalHitRecordIndex(std::uint32_t logicalIndex,
                                                               RtRayType rayType) noexcept
{
    nrAssert(rtRayTypeValid(rayType), "Physical RT hit SBT record index requires a concrete ray type.");
    return static_cast<std::uint64_t>(logicalIndex) * kRtRayTypeCount + static_cast<std::uint32_t>(rayType);
}

[[nodiscard]] constexpr std::uint64_t rtPhysicalHitRecordCount(std::uint32_t logicalCount) noexcept
{
    return static_cast<std::uint64_t>(logicalCount) * kRtRayTypeCount;
}

// CHS variants use one combined material flag mask. Unlit is zero; each of the eight lit physical
// layer masks has an isotropic form and an anisotropic-base-lobe form. Any-hit policy remains
// orthogonal and doubles the hit-group space.
inline constexpr std::uint32_t kRtBsdfVariantHardUpperBound = 17u;
inline constexpr std::uint32_t kRtHitGroupVariantHardUpperBound = kRtBsdfVariantHardUpperBound * 2u;

struct RtBsdfVariantKey
{
    nr::scene::RtMaterialLayerFlag layerFlags = nr::scene::RtMaterialLayerFlag::none;

    [[nodiscard]] friend auto operator<=>(const RtBsdfVariantKey &, const RtBsdfVariantKey &) noexcept = default;
};

struct RtHitGroupKey
{
    RtBsdfVariantKey bsdf{};
    RtHitAnyHitPolicy anyHitPolicy = RtHitAnyHitPolicy::none;

    [[nodiscard]] friend auto operator<=>(const RtHitGroupKey &, const RtHitGroupKey &) noexcept = default;
};

using RtHitPermutationKey = RtHitGroupKey;

struct SceneRtHitSbtPermutation
{
    RtHitPermutationKey key{};
    std::uint32_t permutationIndex = 0u;
};

struct SceneRtHitSbtRecord
{
    std::uint32_t permutationIndex = 0u;
    std::uint32_t instanceRecordIndex = 0u;
    std::uint32_t geometryIndex = 0u;
};

struct SceneRtHitSbtInstanceRecord
{
    std::uint32_t hitRecordBase = 0u;
    std::uint32_t geometryCount = 0u;
    std::uint32_t instanceMetadataIndex = 0u;
};

struct SceneRtHitSbtPlan
{
    std::vector<SceneRtHitSbtPermutation> permutations{};
    std::vector<SceneRtHitSbtRecord> records{};
    std::vector<SceneRtHitSbtInstanceRecord> instances{};
    std::uint64_t permutationSetHash = 0u;
    std::uint64_t recordPlanHash = 0u;

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] constexpr bool rtMaterialLayerFlagsValid(nr::scene::RtMaterialLayerFlag layerFlags) noexcept
{
    constexpr auto knownMask = static_cast<std::uint32_t>(nr::scene::kRtMaterialVariantMask);
    auto const bits = static_cast<std::uint32_t>(layerFlags);
    return bits == 0u || ((bits & ~knownMask) == 0u &&
                          (bits & static_cast<std::uint32_t>(nr::scene::RtMaterialLayerFlag::baseSurface)) != 0u);
}

[[nodiscard]] constexpr bool rtBsdfVariantKeyValid(const RtBsdfVariantKey &key) noexcept
{
    return rtMaterialLayerFlagsValid(key.layerFlags);
}

[[nodiscard]] constexpr bool rtHitAnyHitPolicyValid(RtHitAnyHitPolicy policy) noexcept
{
    return policy == RtHitAnyHitPolicy::none || policy == RtHitAnyHitPolicy::materialPolicy;
}

[[nodiscard]] constexpr bool rtHitPermutationKeyValid(const RtHitPermutationKey &key) noexcept
{
    return rtBsdfVariantKeyValid(key.bsdf) && rtHitAnyHitPolicyValid(key.anyHitPolicy);
}

[[nodiscard]] inline bool rtHitPermutationUsesAnyHit(const RtHitPermutationKey &key) noexcept
{
    return key.anyHitPolicy == RtHitAnyHitPolicy::materialPolicy;
}

[[nodiscard]] inline RtBsdfVariantKey makeRtBsdfVariantKey(nr::scene::RtMaterialLayerFlag layerFlags) noexcept
{
    nrAssert(rtMaterialLayerFlagsValid(layerFlags),
             "RT BSDF variant layer flags are outside the exact unlit/lit domain.");
    return RtBsdfVariantKey{
        .layerFlags = layerFlags,
    };
}

[[nodiscard]] inline RtHitGroupKey makeRtHitGroupKey(nr::scene::RtMaterialLayerFlag layerFlags,
                                                     nr::scene::RtMaterialFeatureFlag featureFlags,
                                                     bool forceMaterialPolicy = false) noexcept
{
    auto const alphaMaskFeature = static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaMask);
    auto const requiresMaterialPolicy =
        forceMaterialPolicy || (static_cast<std::uint32_t>(featureFlags) & alphaMaskFeature) != 0u;
    return RtHitGroupKey{
        .bsdf = makeRtBsdfVariantKey(layerFlags),
        .anyHitPolicy = requiresMaterialPolicy ? RtHitAnyHitPolicy::materialPolicy : RtHitAnyHitPolicy::none,
    };
}

[[nodiscard]] inline RtHitPermutationKey makeRtHitPermutationKey(nr::scene::RtMaterialLayerFlag layerFlags,
                                                                 nr::scene::RtMaterialFeatureFlag featureFlags,
                                                                 bool forceMaterialPolicy = false) noexcept
{
    return makeRtHitGroupKey(layerFlags, featureFlags, forceMaterialPolicy);
}

[[nodiscard]] inline std::uint64_t hashRtBsdfVariantKey(const RtBsdfVariantKey &key) noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "RtBsdfVariantKey.v4");
    nr::hash::hashAppend(state, static_cast<std::uint32_t>(key.layerFlags));
    return state;
}

[[nodiscard]] inline std::uint64_t hashRtHitPermutationKey(const RtHitPermutationKey &key) noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "RtHitGroupKey.v4");
    nr::hash::hashAppend(state, hashRtBsdfVariantKey(key.bsdf));
    nr::hash::hashAppend(state, key.anyHitPolicy);
    return state;
}

[[nodiscard]] inline std::string rtHitClosestHitEntryPointName(const RtBsdfVariantKey &key)
{
    return std::format("ch_{}", nr::hash::toHexString(hashRtBsdfVariantKey(key)));
}

[[nodiscard]] inline std::string rtHitClosestHitEntryPointName(const RtHitPermutationKey &key)
{
    return rtHitClosestHitEntryPointName(key.bsdf);
}

[[nodiscard]] inline std::uint32_t ensureRtHitPermutation(
    SceneRtHitSbtPlan &plan, std::map<RtHitPermutationKey, std::uint32_t> &permutationLookup, RtHitPermutationKey key)
{
    nrAssert(rtHitPermutationKeyValid(key), "RT hit permutation key is outside the exact BSDF/any-hit domain.");
    if (auto found = permutationLookup.find(key); found != permutationLookup.end())
    {
        return found->second;
    }

    nrAssert(plan.permutations.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
             "RT hit permutation count exceeds uint32 ABI.");
    nrAssert(plan.permutations.size() < static_cast<std::size_t>(kRtHitGroupVariantHardUpperBound),
             "RT hit permutation count exceeds the current BSDF/any-hit-policy key space.");
    auto const permutationIndex = static_cast<std::uint32_t>(plan.permutations.size());
    plan.permutations.push_back(SceneRtHitSbtPermutation{
        .key = key,
        .permutationIndex = permutationIndex,
    });
    permutationLookup.emplace(key, permutationIndex);
    return permutationIndex;
}

[[nodiscard]] inline std::uint32_t appendRtHitSbtPlanInstance(
    SceneRtHitSbtPlan &plan, std::map<RtHitPermutationKey, std::uint32_t> &permutationLookup,
    std::uint32_t instanceMetadataIndex, std::span<const RtHitPermutationKey> geometryPermutationKeys)
{
    nrAssert(!geometryPermutationKeys.empty(), "RT hit SBT plan instance requires at least one geometry.");
    nrAssert(plan.records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
             "RT hit SBT record base exceeds uint32 ABI.");
    nrAssert(plan.instances.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
             "RT hit SBT instance record count exceeds uint32 ABI.");
    nrAssert(geometryPermutationKeys.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
             "RT hit SBT geometry count exceeds uint32 ABI.");

    auto const hitRecordBase = static_cast<std::uint32_t>(plan.records.size());
    auto const instanceRecordIndex = static_cast<std::uint32_t>(plan.instances.size());
    auto const geometryCount = static_cast<std::uint32_t>(geometryPermutationKeys.size());
    nrAssert(plan.records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - geometryCount),
             "RT hit SBT record range exceeds uint32 ABI.");
    auto const logicalRecordEnd = static_cast<std::uint64_t>(plan.records.size()) + geometryCount;
    nrAssert(logicalRecordEnd * kRtRayTypeCount <= std::numeric_limits<std::uint32_t>::max(),
             "Physical RT hit SBT record range exceeds uint32 ABI.");
    plan.instances.push_back(SceneRtHitSbtInstanceRecord{
        .hitRecordBase = hitRecordBase,
        .geometryCount = geometryCount,
        .instanceMetadataIndex = instanceMetadataIndex,
    });

    auto const geometryIndices = std::views::iota(std::uint32_t{0}, geometryCount);
    std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) {
        auto const permutationIndex =
            ensureRtHitPermutation(plan, permutationLookup, geometryPermutationKeys[geometryIndex]);
        plan.records.push_back(SceneRtHitSbtRecord{
            .permutationIndex = permutationIndex,
            .instanceRecordIndex = instanceRecordIndex,
            .geometryIndex = geometryIndex,
        });
    });

    return hitRecordBase;
}

[[nodiscard]] inline std::uint64_t calculateSceneRtHitSbtPermutationSetHash(const SceneRtHitSbtPlan &plan) noexcept
{
    auto permutationState = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(permutationState, "SceneRtHitSbtPlan.permutationSet.v3");
    nr::hash::hashAppend(permutationState, static_cast<std::uint32_t>(plan.permutations.size()));
    std::ranges::for_each(plan.permutations, [&](const SceneRtHitSbtPermutation &permutation) {
        nr::hash::hashAppend(permutationState, permutation.permutationIndex);
        nr::hash::hashAppend(permutationState, hashRtHitPermutationKey(permutation.key));
    });
    return permutationState;
}

[[nodiscard]] inline std::uint64_t calculateSceneRtHitSbtRecordPlanHash(const SceneRtHitSbtPlan &plan,
                                                                        std::uint64_t permutationSetHash) noexcept
{
    auto recordState = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(recordState, "SceneRtHitSbtPlan.recordPlan.v4");
    nr::hash::hashAppend(recordState, static_cast<std::uint32_t>(RtRayType::material));
    nr::hash::hashAppend(recordState, static_cast<std::uint32_t>(RtRayType::shadow));
    nr::hash::hashAppend(recordState, kRtRayTypeCount);
    nr::hash::hashAppend(recordState, permutationSetHash);
    nr::hash::hashAppend(recordState, static_cast<std::uint32_t>(plan.instances.size()));
    std::ranges::for_each(plan.instances, [&](const SceneRtHitSbtInstanceRecord &instance) {
        nr::hash::hashAppend(recordState, instance.hitRecordBase);
        nr::hash::hashAppend(recordState, instance.geometryCount);
        nr::hash::hashAppend(recordState, instance.instanceMetadataIndex);
    });
    nr::hash::hashAppend(recordState, static_cast<std::uint32_t>(plan.records.size()));
    std::ranges::for_each(plan.records, [&](const SceneRtHitSbtRecord &record) {
        nr::hash::hashAppend(recordState, record.permutationIndex);
        nr::hash::hashAppend(recordState, record.instanceRecordIndex);
        nr::hash::hashAppend(recordState, record.geometryIndex);
    });
    return recordState;
}

[[nodiscard]] inline bool SceneRtHitSbtPlan::valid() const noexcept
{
    if (permutations.empty() || records.empty() || instances.empty() ||
        permutations.size() > kRtHitGroupVariantHardUpperBound)
    {
        return false;
    }

    if (records.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        rtPhysicalHitRecordCount(static_cast<std::uint32_t>(records.size())) >
            std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }

    auto const permutationIndices = std::views::iota(std::size_t{0}, permutations.size());
    if (!std::ranges::all_of(permutationIndices, [&](std::size_t index) {
            auto const &permutation = permutations[index];
            return permutation.permutationIndex == index && rtHitPermutationKeyValid(permutation.key) &&
                   std::ranges::count(permutations, permutation.key, &SceneRtHitSbtPermutation::key) == 1;
        }))
    {
        return false;
    }

    if (!std::ranges::all_of(records, [&](const SceneRtHitSbtRecord &record) {
            return record.permutationIndex < permutations.size() && record.instanceRecordIndex < instances.size();
        }))
    {
        return false;
    }

    auto const instanceIndices = std::views::iota(std::size_t{0}, instances.size());
    if (!std::ranges::all_of(instanceIndices, [&](std::size_t instanceIndex) {
            auto const &instance = instances[instanceIndex];
            auto const expectedBase = instanceIndex == 0u
                                          ? std::uint64_t{0}
                                          : static_cast<std::uint64_t>(instances[instanceIndex - 1u].hitRecordBase) +
                                                instances[instanceIndex - 1u].geometryCount;
            auto const recordEnd = static_cast<std::uint64_t>(instance.hitRecordBase) + instance.geometryCount;
            if (instance.geometryCount == 0u || instance.hitRecordBase != expectedBase || recordEnd > records.size())
            {
                return false;
            }

            auto const geometryIndices = std::views::iota(std::uint32_t{0}, instance.geometryCount);
            return std::ranges::all_of(geometryIndices, [&](std::uint32_t geometryIndex) {
                auto const &record = records[instance.hitRecordBase + geometryIndex];
                return record.instanceRecordIndex == instanceIndex && record.geometryIndex == geometryIndex;
            });
        }))
    {
        return false;
    }

    auto const &finalInstance = instances.back();
    auto const finalRecordEnd = static_cast<std::uint64_t>(finalInstance.hitRecordBase) + finalInstance.geometryCount;
    if (finalRecordEnd != records.size())
    {
        return false;
    }

    auto const expectedPermutationSetHash = calculateSceneRtHitSbtPermutationSetHash(*this);
    auto const expectedRecordPlanHash = calculateSceneRtHitSbtRecordPlanHash(*this, expectedPermutationSetHash);
    return permutationSetHash == expectedPermutationSetHash && recordPlanHash == expectedRecordPlanHash;
}

inline void finalizeSceneRtHitSbtPlan(SceneRtHitSbtPlan &plan) noexcept
{
    plan.permutationSetHash = calculateSceneRtHitSbtPermutationSetHash(plan);
    plan.recordPlanHash = calculateSceneRtHitSbtRecordPlanHash(plan, plan.permutationSetHash);
}
} // namespace nr::renderPasses
