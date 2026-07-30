export module nr.renderPasses:rtHitSbtPlan;

import dependency.shaderShare;

import nr.scene;
import nr.utils;
import std;

export namespace nr::renderPasses
{
using nr::shader::share::RtHitAnyHitPolicy;

// CHS variants are keyed solely by the combined RtMaterialLayerFlag mask (9 valid combinations: unlit
// plus baseSurface with any subset of clearcoat/sheen/transmission). Any-hit policy doubles the hit-group
// space (hardware-opaque vs shared material-policy any-hit).
inline constexpr std::uint32_t kRtBsdfVariantHardUpperBound = 9u;
inline constexpr std::uint32_t kRtHitGroupVariantHardUpperBound = kRtBsdfVariantHardUpperBound * 2u;

struct RtBsdfVariantKey
{
    nr::scene::RtMaterialLayerFlag layerFlags = nr::scene::RtMaterialLayerFlag::none;

    [[nodiscard]] friend auto operator<=>(const RtBsdfVariantKey&, const RtBsdfVariantKey&) noexcept = default;
};

struct RtHitGroupKey
{
    RtBsdfVariantKey bsdf{};
    RtHitAnyHitPolicy anyHitPolicy = RtHitAnyHitPolicy::none;

    [[nodiscard]] friend auto operator<=>(const RtHitGroupKey&, const RtHitGroupKey&) noexcept = default;
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

    [[nodiscard]] bool valid() const noexcept
    {
        if (permutations.empty() || records.empty() || instances.empty())
        {
            return false;
        }

        auto const permutationIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(permutations.size()));
        auto const permutationsAreDense = std::ranges::all_of(permutationIndices, [&](std::uint32_t index) {
            return permutations[index].permutationIndex == index;
        });
        if (!permutationsAreDense)
        {
            return false;
        }

        auto const recordsReferenceValidPermutations = std::ranges::all_of(records, [&](const SceneRtHitSbtRecord& record) {
            return record.permutationIndex < permutations.size() && record.instanceRecordIndex < instances.size();
        });
        if (!recordsReferenceValidPermutations)
        {
            return false;
        }

        auto const instancesCoverValidRecordRanges = std::ranges::all_of(instances, [&](const SceneRtHitSbtInstanceRecord& instance) {
            auto const recordEnd = static_cast<std::uint64_t>(instance.hitRecordBase) + static_cast<std::uint64_t>(instance.geometryCount);
            return instance.geometryCount > 0u && recordEnd <= static_cast<std::uint64_t>(records.size());
        });
        return instancesCoverValidRecordRanges && permutationSetHash != 0u && recordPlanHash != 0u;
    }
};

[[nodiscard]] inline bool rtHitPermutationUsesAnyHit(const RtHitPermutationKey& key) noexcept
{
    return key.anyHitPolicy == RtHitAnyHitPolicy::materialPolicy;
}

[[nodiscard]] inline RtBsdfVariantKey makeRtBsdfVariantKey(nr::scene::RtMaterialLayerFlag layerFlags) noexcept
{
    return RtBsdfVariantKey{
        .layerFlags = layerFlags,
    };
}

[[nodiscard]] inline RtHitGroupKey makeRtHitGroupKey(
    nr::scene::RtMaterialLayerFlag layerFlags,
    nr::scene::RtMaterialFeatureFlag featureFlags,
    bool forceMaterialPolicy = false) noexcept
{
    auto const alphaMaskFeature = static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaMask);
    auto const requiresMaterialPolicy =
        forceMaterialPolicy ||
        (static_cast<std::uint32_t>(featureFlags) & alphaMaskFeature) != 0u;
    return RtHitGroupKey{
        .bsdf = makeRtBsdfVariantKey(layerFlags),
        .anyHitPolicy = requiresMaterialPolicy
                            ? RtHitAnyHitPolicy::materialPolicy
                            : RtHitAnyHitPolicy::none,
    };
}

[[nodiscard]] inline RtHitPermutationKey makeRtHitPermutationKey(
    nr::scene::RtMaterialLayerFlag layerFlags,
    nr::scene::RtMaterialFeatureFlag featureFlags,
    bool forceMaterialPolicy = false) noexcept
{
    return makeRtHitGroupKey(layerFlags, featureFlags, forceMaterialPolicy);
}

[[nodiscard]] inline std::uint64_t hashRtBsdfVariantKey(const RtBsdfVariantKey& key) noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "RtBsdfVariantKey.v2");
    nr::hash::hashAppend(state, static_cast<std::uint32_t>(key.layerFlags));
    return state;
}

[[nodiscard]] inline std::uint64_t hashRtHitPermutationKey(const RtHitPermutationKey& key) noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "RtHitGroupKey.v2");
    nr::hash::hashAppend(state, hashRtBsdfVariantKey(key.bsdf));
    nr::hash::hashAppend(state, key.anyHitPolicy);
    return state;
}

[[nodiscard]] inline std::string rtHitClosestHitEntryPointName(const RtBsdfVariantKey& key)
{
    return std::format("ch_{}", nr::hash::toHexString(hashRtBsdfVariantKey(key)));
}

[[nodiscard]] inline std::string rtHitClosestHitEntryPointName(const RtHitPermutationKey& key)
{
    return rtHitClosestHitEntryPointName(key.bsdf);
}

[[nodiscard]] inline std::uint32_t ensureRtHitPermutation(
    SceneRtHitSbtPlan& plan,
    std::map<RtHitPermutationKey, std::uint32_t>& permutationLookup,
    RtHitPermutationKey key)
{
    if (auto found = permutationLookup.find(key); found != permutationLookup.end())
    {
        return found->second;
    }

    nrAssert(
        plan.permutations.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit permutation count exceeds uint32 ABI.");
    nrAssert(
        plan.permutations.size() < static_cast<std::size_t>(kRtHitGroupVariantHardUpperBound),
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
    SceneRtHitSbtPlan& plan,
    std::map<RtHitPermutationKey, std::uint32_t>& permutationLookup,
    std::uint32_t instanceMetadataIndex,
    std::span<const RtHitPermutationKey> geometryPermutationKeys)
{
    nrAssert(!geometryPermutationKeys.empty(), "RT hit SBT plan instance requires at least one geometry.");
    nrAssert(
        plan.records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit SBT record base exceeds uint32 ABI.");
    nrAssert(
        plan.instances.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit SBT instance record count exceeds uint32 ABI.");
    nrAssert(
        geometryPermutationKeys.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit SBT geometry count exceeds uint32 ABI.");

    auto const hitRecordBase = static_cast<std::uint32_t>(plan.records.size());
    auto const instanceRecordIndex = static_cast<std::uint32_t>(plan.instances.size());
    auto const geometryCount = static_cast<std::uint32_t>(geometryPermutationKeys.size());
    nrAssert(
        plan.records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - geometryCount),
        "RT hit SBT record range exceeds uint32 ABI.");
    plan.instances.push_back(SceneRtHitSbtInstanceRecord{
        .hitRecordBase = hitRecordBase,
        .geometryCount = geometryCount,
        .instanceMetadataIndex = instanceMetadataIndex,
    });

    auto const geometryIndices = std::views::iota(std::uint32_t{0}, geometryCount);
    std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) {
        auto const permutationIndex = ensureRtHitPermutation(plan, permutationLookup, geometryPermutationKeys[geometryIndex]);
        plan.records.push_back(SceneRtHitSbtRecord{
            .permutationIndex = permutationIndex,
            .instanceRecordIndex = instanceRecordIndex,
            .geometryIndex = geometryIndex,
        });
    });

    return hitRecordBase;
}

inline void finalizeSceneRtHitSbtPlan(SceneRtHitSbtPlan& plan) noexcept
{
    auto permutationState = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(permutationState, "SceneRtHitSbtPlan.permutationSet.v1");
    nr::hash::hashAppend(permutationState, static_cast<std::uint32_t>(plan.permutations.size()));
    std::ranges::for_each(plan.permutations, [&](const SceneRtHitSbtPermutation& permutation) {
        nr::hash::hashAppend(permutationState, permutation.permutationIndex);
        nr::hash::hashAppend(permutationState, hashRtHitPermutationKey(permutation.key));
    });
    plan.permutationSetHash = permutationState;

    auto recordState = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(recordState, "SceneRtHitSbtPlan.recordPlan.v1");
    nr::hash::hashAppend(recordState, plan.permutationSetHash);
    nr::hash::hashAppend(recordState, static_cast<std::uint32_t>(plan.instances.size()));
    std::ranges::for_each(plan.instances, [&](const SceneRtHitSbtInstanceRecord& instance) {
        nr::hash::hashAppend(recordState, instance.hitRecordBase);
        nr::hash::hashAppend(recordState, instance.geometryCount);
        nr::hash::hashAppend(recordState, instance.instanceMetadataIndex);
    });
    nr::hash::hashAppend(recordState, static_cast<std::uint32_t>(plan.records.size()));
    std::ranges::for_each(plan.records, [&](const SceneRtHitSbtRecord& record) {
        nr::hash::hashAppend(recordState, record.permutationIndex);
        nr::hash::hashAppend(recordState, record.instanceRecordIndex);
        nr::hash::hashAppend(recordState, record.geometryIndex);
    });
    plan.recordPlanHash = recordState;
}
} // namespace nr::renderPasses
