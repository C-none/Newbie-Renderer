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

[[nodiscard]] bool isUnboundedDescriptorCount(SlangInt descriptorCount);

[[nodiscard]] bool isInlineUniformByteCountValid(std::uint32_t byteCount);

[[nodiscard]] std::uint32_t sanitizePushConstantSize(std::size_t byteSize);

[[nodiscard]] std::uint32_t sanitizeDescriptorCount(SlangInt descriptorCount);

[[nodiscard]] std::uint32_t sanitizeRangeOffset(SlangInt rangeOffset);

[[nodiscard]] std::uint32_t sanitizeFieldIndex(SlangInt fieldIndex);

[[nodiscard]] std::uint32_t sanitizeElementCount(std::size_t elementCount);

[[nodiscard]] std::optional<std::uint32_t> tryElementCount(std::size_t elementCount);

[[nodiscard]] std::optional<std::size_t> tryLayoutSize(std::size_t value);
} // namespace nr::rhi::detail

export namespace nr::rhi
{

inline constexpr std::uint32_t kMaxPushConstantBytes = 128u;

struct CursorAddress
{
    std::size_t uniformOffset = 0;
    std::uint32_t bindingRangeIndex = 0;
    std::uint32_t bindingArrayIndex = 0;
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

[[nodiscard]] std::string_view shaderDescriptorSemanticName(ShaderDescriptorSemantic semantic) noexcept;

[[nodiscard]] ShaderDescriptorSemantic descriptorSemantic(vk::DescriptorType descriptorType) noexcept;

[[nodiscard]] bool supportsImmutableSampler(vk::DescriptorType descriptorType) noexcept;

[[nodiscard]] std::optional<std::uint32_t> runtimeDescriptorArraySetFor(ShaderDescriptorSemantic semantic) noexcept;

struct DescriptorBindingInfo
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t descriptorCount = 1;
    bool isRuntimeSized = false;
    bool usesImmutableSampler = false;
    vk::DescriptorType descriptorType = vk::DescriptorType::eStorageBuffer;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    vk::DescriptorBindingFlags bindingFlags{};
    std::uint32_t bindingRangeIndex = 0;
    std::string debugPath;

    [[nodiscard]] bool supportsVariableDescriptorCount() const noexcept;

    [[nodiscard]] bool isPartiallyBound() const noexcept;

    [[nodiscard]] ShaderDescriptorSemantic semantic() const noexcept;

    [[nodiscard]] bool supportsImmutableSampler() const noexcept;

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

    [[nodiscard]] friend auto operator<=>(const ShaderDescriptorAbiBinding &,
                                          const ShaderDescriptorAbiBinding &) noexcept = default;
};

struct ShaderPushConstantAbiRange
{
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;

    [[nodiscard]] friend auto operator<=>(const ShaderPushConstantAbiRange &,
                                          const ShaderPushConstantAbiRange &) noexcept = default;
};

struct ShaderLayoutAbiSignature
{
    std::vector<ShaderDescriptorAbiBinding> descriptorBindings;
    std::vector<ShaderPushConstantAbiRange> pushConstantRanges;

    [[nodiscard]] friend bool operator==(const ShaderLayoutAbiSignature &,
                                         const ShaderLayoutAbiSignature &) noexcept = default;
};

struct BufferDescriptorWrite
{
    vk::Buffer buffer{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = detail::kWholeBufferRange;

    [[nodiscard]] friend bool operator==(const BufferDescriptorWrite &, const BufferDescriptorWrite &) noexcept =
        default;
};

struct TexelBufferDescriptorWrite
{
    vk::BufferView view{};

    [[nodiscard]] friend bool operator==(const TexelBufferDescriptorWrite &,
                                         const TexelBufferDescriptorWrite &) noexcept = default;
};

struct ImageDescriptorWrite
{
    vk::ImageView imageView{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::Sampler sampler{};

    [[nodiscard]] friend bool operator==(const ImageDescriptorWrite &, const ImageDescriptorWrite &) noexcept =
        default;
};

struct AccelerationStructureDescriptorWrite
{
    vk::AccelerationStructureKHR accelerationStructure{};

    [[nodiscard]] friend bool operator==(const AccelerationStructureDescriptorWrite &,
                                         const AccelerationStructureDescriptorWrite &) noexcept = default;
};

struct InlineUniformDescriptorWrite
{
    std::vector<std::uint8_t> data;

    [[nodiscard]] friend bool operator==(const InlineUniformDescriptorWrite &,
                                         const InlineUniformDescriptorWrite &) noexcept = default;
};

using DescriptorWritePayload = std::variant<BufferDescriptorWrite, TexelBufferDescriptorWrite, ImageDescriptorWrite,
                                            AccelerationStructureDescriptorWrite, InlineUniformDescriptorWrite>;

struct DescriptorWriteRequest
{
    DescriptorBindingInfo binding;
    std::uint32_t arrayElement = 0;
    DescriptorWritePayload payload;
    bool forceWrite = false;
};

class DescriptorWriteCache
{
  public:
    void clear() noexcept;

    [[nodiscard]] std::vector<DescriptorWriteRequest> filterChanged(
        std::span<const DescriptorWriteRequest> writeRequests) const;

    void commit(std::span<const DescriptorWriteRequest> writeRequests);

  private:
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, vk::DescriptorType>, DescriptorWritePayload>
        payloadsBySlot_{};
};

/**
 * @brief Move-only descriptor-set ownership allocated from a ShaderBindingPool.
 *
 * The issuing pool must outlive the set. Destruction returns the set to that pool.
 */
class ShaderBindingSet
{
  public:
    ShaderBindingSet() = default;
    ShaderBindingSet(const ShaderBindingSet &) = delete;
    ShaderBindingSet &operator=(const ShaderBindingSet &) = delete;
    ShaderBindingSet(ShaderBindingSet &&) noexcept = default;
    ShaderBindingSet &operator=(ShaderBindingSet &&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] vk::DescriptorSet raw() const noexcept;
    [[nodiscard]] std::uint32_t setIndex() const noexcept;
    [[nodiscard]] std::uint32_t descriptorCapacity(const DescriptorBindingInfo &bindingInfo) const noexcept;

  private:
    friend class ShaderBindingPool;
    vk::raii::DescriptorSet set_ = {nullptr};
    vk::DescriptorPool descriptorPool_{};
    std::uint32_t setIndex_ = 0;
    std::map<std::uint32_t, std::uint32_t> allocatedDescriptorCountByBinding_{};
};

class ShaderDescriptorLayout;
class ShaderCursor;

struct DescriptorBindingPolicy
{
    std::uint32_t defaultRuntimeDescriptorCount = 1024;
};

class ShaderBindingPool
{
  public:
    // Allocation and descriptor updates are host-mutable and externally synchronized.
    // The renderer performs them during serial prepare before parallel command recording.
    // maxBindingGroups counts complete allocations of every reflected descriptor set in the layout;
    // the Vulkan pool expands that logical budget by the number of non-empty set layouts.
    [[nodiscard]] static ShaderBindingPool create(const vk::raii::Device &device,
                                                  const ShaderDescriptorLayout &descriptorLayout,
                                                  std::uint32_t maxBindingGroups);

    [[nodiscard]] ShaderBindingSet allocate(vk::DescriptorSetLayout descriptorSetLayout, std::uint32_t setIndex,
                                            std::optional<std::uint32_t> variableDescriptorCount = std::nullopt);

    void update(const ShaderBindingSet &set, std::span<const DescriptorWriteRequest> writeRequests);

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    vk::raii::DescriptorPool pool_ = {nullptr};
    std::map<std::tuple<std::uint32_t, std::uint32_t>, DescriptorBindingInfo> bindings_{};
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
    std::variant<BufferDescriptorWrite, TexelBufferDescriptorWrite, ImageDescriptorWrite,
                 AccelerationStructureDescriptorWrite, InlineUniformDescriptorWrite, LogicalResourceDescriptorWrite>;

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

    [[nodiscard]] ShaderBindingSnapshot withForcedDescriptorWrites() const;

  private:
    friend class ShaderCursor;

    struct Storage
    {
        std::vector<ShaderBindingRecord> descriptorWrites{};
        std::vector<PushConstantWriteRecord> pushConstantWrites{};
    };

    std::shared_ptr<const Storage> storage_{};
};

using LogicalDescriptorResolver = std::function<std::optional<DescriptorWritePayload>(
    const LogicalResourceDescriptorWrite &logicalResource, const DescriptorBindingInfo &binding,
    std::uint32_t arrayElement)>;

[[nodiscard]] std::vector<DescriptorWriteRequest> resolveDescriptorWriteRequests(
    const ShaderBindingSnapshot &snapshot, LogicalDescriptorResolver logicalResolver = {});

class ShaderCursor
{
  public:
    // Cursor guide:
    // - The cursor carries reflection type info, a logical write address, and shared mutable binding state.
    // - Copied sub-cursors write into one coherent binding snapshot.
    // - Cursors and their snapshots are collected by one prepare thread; shared write state is not synchronized.
    // - A cursor must not outlive the ShaderDescriptorLayout that created it.
    // - Binding queries classify Vulkan shader-interface semantics without touching GPU objects.
    // - setObject(...) records descriptor-backed resources (or logical graph references).
    // - setData(...) records push constants or inline uniform bytes.
    // - beginRecording()/takeSnapshot() delimit one stable per-pass binding view for execute-time replay.

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

    [[nodiscard]] ShaderBindingKind bindingKind() const;

    [[nodiscard]] std::optional<ShaderDescriptorSemantic> descriptorSemantic() const;

    [[nodiscard]] std::optional<SlangImmutableSamplerBinding> makeImmutableSamplerBinding(
        SlangSamplerDesc samplerDesc) const;

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

    [[nodiscard]] bool setObject(const Buffer &buffer, vk::DeviceSize offset = 0,
                                 vk::DeviceSize range = vk::WholeSize) const;

    [[nodiscard]] bool setObject(vk::BufferView view) const;

    [[nodiscard]] bool setObject(Buffer &buffer, vk::Format format, vk::DeviceSize offset = 0,
                                 vk::DeviceSize range = vk::WholeSize, std::string_view viewName = {}) const;

    [[nodiscard]] bool setObject(const Image &image,
                                 vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) const;

    [[nodiscard]] bool setObject(vk::Sampler sampler) const;

    [[nodiscard]] bool setObject(const Image &image, vk::Sampler sampler,
                                 vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal) const;

    [[nodiscard]] bool setObject(vk::AccelerationStructureKHR accelerationStructure) const;

    [[nodiscard]] bool setObject(LogicalResourceDescriptorWrite logicalResource) const;

    void beginRecording() const;

    [[nodiscard]] ShaderBindingSnapshot takeSnapshot() const;

    // Slang-style convenience accessors:
    // - cursor["field"] -> field lookup
    // - cursor[index]   -> array/vector/matrix/struct element lookup
    [[nodiscard]] ShaderCursor operator[](std::string_view fieldName) const
    {
        return field(fieldName);
    }

    template <typename TIndex>
        requires(std::integral<std::remove_cvref_t<TIndex>> && !std::same_as<std::remove_cvref_t<TIndex>, bool>)
    [[nodiscard]] ShaderCursor operator[](TIndex index) const
    {
        if constexpr (std::signed_integral<std::remove_cvref_t<TIndex>>)
        {
            nrAssert(index >= 0, "ShaderCursor array index must not be negative.");
        }
        nrAssert(std::in_range<std::uint32_t>(index), "ShaderCursor array index exceeds uint32.");
        return element(static_cast<std::uint32_t>(index));
    }

  private:
    friend class ShaderDescriptorLayout;

    struct RootField
    {
        slang::TypeLayoutReflection *typeLayout = nullptr;
        CursorAddress address{};
        std::string debugPath;
    };

    template <typename TRecord> struct EpochStampedRecord
    {
        std::uint64_t epoch = 0;
        TRecord record;
    };

    struct SharedBindingState
    {
        using DescriptorWriteKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>;
        using PushConstantWriteKey = std::tuple<std::uint32_t, std::uint32_t>;

        std::map<DescriptorWriteKey, EpochStampedRecord<ShaderBindingRecord>> descriptorWritesByBinding{};
        std::map<PushConstantWriteKey, EpochStampedRecord<PushConstantWriteRecord>>
            pushConstantWritesByRangeAndOffset{};
        std::map<std::string, RootField, std::less<>> resolvedRootPaths{};
        std::uint64_t epoch = 0;
        bool recording = false;

        void beginRecording();

        void assertRecording(std::string_view operation) const;

        void writeDescriptor(ShaderBindingRecord record);

        void writePushConstant(PushConstantWriteRecord record);

        [[nodiscard]] std::pair<std::vector<ShaderBindingRecord>, std::vector<PushConstantWriteRecord>>
        takeRecords();
    };

    ShaderCursor(const ShaderDescriptorLayout &layout, RootField field,
                 std::shared_ptr<SharedBindingState> bindingState);

    explicit ShaderCursor(const ShaderDescriptorLayout &layout);

    [[nodiscard]] static vk::DeviceSize normalizeBufferRange(const Buffer &buffer, vk::DeviceSize offset,
                                                             vk::DeviceSize range);

    [[nodiscard]] static bool acceptsDescriptorType(vk::DescriptorType descriptorType,
                                                    std::initializer_list<vk::DescriptorType> allowed);

    [[nodiscard]] static std::string describeDescriptorBinding(const DescriptorBindingInfo &bindingInfo);

    [[nodiscard]] static std::string describeDescriptorTypes(std::initializer_list<vk::DescriptorType> descriptorTypes);

    [[nodiscard]] static std::string describeRootFields(const ShaderDescriptorLayout &layout);

    [[nodiscard]] static std::string describeStructFields(slang::TypeLayoutReflection *typeLayout);

    [[nodiscard]] std::string debugSummary() const;

    void assertValidCursor(std::string_view operation) const;

    void assertWritableCursor(std::string_view operation) const;

    [[nodiscard]] bool writeDescriptorRecord(ShaderBindingRecordPayload payload,
                                             std::initializer_list<vk::DescriptorType> allowedTypes,
                                             std::optional<std::uint32_t> explicitArrayElement = std::nullopt) const;

    template <typename FieldLayout>
    [[nodiscard]] ShaderCursor fieldCursorFromLayout(FieldLayout &fieldLayout, std::uint32_t fieldIndex,
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
    //   3) root field map for cursor traversal.
    //
    // Pseudocode:
    //   layout = ShaderDescriptorLayout::create(program)
    //   root = layout.rootCursor()
    //   cursor = root.getPath("material.albedo")
    //   binding = cursor.descriptorBinding() // -> set/binding/type
    //
    // PushConstant timing note:
    // The layout retains the source SlangProgram so its reflection pointers remain valid.

    [[nodiscard]] static ShaderDescriptorLayout create(const SlangProgram &program,
                                                       DescriptorBindingPolicy policy = {},
                                                       std::span<const SlangImmutableSamplerBinding> immutableSamplers = {});

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] std::span<const DescriptorSetLayoutInfo> descriptorSets() const noexcept;

    [[nodiscard]] std::vector<vk::DescriptorSetLayoutBinding> makeVkSetLayoutBindings(std::uint32_t setIndex) const;

    [[nodiscard]] std::vector<vk::DescriptorBindingFlags> makeVkSetLayoutBindingFlags(std::uint32_t setIndex) const;

    [[nodiscard]] std::optional<PushConstantRangeInfo> pushConstantRange(const ShaderCursor &cursor) const;

    [[nodiscard]] ShaderLayoutAbiSignature abiSignature() const;

    [[nodiscard]] std::vector<vk::PushConstantRange> makeVkPushConstantRanges() const;

    [[nodiscard]] ShaderCursor rootCursor() const;

  private:
    friend class ShaderCursor;

    [[nodiscard]] std::optional<ShaderCursor::RootField> findRootField(std::string_view fieldName) const;

    [[nodiscard]] std::optional<DescriptorBindingInfo> findBindingByRangeIndex(std::uint32_t bindingRangeIndex) const;

    bool isValid_ = false;
    SlangProgram reflectionProgram_{};
    std::map<std::string, ShaderCursor::RootField> rootFields_;
    std::map<std::uint32_t, DescriptorBindingInfo> bindingByRangeIndex_;
    std::map<std::uint32_t, PushConstantRangeInfo> pushConstantByRangeIndex_;
    std::map<std::tuple<std::uint32_t, std::uint32_t>, PushConstantRangeInfo> pushConstantByOffsetAndSize_;
    std::map<std::tuple<std::uint32_t, std::uint32_t>, DescriptorBindingInfo> bindingBySetAndBinding_;
    std::vector<DescriptorSetLayoutInfo> descriptorSets_;
    std::vector<PushConstantRangeInfo> pushConstantRanges_;
};

[[nodiscard]] bool shaderLayoutAbiEquivalent(const ShaderLayoutAbiSignature &lhs,
                                             const ShaderLayoutAbiSignature &rhs) noexcept;

[[nodiscard]] std::string describeShaderLayoutAbiDifference(const ShaderLayoutAbiSignature &baseline,
                                                            const ShaderLayoutAbiSignature &variant);

void assertShaderLayoutAbiStable(const SlangProgram &baselineProgram, const SlangProgram &variantProgram,
                                 DescriptorBindingPolicy policy = {}, std::string_view debugName = {});

template <typename FieldLayout>
[[nodiscard]] ShaderCursor ShaderCursor::fieldCursorFromLayout(FieldLayout &fieldLayout, std::uint32_t fieldIndex,
                                                               std::string debugPath) const
{
    auto *fieldTypeLayout = fieldLayout.getTypeLayout();
    nrAssert(fieldTypeLayout != nullptr,
             "ShaderCursor::fieldCursorFromLayout requires a non-null field type layout. fieldIndex={}, "
             "debugPath='{}', cursor={}",
             fieldIndex, debugPath, debugSummary());

    ShaderCursor next = *this;
    next.typeLayout_ = fieldTypeLayout;
    auto fieldOffset = fieldLayout.getOffset();
    nrAssert(fieldOffset <= std::numeric_limits<std::size_t>::max() - next.address_.uniformOffset,
             "ShaderCursor field uniform offset overflows size_t. fieldIndex={}, fieldOffset={}, cursor={}", fieldIndex,
             fieldOffset, debugSummary());
    next.address_.uniformOffset += fieldOffset;

    auto bindingRangeOffset =
        detail::sanitizeRangeOffset(typeLayout_->getFieldBindingRangeOffset(static_cast<SlangInt>(fieldIndex)));
    nrAssert(bindingRangeOffset <= std::numeric_limits<std::uint32_t>::max() - next.address_.bindingRangeIndex,
             "ShaderCursor field binding range overflows uint32. fieldIndex={}, rangeOffset={}, cursor={}", fieldIndex,
             bindingRangeOffset, debugSummary());
    next.address_.bindingRangeIndex += bindingRangeOffset;
    next.isRoot_ = false;
    next.debugPath_ = std::move(debugPath);
    return next;
}

} // namespace nr::rhi
