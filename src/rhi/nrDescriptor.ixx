export module nr.rhi:descriptor;
import dependency.slang;
import dependency.vulkan;

import :slang;
import :resource;
import std;

namespace nr::rhi::detail
{
constexpr vk::DeviceSize kWholeBufferRange = std::numeric_limits<vk::DeviceSize>::max();

[[nodiscard]] std::optional<vk::DescriptorType> toVkDescriptorType(slang::BindingType bindingType);

[[nodiscard]] vk::ShaderStageFlags toVkShaderStageFlags(std::optional<SlangStage> stage);

[[nodiscard]] bool isUnboundedDescriptorCount(SlangInt descriptorCount);

[[nodiscard]] bool isInlineUniformByteCountValid(std::uint32_t byteCount);

template <typename T, T DefaultValue>
[[nodiscard]] std::uint32_t sanitizeCountOrSize(T value, bool invalidIfZero = true)
{
    if constexpr (std::is_signed_v<T>)
    {
        if (value <= (invalidIfZero ? 0 : -1) || value > static_cast<T>(std::numeric_limits<std::uint32_t>::max()))
            return DefaultValue;
    }
    else
    {
        if ((invalidIfZero && value == 0) || value == std::numeric_limits<T>::max() || value > std::numeric_limits<std::uint32_t>::max())
            return DefaultValue;
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t sanitizePushConstantSize(std::size_t byteSize);

[[nodiscard]] std::uint32_t sanitizeDescriptorCount(SlangInt descriptorCount);

[[nodiscard]] std::uint32_t sanitizeRangeOffset(SlangInt rangeOffset);

[[nodiscard]] std::uint32_t sanitizeFieldIndex(SlangInt fieldIndex);

[[nodiscard]] std::uint32_t sanitizeElementCount(std::size_t elementCount);

[[nodiscard]] std::optional<std::uint32_t> tryElementCount(std::size_t elementCount);

[[nodiscard]] std::optional<std::size_t> tryLayoutSize(std::size_t value);
}

export namespace nr::rhi
{

inline constexpr std::uint32_t kMaxPushConstantBytes = 128u;

struct CursorAddress
{
    std::size_t uniformOffset = 0;
    std::uint32_t bindingRangeIndex = 0;
    std::uint32_t bindingArrayIndex = 0;
};

enum class ShaderBindingPhase : unsigned
{
    Layout,
    DescriptorWrite,
    CommandRecord,
};

enum class ShaderBindingKind : unsigned
{
    None,
    Descriptor,
    PushConstant,
};

enum class ShaderDescriptorSemantic : unsigned
{
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformTexelBuffer,
    StorageTexelBuffer,
    UniformBuffer,
    StorageBuffer,
    DynamicUniformBuffer,
    DynamicStorageBuffer,
    InputAttachment,
    InlineUniformBlock,
    AccelerationStructure,
    Unsupported,
};

enum class RuntimeDescriptorArraySetPolicy : unsigned
{
    PreserveShaderSets,
    RequireSemanticMultiSet,
};

struct RuntimeDescriptorArraySetConvention
{
    std::uint32_t samplerSet = 0;
    std::uint32_t sampledImageSet = 1;
    std::uint32_t storageImageSet = 2;
    std::uint32_t bufferSet = 3;
    std::uint32_t accelerationStructureSet = 4;
};

[[nodiscard]] std::string_view shaderDescriptorSemanticName(ShaderDescriptorSemantic semantic) noexcept;

[[nodiscard]] ShaderDescriptorSemantic descriptorSemantic(vk::DescriptorType descriptorType) noexcept;

[[nodiscard]] bool supportsImmutableSampler(vk::DescriptorType descriptorType) noexcept;

[[nodiscard]] bool usesDynamicDescriptorOffset(vk::DescriptorType descriptorType) noexcept;

[[nodiscard]] std::optional<std::uint32_t> runtimeDescriptorArraySetFor(
    ShaderDescriptorSemantic semantic,
    const RuntimeDescriptorArraySetConvention &convention) noexcept;

struct DescriptorBindingInfo
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t descriptorCount = 1;
    bool isRuntimeSized = false;
    vk::DescriptorType descriptorType = vk::DescriptorType::eStorageBuffer;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    vk::DescriptorBindingFlags bindingFlags{};
    std::uint32_t bindingRangeIndex = 0;
    std::optional<std::uint32_t> expectedRuntimeSet{};
    std::string debugPath;

    [[nodiscard]] bool supportsVariableDescriptorCount() const noexcept;

    [[nodiscard]] bool isPartiallyBound() const noexcept;

    [[nodiscard]] bool isUpdateAfterBind() const noexcept;

    [[nodiscard]] ShaderDescriptorSemantic semantic() const noexcept;

    [[nodiscard]] bool supportsImmutableSampler() const noexcept;

    [[nodiscard]] bool usesDynamicDescriptorOffset() const noexcept;

    [[nodiscard]] bool followsExpectedRuntimeSet() const noexcept;

    [[nodiscard]] bool hasPhase(ShaderBindingPhase phase) const noexcept;
};

struct DescriptorSetLayoutInfo
{
    std::uint32_t set = 0;
    std::vector<DescriptorBindingInfo> bindings;
};

struct PushConstantRangeInfo
{
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    std::uint32_t bindingRangeIndex = 0;
    std::string debugPath;

    [[nodiscard]] bool hasPhase(ShaderBindingPhase phase) const noexcept;
};

struct ShaderDescriptorAbiBinding
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t descriptorCount = 1;
    bool isRuntimeSized = false;
    vk::DescriptorType descriptorType = vk::DescriptorType::eStorageBuffer;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    vk::DescriptorBindingFlags bindingFlags{};

    [[nodiscard]] friend auto operator<=>(const ShaderDescriptorAbiBinding &, const ShaderDescriptorAbiBinding &) noexcept = default;
};

struct ShaderPushConstantAbiRange
{
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;

    [[nodiscard]] friend auto operator<=>(const ShaderPushConstantAbiRange &, const ShaderPushConstantAbiRange &) noexcept = default;
};

struct ShaderLayoutAbiSignature
{
    std::vector<ShaderDescriptorAbiBinding> descriptorBindings;
    std::vector<ShaderPushConstantAbiRange> pushConstantRanges;

    [[nodiscard]] friend bool operator==(const ShaderLayoutAbiSignature &, const ShaderLayoutAbiSignature &) noexcept = default;
};

struct ShaderBindingReflection
{
    ShaderBindingKind kind = ShaderBindingKind::None;
    std::optional<DescriptorBindingInfo> descriptorBinding{};
    std::optional<PushConstantRangeInfo> pushConstantRange{};

    [[nodiscard]] bool hasPhase(ShaderBindingPhase phase) const noexcept;

    [[nodiscard]] std::optional<ShaderDescriptorSemantic> descriptorSemantic() const noexcept;

    [[nodiscard]] bool supportsImmutableSampler() const noexcept;

    [[nodiscard]] bool usesDynamicDescriptorOffset() const noexcept;
};

struct BufferDescriptorWrite
{
    vk::Buffer buffer{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = detail::kWholeBufferRange;
};

struct TexelBufferDescriptorWrite
{
    vk::BufferView view{};
};

struct ImageDescriptorWrite
{
    vk::ImageView imageView{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::Sampler sampler{};
};

struct AccelerationStructureDescriptorWrite
{
    vk::AccelerationStructureKHR accelerationStructure{};
};

struct InlineUniformDescriptorWrite
{
    std::vector<std::uint8_t> data;
};

using DescriptorWritePayload =
    std::variant<BufferDescriptorWrite, TexelBufferDescriptorWrite, ImageDescriptorWrite, AccelerationStructureDescriptorWrite, InlineUniformDescriptorWrite>;

struct DescriptorWriteRequest
{
    DescriptorBindingInfo binding;
    std::uint32_t arrayElement = 0;
    DescriptorWritePayload payload;
    bool forceWrite = false;
};

struct DescriptorWriteSlotKey
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t arrayElement = 0;
    vk::DescriptorType descriptorType = vk::DescriptorType::eStorageBuffer;

    [[nodiscard]] friend auto operator<=>(const DescriptorWriteSlotKey &, const DescriptorWriteSlotKey &) noexcept = default;
};

struct BufferDescriptorPayloadKey
{
    vk::Buffer buffer{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = detail::kWholeBufferRange;

    [[nodiscard]] friend bool operator==(const BufferDescriptorPayloadKey &, const BufferDescriptorPayloadKey &) noexcept = default;
};

struct TexelBufferDescriptorPayloadKey
{
    vk::BufferView view{};

    [[nodiscard]] friend bool operator==(const TexelBufferDescriptorPayloadKey &, const TexelBufferDescriptorPayloadKey &) noexcept = default;
};

struct ImageDescriptorPayloadKey
{
    vk::ImageView imageView{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::Sampler sampler{};

    [[nodiscard]] friend bool operator==(const ImageDescriptorPayloadKey &, const ImageDescriptorPayloadKey &) noexcept = default;
};

struct AccelerationStructureDescriptorPayloadKey
{
    vk::AccelerationStructureKHR accelerationStructure{};

    [[nodiscard]] friend bool operator==(const AccelerationStructureDescriptorPayloadKey &, const AccelerationStructureDescriptorPayloadKey &) noexcept = default;
};

struct InlineUniformDescriptorPayloadKey
{
    std::vector<std::uint8_t> data;

    [[nodiscard]] friend bool operator==(const InlineUniformDescriptorPayloadKey &, const InlineUniformDescriptorPayloadKey &) noexcept = default;
};

using DescriptorWritePayloadKey =
    std::variant<BufferDescriptorPayloadKey, TexelBufferDescriptorPayloadKey, ImageDescriptorPayloadKey, AccelerationStructureDescriptorPayloadKey, InlineUniformDescriptorPayloadKey>;

class DescriptorWriteCache
{
  public:
    void clear() noexcept;

    [[nodiscard]] std::uint64_t version() const noexcept;

    [[nodiscard]] std::vector<DescriptorWriteRequest> filterChanged(std::span<const DescriptorWriteRequest> writeRequests) const;

    void commit(std::span<const DescriptorWriteRequest> writeRequests);

  private:
    std::map<DescriptorWriteSlotKey, DescriptorWritePayloadKey> payloadsBySlot_{};
    std::uint64_t version_ = 0;
};

[[nodiscard]] std::vector<DescriptorWriteRequest> filterChangedDescriptorWrites(
    DescriptorWriteCache &cache,
    std::span<const DescriptorWriteRequest> writeRequests);

void commitDescriptorWrites(
    DescriptorWriteCache &cache,
    std::span<const DescriptorWriteRequest> writeRequests);

class ShaderBindingSet
{
  public:
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] vk::DescriptorSet raw() const noexcept;
    [[nodiscard]] std::uint32_t setIndex() const noexcept;
    [[nodiscard]] std::uint32_t descriptorCapacity(const DescriptorBindingInfo &bindingInfo) const noexcept;

  private:
    friend class ShaderBindingPool;
    vk::DescriptorSet set_{};
    std::uint32_t setIndex_ = 0;
    std::map<std::uint32_t, std::uint32_t> allocatedDescriptorCountByBinding_{};
};

class ShaderDescriptorLayout;
class ShaderCursor;
class CursorPipelineLayout;

enum class ShaderBindingPoolPolicy : unsigned
{
    FrameReset,
    PersistentFreeable,
};

struct ShaderBindingPoolConfig
{
    std::uint32_t maxSets = 64;
    std::uint32_t defaultVariableDescriptorCount = 1024;
    ShaderBindingPoolPolicy policy = ShaderBindingPoolPolicy::FrameReset;
    vk::DescriptorPoolCreateFlags extraFlags{};
};

struct DescriptorBindingPolicy
{
    bool enableUpdateAfterBind = true;
    bool enablePartiallyBound = true;
    bool enableVariableDescriptorCount = true;
    std::uint32_t defaultRuntimeDescriptorCount = 1024;
    RuntimeDescriptorArraySetPolicy runtimeArraySetPolicy = RuntimeDescriptorArraySetPolicy::RequireSemanticMultiSet;
    RuntimeDescriptorArraySetConvention runtimeArraySetConvention{};
};

class ShaderBindingPool
{
  public:
    [[nodiscard]] static ShaderBindingPool create(
        const vk::raii::Device &device,
        const ShaderDescriptorLayout &descriptorLayout,
        ShaderBindingPoolConfig config = {});

    [[nodiscard]] ShaderBindingSet allocate(vk::DescriptorSetLayout descriptorSetLayout, std::uint32_t setIndex, std::optional<std::uint32_t> variableDescriptorCount = std::nullopt) const;

    void update(const ShaderBindingSet &set, std::span<const DescriptorWriteRequest> writeRequests) const;

    void update(const ShaderBindingSet &set, const DescriptorWriteRequest &writeRequest) const;

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    vk::raii::DescriptorPool pool_ = {nullptr};
    std::map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>> variableDescriptorCapBySetAndBinding_{};
};

struct LogicalResourceDescriptorWrite
{
    // Reflection provides descriptor set/binding/type through DescriptorBindingInfo.
    // This logical payload carries runtime resource identity plus optional per-descriptor state.
    std::uint64_t logicalResourceId = 0;
    std::string debugName{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eGeneral;
    vk::Sampler sampler{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = std::numeric_limits<vk::DeviceSize>::max();
};

using ShaderBindingRecordPayload =
    std::variant<
        BufferDescriptorWrite,
        TexelBufferDescriptorWrite,
        ImageDescriptorWrite,
        AccelerationStructureDescriptorWrite,
        InlineUniformDescriptorWrite,
        LogicalResourceDescriptorWrite>;

struct ShaderBindingRecord
{
    DescriptorBindingInfo binding;
    std::uint32_t arrayElement = 0;
    ShaderBindingRecordPayload payload;
    bool forceWrite = false;
};

struct PushConstantWriteRecord
{
    PushConstantRangeInfo range;
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> data;
};

class ShaderBindingSnapshot
{
  public:
    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] std::size_t descriptorWriteCount() const noexcept;

    [[nodiscard]] std::size_t pushConstantWriteCount() const noexcept;

    [[nodiscard]] std::span<const ShaderBindingRecord> descriptorWrites() const noexcept;

    [[nodiscard]] std::span<const PushConstantWriteRecord> pushConstantWrites() const noexcept;

    void forceDescriptorWrites() noexcept;

  private:
    friend class ShaderCursor;
    std::vector<ShaderBindingRecord> descriptorWrites_{};
    std::vector<PushConstantWriteRecord> pushConstantWrites_{};
};

using LogicalDescriptorResolver = std::function<std::optional<DescriptorWritePayload>(
    const LogicalResourceDescriptorWrite &logicalResource,
    const DescriptorBindingInfo &binding,
    std::uint32_t arrayElement)>;

[[nodiscard]] std::vector<DescriptorWriteRequest> resolveDescriptorWriteRequests(
    const ShaderBindingSnapshot &snapshot,
    LogicalDescriptorResolver logicalResolver = {});

class ShaderDescriptorLayout;

class ShaderCursor
{
  public:
    // Cursor guide:
    // - The cursor carries reflection type info, a logical write address, and shared mutable binding state.
    // - Copied sub-cursors write into one coherent binding snapshot.
    // - bindingReflection() classifies Vulkan shader-interface semantics without touching GPU objects.
    // - setObject(...) records descriptor-backed resources (or logical graph references).
    // - setData(...) records push constants or inline uniform bytes.
    // - snapshot() captures a stable per-pass binding view for execute-time replay.

    ShaderCursor() = default;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] CursorAddress address() const noexcept;

    [[nodiscard]] slang::TypeLayoutReflection *typeLayout() const noexcept;

    [[nodiscard]] ShaderCursor field(std::string_view fieldName) const;

    [[nodiscard]] bool hasField(std::string_view fieldName) const;

    [[nodiscard]] ShaderCursor element(std::uint32_t index) const;

    [[nodiscard]] ShaderCursor getPath(std::string_view path) const;

    [[nodiscard]] std::optional<DescriptorBindingInfo> descriptorBinding() const;

    [[nodiscard]] std::optional<PushConstantRangeInfo> pushConstantRange() const;

    [[nodiscard]] ShaderBindingReflection bindingReflection() const;

    [[nodiscard]] ShaderBindingKind bindingKind() const;

    [[nodiscard]] bool hasBindingPhase(ShaderBindingPhase phase) const;

    [[nodiscard]] std::optional<ShaderDescriptorSemantic> descriptorSemantic() const;

    [[nodiscard]] bool supportsImmutableSampler() const;

    [[nodiscard]] bool usesDynamicDescriptorOffset() const;

    [[nodiscard]] std::optional<SlangImmutableSamplerBinding> makeImmutableSamplerBinding(SlangSamplerDesc samplerDesc) const;

    [[nodiscard]] std::optional<std::uint32_t> bindingDescriptorCount() const;

    [[nodiscard]] bool referencesRuntimeDescriptorArray() const;

    [[nodiscard]] bool setData(std::span<const std::uint8_t> bytes) const;

    template <typename T>
    requires(std::is_trivially_copyable_v<std::remove_cvref_t<T>>)
    [[nodiscard]] bool setData(const T &value) const
    {
        auto bytes = std::as_bytes(std::span{&value, 1});
        auto *raw = reinterpret_cast<const std::uint8_t *>(bytes.data());
        return setData(std::span<const std::uint8_t>{raw, bytes.size()});
    }

    [[nodiscard]] bool setObject(
        const Buffer &buffer,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize) const;

    [[nodiscard]] bool setObject(vk::BufferView view) const;

    [[nodiscard]] bool setObject(
        Buffer &buffer,
        vk::Format format,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize,
        std::string_view viewName = {}) const;

    [[nodiscard]] bool setObject(
        const Image &image,
        vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) const;

    [[nodiscard]] bool setObject(vk::Sampler sampler) const;

    [[nodiscard]] bool setObject(
        const Image &image,
        vk::Sampler sampler,
        vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) const;

    [[nodiscard]] bool setObject(vk::AccelerationStructureKHR accelerationStructure) const;

    [[nodiscard]] bool setObject(const LogicalResourceDescriptorWrite &logicalResource) const;

    [[nodiscard]] ShaderBindingSnapshot snapshot() const;

    void clearSnapshot() const;

    // Cursor layout/type reflection helpers for runtime binding.
    [[nodiscard]] slang::TypeReflection::Kind kind() const noexcept;

    [[nodiscard]] std::string typeName() const;

    [[nodiscard]] std::uint32_t fieldCount() const noexcept;

    [[nodiscard]] std::optional<std::uint32_t> elementCount() const;

    [[nodiscard]] std::optional<std::size_t> size(slang::ParameterCategory category = slang::ParameterCategory::Uniform) const;

    [[nodiscard]] std::optional<std::size_t> stride(slang::ParameterCategory category = slang::ParameterCategory::Uniform) const;

    [[nodiscard]] std::optional<std::int32_t> alignment(slang::ParameterCategory category = slang::ParameterCategory::Uniform) const;

    [[nodiscard]] std::vector<slang::ParameterCategory> categories() const;

    [[nodiscard]] std::optional<SlangResourceShape> resourceShape() const;

    [[nodiscard]] std::optional<SlangResourceAccess> resourceAccess() const;

    [[nodiscard]] slang::TypeReflection *resourceResultType() const noexcept;

    [[nodiscard]] std::optional<std::uint32_t> resourceResultElementCount() const;

    // Slang-style convenience accessors:
    // - cursor["field"] -> field lookup
    // - cursor[index]   -> array/vector/matrix/struct element lookup
    [[nodiscard]] ShaderCursor operator[](std::string_view fieldName) const
    {
        return field(fieldName);
    }

    [[nodiscard]] ShaderCursor operator[](const char *fieldName) const
    {
        return field(fieldName ? std::string_view(fieldName) : std::string_view{});
    }

    template <typename TIndex>
    requires(std::integral<std::remove_cvref_t<TIndex>> && !std::same_as<std::remove_cvref_t<TIndex>, bool>)
    [[nodiscard]] ShaderCursor operator[](TIndex index) const
    {
        return element(static_cast<std::uint32_t>(index));
    }

  private:
    friend class ShaderDescriptorLayout;

    struct SharedBindingState
    {
        std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, ShaderBindingRecord> descriptorWritesByBinding{};
        std::map<std::tuple<std::uint32_t, std::uint32_t>, PushConstantWriteRecord> pushConstantWritesByRangeAndOffset{};

        void writeDescriptor(ShaderBindingRecord record);

        void writePushConstant(PushConstantWriteRecord record);

        [[nodiscard]] ShaderBindingSnapshot snapshot() const;

        void clear();
    };

    struct RootField
    {
        slang::TypeLayoutReflection *typeLayout = nullptr;
        CursorAddress address{};
        std::string debugPath;
    };

    ShaderCursor(const ShaderDescriptorLayout &layout, RootField field, std::shared_ptr<SharedBindingState> bindingState);

    explicit ShaderCursor(const ShaderDescriptorLayout &layout);

    [[nodiscard]] static vk::DeviceSize normalizeBufferRange(const Buffer &buffer, vk::DeviceSize offset, vk::DeviceSize range);

    [[nodiscard]] static bool acceptsDescriptorType(vk::DescriptorType descriptorType, std::initializer_list<vk::DescriptorType> allowed);

    [[nodiscard]] static std::string describeDescriptorBinding(const DescriptorBindingInfo &bindingInfo);

    [[nodiscard]] static std::string describeDescriptorTypes(std::initializer_list<vk::DescriptorType> descriptorTypes);

    [[nodiscard]] static std::string describeRootFields(const ShaderDescriptorLayout &layout);

    [[nodiscard]] static std::string describeStructFields(slang::TypeLayoutReflection *typeLayout);

    [[nodiscard]] std::string debugSummary() const;

    void assertValidCursor(std::string_view operation) const;

    void assertWritableCursor(std::string_view operation) const;

    [[nodiscard]] bool writeDescriptorRecord(
        ShaderBindingRecordPayload payload,
        std::initializer_list<vk::DescriptorType> allowedTypes,
        std::optional<std::uint32_t> explicitArrayElement = std::nullopt) const;

    template <typename FieldLayout>
    [[nodiscard]] ShaderCursor fieldCursorFromLayout(
        FieldLayout &fieldLayout,
        std::uint32_t fieldIndex,
        std::string debugPath) const;

    [[nodiscard]] const ShaderDescriptorLayout &layoutRef() const;

    std::optional<std::reference_wrapper<const ShaderDescriptorLayout>> layout_{};
    slang::TypeLayoutReflection *typeLayout_ = nullptr;
    CursorAddress address_{};
    bool isRoot_ = false;
    std::string debugPath_{};
    std::shared_ptr<SharedBindingState> bindingState_{};
};

class ShaderDescriptorLayout
{
  public:
        // System guide:
        // - Input: SlangProgram reflection.
        // - Output:
        //   1) descriptor set layout metadata (set/binding/type/count/stageFlags)
        //   2) push constant ranges (for command recording time via vkCmdPushConstants)
        //   3) Vulkan shader-binding phase metadata for cursor queries
        //   4) root field map for cursor traversal.
        //
        // Pseudocode:
        //   layout = ShaderDescriptorLayout::create(program)
        //   root = layout.rootCursor()
        //   cursor = root.getPath("material.albedo")
        //   binding = cursor.descriptorBinding() // -> set/binding/type
        //
        // PushConstant timing note:

    [[nodiscard]] static ShaderDescriptorLayout create(const SlangProgram &program, DescriptorBindingPolicy policy = {});

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] std::span<const DescriptorSetLayoutInfo> descriptorSets() const noexcept;

    [[nodiscard]] std::vector<vk::DescriptorSetLayoutBinding> makeVkSetLayoutBindings(std::uint32_t setIndex) const;

    [[nodiscard]] std::vector<vk::DescriptorBindingFlags> makeVkSetLayoutBindingFlags(std::uint32_t setIndex) const;

    [[nodiscard]] bool requiresUpdateAfterBindPool() const;

    [[nodiscard]] std::span<const PushConstantRangeInfo> pushConstantRanges() const noexcept;

    [[nodiscard]] std::optional<PushConstantRangeInfo> pushConstantRange(const ShaderCursor &cursor) const;

    [[nodiscard]] ShaderLayoutAbiSignature abiSignature() const;

    [[nodiscard]] std::vector<vk::PushConstantRange> makeVkPushConstantRanges() const;

    [[nodiscard]] ShaderCursor rootCursor() const;

  private:
    friend class ShaderCursor;

    [[nodiscard]] std::optional<ShaderCursor::RootField> findRootField(std::string_view fieldName) const;

    [[nodiscard]] std::optional<DescriptorBindingInfo> findBindingByRangeIndex(std::uint32_t bindingRangeIndex) const;

    bool isValid_ = false;
    std::map<std::string, ShaderCursor::RootField> rootFields_;
    std::map<std::uint32_t, DescriptorBindingInfo> bindingByRangeIndex_;
    std::map<std::uint32_t, PushConstantRangeInfo> pushConstantByRangeIndex_;
    std::map<std::tuple<std::uint32_t, std::uint32_t>, PushConstantRangeInfo> pushConstantByOffsetAndSize_;
    std::map<std::tuple<std::uint32_t, std::uint32_t>, DescriptorBindingInfo> bindingBySetAndBinding_;
    std::vector<DescriptorSetLayoutInfo> descriptorSets_;
    std::vector<PushConstantRangeInfo> pushConstantRanges_;
};

[[nodiscard]] bool shaderLayoutAbiEquivalent(const ShaderLayoutAbiSignature &lhs, const ShaderLayoutAbiSignature &rhs) noexcept;

[[nodiscard]] std::string describeShaderLayoutAbiDifference(const ShaderLayoutAbiSignature &baseline, const ShaderLayoutAbiSignature &variant);

void assertShaderLayoutAbiStable(
    const SlangProgram &baselineProgram,
    const SlangProgram &variantProgram,
    DescriptorBindingPolicy policy = {},
    std::string_view debugName = {});

[[nodiscard]] std::vector<ShaderBindingSet> allocateBindingSetsForLayout(const CursorPipelineLayout &layout, ShaderBindingPool &pool);

[[nodiscard]] std::vector<ShaderBindingSet> allocateBindingSetsForLayout(
    const CursorPipelineLayout &layout,
    ShaderBindingPool &pool,
    const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet);

void pushConstantsToCommandBuffer(
    const vk::raii::CommandBuffer& commandBuffer,
    const CursorPipelineLayout &layout,
    const ShaderBindingSnapshot &snapshot);











template <typename FieldLayout>
[[nodiscard]] ShaderCursor ShaderCursor::fieldCursorFromLayout(
    FieldLayout &fieldLayout,
    std::uint32_t fieldIndex,
    std::string debugPath) const
{
    auto *fieldTypeLayout = fieldLayout.getTypeLayout();
    nrAssert(
        fieldTypeLayout != nullptr,
        std::format(
            "ShaderCursor::fieldCursorFromLayout requires a non-null field type layout. fieldIndex={}, debugPath='{}', cursor={}",
            fieldIndex,
            debugPath,
            debugSummary()));

    ShaderCursor next = *this;
    next.typeLayout_ = fieldTypeLayout;
    next.address_.uniformOffset += fieldLayout.getOffset();
    next.address_.bindingRangeIndex += detail::sanitizeRangeOffset(typeLayout_->getFieldBindingRangeOffset(static_cast<SlangInt>(fieldIndex)));
    next.isRoot_ = false;
    next.debugPath_ = std::move(debugPath);
    return next;
}








































[[nodiscard]] std::vector<DescriptorWriteRequest> resolveDescriptorWriteRequests(
    const ShaderBindingSnapshot &snapshot,
    LogicalDescriptorResolver logicalResolver);

} // namespace nr::rhi
