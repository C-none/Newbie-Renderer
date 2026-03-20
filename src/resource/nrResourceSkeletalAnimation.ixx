module;
export module nr.resource:skeletalAnimation;

import dependency;
import std;
import :math;

export namespace nr::resource
{
struct Bone
{
    alignas(16) glm::mat4 inverseBindPose{1.0f};
    alignas(16) glm::mat4 localBindPose{1.0f};
    std::string name{};
    std::int32_t parentIndex = -1;

    Bone() = default;
    ~Bone() = default;

    [[nodiscard]] bool isRoot() const noexcept
    {
        return parentIndex < 0;
    }
};

struct Skeleton
{
    std::string name{};
    std::vector<Bone> bones{};

    Skeleton() = default;
    ~Skeleton() = default;

    [[nodiscard]] std::size_t boneCount() const noexcept
    {
        return bones.size();
    }

    [[nodiscard]] std::size_t rootCount() const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count_if(bones, [](const Bone &bone) {
            return bone.isRoot();
        }));
    }

    [[nodiscard]] bool validateHierarchy() const noexcept
    {
        if (bones.empty())
        {
            return true;
        }

        auto indexRange = std::views::iota(std::size_t{0}, bones.size());
        auto allValidParents = std::ranges::all_of(indexRange, [&](std::size_t index) {
            auto parent = bones[index].parentIndex;
            return parent < 0 || static_cast<std::size_t>(parent) < bones.size();
        });
        if (!allValidParents)
        {
            return false;
        }

        if (rootCount() == 0u)
        {
            return false;
        }

        for (auto start : std::views::iota(std::size_t{0}, bones.size()))
        {
            auto hops = std::size_t{0};
            auto current = static_cast<std::int32_t>(start);
            while (current >= 0)
            {
                current = bones[static_cast<std::size_t>(current)].parentIndex;
                ++hops;
                if (hops > bones.size())
                {
                    return false;
                }
            }
        }

        return true;
    }
};

struct KeyframeVec3
{
    float timeSeconds = 0.0f;
    glm::vec3 value{};
};

struct KeyframeQuat
{
    float timeSeconds = 0.0f;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct BoneAnimationTrack
{
    std::int32_t boneIndex = -1;
    std::vector<KeyframeVec3> translations{};
    std::vector<KeyframeQuat> rotations{};
    std::vector<KeyframeVec3> scales{};

    BoneAnimationTrack() = default;
    ~BoneAnimationTrack() = default;
};

struct AnimationClip
{
    std::string name{};
    float durationSeconds = 0.0f;
    float ticksPerSecond = 0.0f;
    bool looping = true;
    std::vector<BoneAnimationTrack> tracks{};

    AnimationClip() = default;
    ~AnimationClip() = default;

    [[nodiscard]] bool valid() const noexcept
    {
        constexpr float eps = 1e-6f;

        if (!math::finiteFloat(durationSeconds) || !math::finiteFloat(ticksPerSecond))
        {
            return false;
        }

        if (durationSeconds <= eps || ticksPerSecond < 0.0f)
        {
            return false;
        }

        if (tracks.empty())
        {
            return false;
        }

        auto validVec3Track = [&](const std::vector<KeyframeVec3> &keys) {
            if (!std::ranges::is_sorted(keys, std::less<>{}, [](const KeyframeVec3 &key) {
                    return key.timeSeconds;
                }))
            {
                return false;
            }

            return std::ranges::all_of(keys, [&](const KeyframeVec3 &key) {
                return math::finiteFloat(key.timeSeconds) &&
                       key.timeSeconds >= 0.0f &&
                       key.timeSeconds <= durationSeconds + eps &&
                       math::finiteVec(key.value);
            });
        };

        auto validQuatTrack = [&](const std::vector<KeyframeQuat> &keys) {
            if (!std::ranges::is_sorted(keys, std::less<>{}, [](const KeyframeQuat &key) {
                    return key.timeSeconds;
                }))
            {
                return false;
            }

            return std::ranges::all_of(keys, [&](const KeyframeQuat &key) {
                return math::finiteFloat(key.timeSeconds) &&
                       key.timeSeconds >= 0.0f &&
                       key.timeSeconds <= durationSeconds + eps &&
                       math::finiteQuat(key.value) &&
                       glm::length(key.value) > eps;
            });
        };

        return std::ranges::all_of(tracks, [&](const BoneAnimationTrack &track) {
            auto hasAnyKeys = !track.translations.empty() || !track.rotations.empty() || !track.scales.empty();

            return track.boneIndex >= 0 &&
                   hasAnyKeys &&
                   validVec3Track(track.translations) &&
                   validQuatTrack(track.rotations) &&
                   validVec3Track(track.scales);
        });
    }
};

} // namespace nr::resource
