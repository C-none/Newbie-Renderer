export module nr.resource:skeletalAnimation;
import dependency.math;

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

    [[nodiscard]] bool isRoot() const noexcept;
};

struct Skeleton
{
    std::string name{};
    std::vector<Bone> bones{};

    Skeleton() = default;
    ~Skeleton() = default;

    [[nodiscard]] std::size_t boneCount() const noexcept;

    [[nodiscard]] std::size_t rootCount() const noexcept;

    [[nodiscard]] bool validateHierarchy() const noexcept;
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

    [[nodiscard]] bool valid() const noexcept;
};

} // namespace nr::resource
