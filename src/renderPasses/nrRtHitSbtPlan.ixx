export module nr.renderPasses:rtHitSbtPlan;

import nr.scene;
import nr.utils;
import std;

export namespace nr::renderPasses
{
enum class RtHitShadingModel : std::uint8_t
{
    gltfPbr = 0u,
};

enum class RtHitAlphaPolicy : std::uint8_t
{
    opaqueLike = 0u,
    alphaMask = 1u,
};

struct RtHitPermutationKey
{
    RtHitShadingModel shadingModel = RtHitShadingModel::gltfPbr;
    std::uint32_t materialFeatureMask = 0u;
    RtHitAlphaPolicy alphaPolicy = RtHitAlphaPolicy::opaqueLike;

    [[nodiscard]] friend bool operator<(const RtHitPermutationKey& lhs, const RtHitPermutationKey& rhs) noexcept
    {
        return std::tie(lhs.shadingModel, lhs.materialFeatureMask, lhs.alphaPolicy) <
               std::tie(rhs.shadingModel, rhs.materialFeatureMask, rhs.alphaPolicy);
    }

    [[nodiscard]] friend bool operator==(const RtHitPermutationKey&, const RtHitPermutationKey&) noexcept = default;
};

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
    return key.alphaPolicy == RtHitAlphaPolicy::alphaMask;
}

[[nodiscard]] inline RtHitPermutationKey makeRtHitPermutationKey(std::uint32_t materialFeatureMask) noexcept
{
    auto const alphaMaskFeature = static_cast<std::uint32_t>(nr::scene::RtMaterialFeatureFlag::alphaMask);
    return RtHitPermutationKey{
        .materialFeatureMask = materialFeatureMask,
        .alphaPolicy = (materialFeatureMask & alphaMaskFeature) != 0u
                           ? RtHitAlphaPolicy::alphaMask
                           : RtHitAlphaPolicy::opaqueLike,
    };
}

[[nodiscard]] inline std::uint64_t hashRtHitPermutationKey(const RtHitPermutationKey& key) noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "RtHitPermutationKey.v1");
    nr::hash::hashAppend(state, key.shadingModel);
    nr::hash::hashAppend(state, key.materialFeatureMask);
    nr::hash::hashAppend(state, key.alphaPolicy);
    return state;
}

[[nodiscard]] inline std::string rtHitPermutationHashHex(const RtHitPermutationKey& key)
{
    auto chars = nr::hash::toHexChars(hashRtHitPermutationKey(key));
    return std::string(nr::hash::toHexView(chars));
}

[[nodiscard]] inline std::string rtHitClosestHitEntryPointName(const RtHitPermutationKey& key)
{
    return std::format("ch_{}", rtHitPermutationHashHex(key));
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
    std::span<const std::uint32_t> geometryMaterialFeatureMasks)
{
    nrAssert(!geometryMaterialFeatureMasks.empty(), "RT hit SBT plan instance requires at least one geometry.");
    nrAssert(
        plan.records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit SBT record base exceeds uint32 ABI.");
    nrAssert(
        plan.instances.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit SBT instance record count exceeds uint32 ABI.");
    nrAssert(
        geometryMaterialFeatureMasks.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
        "RT hit SBT geometry count exceeds uint32 ABI.");

    auto const hitRecordBase = static_cast<std::uint32_t>(plan.records.size());
    auto const instanceRecordIndex = static_cast<std::uint32_t>(plan.instances.size());
    auto const geometryCount = static_cast<std::uint32_t>(geometryMaterialFeatureMasks.size());
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
        auto const key = makeRtHitPermutationKey(geometryMaterialFeatureMasks[geometryIndex]);
        auto const permutationIndex = ensureRtHitPermutation(plan, permutationLookup, key);
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
