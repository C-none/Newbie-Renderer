module;
export module nr.rhi:descriptor;

import dependency;
import :slang;
import :resource;
import std;

namespace nr::rhi::detail
{
constexpr vk::DeviceSize kWholeBufferRange = std::numeric_limits<vk::DeviceSize>::max();

[[nodiscard]] std::optional<vk::DescriptorType> toVkDescriptorType(slang::BindingType bindingType)
{
    switch (bindingType)
    {
    case slang::BindingType::Sampler:
        return vk::DescriptorType::eSampler;
    case slang::BindingType::CombinedTextureSampler:
        return vk::DescriptorType::eCombinedImageSampler;
    case slang::BindingType::Texture:
        return vk::DescriptorType::eSampledImage;
    case slang::BindingType::MutableTexture:
        return vk::DescriptorType::eStorageImage;
    case slang::BindingType::InputRenderTarget:
        return vk::DescriptorType::eInputAttachment;
    case slang::BindingType::ConstantBuffer:
    case slang::BindingType::ParameterBlock:
        return vk::DescriptorType::eUniformBuffer;
    case slang::BindingType::TypedBuffer:
        return vk::DescriptorType::eUniformTexelBuffer;
    case slang::BindingType::MutableTypedBuffer:
        return vk::DescriptorType::eStorageTexelBuffer;
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
        return vk::DescriptorType::eStorageBuffer;
    case slang::BindingType::InlineUniformData:
        return vk::DescriptorType::eInlineUniformBlock;
    case slang::BindingType::RayTracingAccelerationStructure:
        return vk::DescriptorType::eAccelerationStructureKHR;
    case slang::BindingType::PushConstant:
    case slang::BindingType::Unknown:
    case slang::BindingType::VaryingInput:
    case slang::BindingType::VaryingOutput:
    case slang::BindingType::ExistentialValue:
    case slang::BindingType::MutableFlag:
    case slang::BindingType::BaseMask:
    case slang::BindingType::ExtMask:
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] vk::ShaderStageFlags toVkShaderStageFlags(std::optional<SlangStage> stage)
{
    if (!stage.has_value() || *stage == SLANG_STAGE_NONE)
    {
        return vk::ShaderStageFlagBits::eAll;
    }
    return vk::ShaderStageFlags(toVkShaderStage(*stage));
}

[[nodiscard]] bool isUnboundedDescriptorCount(SlangInt descriptorCount)
{
    return descriptorCount <= 0;
}

[[nodiscard]] bool isInlineUniformByteCountValid(uint32_t byteCount)
{
    return byteCount > 0u && (byteCount % 4u) == 0u;
}

template <typename T, T DefaultValue>
[[nodiscard]] uint32_t sanitizeCountOrSize(T value, bool invalidIfZero = true)
{
    if constexpr (std::is_signed_v<T>)
    {
        if (value <= (invalidIfZero ? 0 : -1) || value > static_cast<T>(std::numeric_limits<uint32_t>::max()))
            return DefaultValue;
    }
    else
    {
        if ((invalidIfZero && value == 0) || value == std::numeric_limits<T>::max() || value > std::numeric_limits<uint32_t>::max())
            return DefaultValue;
    }
    return static_cast<uint32_t>(value);
}

[[nodiscard]] uint32_t sanitizePushConstantSize(size_t byteSize)
{
    return sanitizeCountOrSize<size_t, 0u>(byteSize);
}

[[nodiscard]] uint32_t sanitizeDescriptorCount(SlangInt descriptorCount)
{
    return sanitizeCountOrSize<SlangInt, 1u>(descriptorCount, true);
}

[[nodiscard]] uint32_t sanitizeRangeOffset(SlangInt rangeOffset)
{
    return sanitizeCountOrSize<SlangInt, 0u>(rangeOffset, true);
}

[[nodiscard]] uint32_t sanitizeFieldIndex(SlangInt fieldIndex)
{
    if (fieldIndex < 0) return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(fieldIndex);
}

[[nodiscard]] uint32_t sanitizeElementCount(size_t elementCount)
{
    return sanitizeCountOrSize<size_t, 1u>(elementCount);
}

[[nodiscard]] std::optional<uint32_t> tryElementCount(size_t elementCount)
{
    if (elementCount == 0 || elementCount == std::numeric_limits<size_t>::max() || elementCount > std::numeric_limits<uint32_t>::max())
        return std::nullopt;
    return static_cast<uint32_t>(elementCount);
}

[[nodiscard]] std::optional<size_t> tryLayoutSize(size_t value)
{
    return value == std::numeric_limits<size_t>::max() ? std::nullopt : std::optional<size_t>(value);
}
}

export namespace nr::rhi
{

struct CursorAddress
{
    size_t uniformOffset = 0;
    uint32_t bindingRangeIndex = 0;
    uint32_t bindingArrayIndex = 0;
};

struct DescriptorBindingInfo
{
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t descriptorCount = 1;
    vk::DescriptorType descriptorType = vk::DescriptorType::eStorageBuffer;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    vk::DescriptorBindingFlags bindingFlags{};
    uint32_t bindingRangeIndex = 0;
    std::string debugPath;
};

struct DescriptorSetLayoutInfo
{
    uint32_t set = 0;
    std::vector<DescriptorBindingInfo> bindings;
};

struct PushConstantRangeInfo
{
    uint32_t offset = 0;
    uint32_t size = 0;
    vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    uint32_t bindingRangeIndex = 0;
    std::string debugPath;
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
    std::vector<uint8_t> data;
};

using DescriptorWritePayload =
    std::variant<BufferDescriptorWrite, TexelBufferDescriptorWrite, ImageDescriptorWrite, AccelerationStructureDescriptorWrite, InlineUniformDescriptorWrite>;

struct DescriptorWriteRequest
{
    DescriptorBindingInfo binding;
    uint32_t arrayElement = 0;
    DescriptorWritePayload payload;
};

class ShaderBindingSet
{
  public:
    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(set_); }
    [[nodiscard]] vk::DescriptorSet raw() const noexcept { return set_; }
    [[nodiscard]] uint32_t setIndex() const noexcept { return setIndex_; }

  private:
    friend class ShaderBindingPool;
    vk::DescriptorSet set_{};
    uint32_t setIndex_ = 0;
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
    uint32_t maxSets = 64;
    uint32_t defaultVariableDescriptorCount = 1024;
    ShaderBindingPoolPolicy policy = ShaderBindingPoolPolicy::FrameReset;
    vk::DescriptorPoolCreateFlags extraFlags{};
};

struct DescriptorBindingPolicy
{
    bool enableUpdateAfterBind = true;
    bool enablePartiallyBound = true;
    bool enableVariableDescriptorCount = true;
};

class ShaderBindingPool
{
  public:
    [[nodiscard]] static ShaderBindingPool create(
        const vk::raii::Device &device,
        const ShaderDescriptorLayout &descriptorLayout,
        ShaderBindingPoolConfig config = {});

    [[nodiscard]] ShaderBindingSet allocate(vk::DescriptorSetLayout descriptorSetLayout, uint32_t setIndex, std::optional<uint32_t> variableDescriptorCount = std::nullopt) const;

    void update(const ShaderBindingSet &set, std::span<const DescriptorWriteRequest> writeRequests) const;

    void update(const ShaderBindingSet &set, const DescriptorWriteRequest &writeRequest) const;

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    vk::raii::DescriptorPool pool_ = {nullptr};
    std::map<uint32_t, uint32_t> variableDescriptorCapBySet_{};
};

struct LogicalResourceDescriptorWrite
{
    uint64_t logicalResourceId = 0;
    std::string debugName{};
    vk::ImageLayout imageLayout = vk::ImageLayout::eGeneral;
    vk::Sampler sampler{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize range = detail::kWholeBufferRange;
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
    uint32_t arrayElement = 0;
    ShaderBindingRecordPayload payload;
};

struct PushConstantWriteRecord
{
    PushConstantRangeInfo range;
    uint32_t offset = 0;
    std::vector<uint8_t> data;
};

class ShaderBindingSnapshot
{
  public:
    [[nodiscard]] bool empty() const noexcept
    {
        return descriptorWrites_.empty() && pushConstantWrites_.empty();
    }

    [[nodiscard]] size_t descriptorWriteCount() const noexcept
    {
        return descriptorWrites_.size();
    }

    [[nodiscard]] size_t pushConstantWriteCount() const noexcept
    {
        return pushConstantWrites_.size();
    }

    [[nodiscard]] std::span<const ShaderBindingRecord> descriptorWrites() const noexcept
    {
        return descriptorWrites_;
    }

    [[nodiscard]] std::span<const PushConstantWriteRecord> pushConstantWrites() const noexcept
    {
        return pushConstantWrites_;
    }

  private:
    friend class ShaderCursor;
    std::vector<ShaderBindingRecord> descriptorWrites_{};
    std::vector<PushConstantWriteRecord> pushConstantWrites_{};
};

using LogicalDescriptorResolver = std::function<std::optional<DescriptorWritePayload>(
    const LogicalResourceDescriptorWrite &logicalResource,
    const DescriptorBindingInfo &binding,
    uint32_t arrayElement)>;

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
    // - setObject(...) records descriptor-backed resources (or logical graph references).
    // - setData(...) records push constants or inline uniform bytes.
    // - snapshot() captures a stable per-pass binding view for execute-time replay.

    ShaderCursor() = default;

    [[nodiscard]] bool valid() const noexcept
    {
        return layout_.has_value() && (isRoot_ || typeLayout_ != nullptr);
    }

    [[nodiscard]] CursorAddress address() const noexcept
    {
        return address_;
    }

    [[nodiscard]] slang::TypeLayoutReflection *typeLayout() const noexcept
    {
        return typeLayout_;
    }

    [[nodiscard]] ShaderCursor field(std::string_view fieldName) const;

    [[nodiscard]] ShaderCursor element(uint32_t index) const;

    [[nodiscard]] ShaderCursor getPath(std::string_view path) const;

    [[nodiscard]] std::optional<DescriptorBindingInfo> descriptorBinding() const;

    [[nodiscard]] std::optional<PushConstantRangeInfo> pushConstantRange() const;

    [[nodiscard]] bool setData(std::span<const uint8_t> bytes) const;

    template <typename T>
    requires(std::is_trivially_copyable_v<std::remove_cvref_t<T>>)
    [[nodiscard]] bool setData(const T &value) const
    {
        auto bytes = std::as_bytes(std::span{&value, 1});
        auto *raw = reinterpret_cast<const uint8_t *>(bytes.data());
        return setData(std::span<const uint8_t>{raw, bytes.size()});
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

    [[nodiscard]] uint32_t fieldCount() const noexcept;

    [[nodiscard]] std::optional<uint32_t> elementCount() const;

    [[nodiscard]] std::optional<size_t> size(slang::ParameterCategory category = slang::ParameterCategory::Uniform) const;

    [[nodiscard]] std::optional<size_t> stride(slang::ParameterCategory category = slang::ParameterCategory::Uniform) const;

    [[nodiscard]] std::optional<int32_t> alignment(slang::ParameterCategory category = slang::ParameterCategory::Uniform) const;

    [[nodiscard]] std::vector<slang::ParameterCategory> categories() const;

    [[nodiscard]] std::optional<SlangResourceShape> resourceShape() const;

    [[nodiscard]] std::optional<SlangResourceAccess> resourceAccess() const;

    [[nodiscard]] slang::TypeReflection *resourceResultType() const noexcept;

    [[nodiscard]] std::optional<uint32_t> resourceResultElementCount() const;

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
        return element(static_cast<uint32_t>(index));
    }

  private:
    friend class ShaderDescriptorLayout;

    struct SharedBindingState
    {
        std::map<std::tuple<uint32_t, uint32_t, uint32_t>, ShaderBindingRecord> descriptorWritesByBinding{};
        std::map<std::tuple<uint32_t, uint32_t>, PushConstantWriteRecord> pushConstantWritesByRangeAndOffset{};

        void writeDescriptor(ShaderBindingRecord record)
        {
            auto key = std::tuple{record.binding.set, record.binding.binding, record.arrayElement};
            descriptorWritesByBinding.insert_or_assign(key, std::move(record));
        }

        void writePushConstant(PushConstantWriteRecord record)
        {
            auto key = std::tuple{record.range.bindingRangeIndex, record.offset};
            pushConstantWritesByRangeAndOffset.insert_or_assign(key, std::move(record));
        }

        [[nodiscard]] ShaderBindingSnapshot snapshot() const
        {
            auto snapshot = ShaderBindingSnapshot{};
            snapshot.descriptorWrites_.reserve(descriptorWritesByBinding.size());
            snapshot.pushConstantWrites_.reserve(pushConstantWritesByRangeAndOffset.size());

            std::ranges::for_each(descriptorWritesByBinding, [&](const auto &entry) {
                snapshot.descriptorWrites_.push_back(entry.second);
            });

            std::ranges::for_each(pushConstantWritesByRangeAndOffset, [&](const auto &entry) {
                snapshot.pushConstantWrites_.push_back(entry.second);
            });

            return snapshot;
        }

        void clear()
        {
            descriptorWritesByBinding.clear();
            pushConstantWritesByRangeAndOffset.clear();
        }
    };

    struct RootField
    {
        slang::TypeLayoutReflection *typeLayout = nullptr;
        CursorAddress address{};
        std::string debugPath;
    };

    ShaderCursor(const ShaderDescriptorLayout &layout, RootField field, std::shared_ptr<SharedBindingState> bindingState)
        : layout_(std::cref(layout)),
          typeLayout_(field.typeLayout),
          address_(field.address),
          isRoot_(false),
          debugPath_(std::move(field.debugPath)),
          bindingState_(std::move(bindingState))
    {
    }

    explicit ShaderCursor(const ShaderDescriptorLayout &layout)
        : layout_(std::cref(layout)),
          typeLayout_(nullptr),
          address_({}),
          isRoot_(true),
          debugPath_("$root"),
          bindingState_(std::make_shared<SharedBindingState>())
    {
    }

    [[nodiscard]] static vk::DeviceSize normalizeBufferRange(const Buffer &buffer, vk::DeviceSize offset, vk::DeviceSize range)
    {
        nrAssert(offset <= buffer.size(), std::format("Buffer write offset out of range: offset={}, size={}", offset, buffer.size()));
        if (range == vk::WholeSize)
        {
            return buffer.size() - offset;
        }
        nrAssert(
            offset + range <= buffer.size(),
            std::format("Buffer write range out of bounds: offset={}, range={}, size={}", offset, range, buffer.size()));
        return range;
    }

    [[nodiscard]] static bool acceptsDescriptorType(vk::DescriptorType descriptorType, std::initializer_list<vk::DescriptorType> allowed)
    {
        return std::ranges::find(allowed, descriptorType) != allowed.end();
    }

    [[nodiscard]] bool writeDescriptorRecord(
        ShaderBindingRecordPayload payload,
        std::initializer_list<vk::DescriptorType> allowedTypes,
        std::optional<uint32_t> explicitArrayElement = std::nullopt) const;

    [[nodiscard]] const ShaderDescriptorLayout &layoutRef() const
    {
        nrAssert(layout_.has_value(), "ShaderCursor requires a valid layout reference.");
        return layout_->get();
    }

    std::optional<std::reference_wrapper<const ShaderDescriptorLayout>> layout_;
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
        // - Push constants are captured through ShaderCursor::setData(...) into a snapshot.
        // - Execute-time replay happens through pushConstantsToCommandBuffer(...).

    [[nodiscard]] static ShaderDescriptorLayout create(const SlangProgram &program, DescriptorBindingPolicy policy = {})
    {
        ShaderDescriptorLayout layout;
        if (!program.valid())
        {
            return layout;
        }

        auto *programLayout = program.programLayout();
        if (!programLayout)
        {
            return layout;
        }

        layout.isValid_ = true;

        uint32_t baseBindingRangeIndex = 0;

        auto collectFromTypeLayout = [&](slang::TypeLayoutReflection *typeLayout, std::optional<SlangStage> stage, std::string_view scopeName) {
            if (!typeLayout)
            {
                return;
            }

            auto stageFlags = detail::toVkShaderStageFlags(stage);
            auto bindingRangeCount = std::max<SlangInt>(0, typeLayout->getBindingRangeCount());

            for (SlangInt rangeIndex = 0; rangeIndex < bindingRangeCount; ++rangeIndex)
            {
                auto setIndex = typeLayout->getBindingRangeDescriptorSetIndex(rangeIndex);
                auto bindingIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(rangeIndex);
                auto bindingType = typeLayout->getBindingRangeType(rangeIndex);
                auto bindingRangeIndex = baseBindingRangeIndex + static_cast<uint32_t>(rangeIndex);

                if (bindingType == slang::BindingType::PushConstant)
                {
                    auto *pushConstantBufferTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(rangeIndex);
                    nrAssert(pushConstantBufferTypeLayout != nullptr, std::format("PushConstant binding range {} has null leaf type layout in '{}'", rangeIndex, scopeName));

                    auto *elementTypeLayout = pushConstantBufferTypeLayout ? pushConstantBufferTypeLayout->getElementTypeLayout() : nullptr;
                    if (elementTypeLayout)
                    {
                        pushConstantBufferTypeLayout = elementTypeLayout;
                    }

                    auto pushConstantSize = detail::sanitizePushConstantSize(pushConstantBufferTypeLayout ? pushConstantBufferTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) : 0);
                    nrAssert(
                        pushConstantSize > 0,
                        std::format(
                            "Invalid push constant size in '{}::bindingRange[{}]' (size={})",
                            scopeName,
                            rangeIndex,
                            pushConstantBufferTypeLayout ? pushConstantBufferTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) : size_t(0)));

                    auto key = std::tuple<uint32_t, uint32_t>{0u, pushConstantSize};
                    auto mergedIt = layout.pushConstantByOffsetAndSize_.find(key);
                    PushConstantRangeInfo info{
                        .offset = 0,
                        .size = pushConstantSize,
                        .stageFlags = stageFlags,
                        .bindingRangeIndex = bindingRangeIndex,
                        .debugPath = std::format("{}::bindingRange[{}]", scopeName, rangeIndex),
                    };

                    if (mergedIt == layout.pushConstantByOffsetAndSize_.end())
                    {
                        layout.pushConstantByOffsetAndSize_.insert_or_assign(key, info);
                    }
                    else
                    {
                        mergedIt->second.stageFlags |= info.stageFlags;
                        info = mergedIt->second;
                    }

                    layout.pushConstantByRangeIndex_.insert_or_assign(bindingRangeIndex, info);
                    continue;
                }

                if (setIndex < 0 || bindingIndex < 0)
                {
                    continue;
                }

                auto descriptorType = detail::toVkDescriptorType(bindingType);
                nrAssert(
                    descriptorType.has_value(),
                    std::format(
                        "Unsupported Slang binding type {} in '{}::bindingRange[{}]'.",
                        static_cast<int32_t>(bindingType),
                        scopeName,
                        rangeIndex));

                auto descriptorCountRaw = typeLayout->getBindingRangeBindingCount(rangeIndex);

                DescriptorBindingInfo info{};
                info.set = static_cast<uint32_t>(setIndex);
                info.binding = static_cast<uint32_t>(bindingIndex);
                info.descriptorCount = detail::sanitizeDescriptorCount(descriptorCountRaw);
                info.descriptorType = *descriptorType;
                if (detail::isUnboundedDescriptorCount(descriptorCountRaw) && policy.enableVariableDescriptorCount)
                {
                    info.bindingFlags |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
                    if (policy.enablePartiallyBound)
                    {
                        info.bindingFlags |= vk::DescriptorBindingFlagBits::ePartiallyBound;
                    }
                    if (policy.enableUpdateAfterBind)
                    {
                        info.bindingFlags |= vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                    }
                }
                if (info.descriptorType == vk::DescriptorType::eInlineUniformBlock)
                {
                    nrAssert(
                        detail::isInlineUniformByteCountValid(info.descriptorCount),
                        std::format(
                            "Inline uniform descriptor byte count must be > 0 and multiple of 4. set={}, binding={}, count={}",
                            info.set,
                            info.binding,
                            info.descriptorCount));
                }
                info.stageFlags = stageFlags;
                info.bindingRangeIndex = bindingRangeIndex;
                info.debugPath = std::format("{}::bindingRange[{}]", scopeName, rangeIndex);

                auto key = std::tuple<uint32_t, uint32_t>{info.set, info.binding};
                auto mergedIt = layout.bindingBySetAndBinding_.find(key);
                if (mergedIt == layout.bindingBySetAndBinding_.end())
                {
                    layout.bindingBySetAndBinding_.insert_or_assign(key, info);
                }
                else
                {
                    nrAssert(
                        mergedIt->second.descriptorType == info.descriptorType &&
                            mergedIt->second.descriptorCount == info.descriptorCount &&
                            mergedIt->second.bindingFlags == info.bindingFlags,
                        std::format("Descriptor layout mismatch at set={}, binding={} when merging '{}'", info.set, info.binding, info.debugPath));
                    mergedIt->second.stageFlags |= info.stageFlags;
                    info = mergedIt->second;
                }

                layout.bindingByRangeIndex_.insert_or_assign(info.bindingRangeIndex, info);
            }

            auto fieldCount = std::max<SlangInt>(0, typeLayout->getFieldCount());
            for (SlangInt fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex)
            {
                auto *fieldLayout = typeLayout->getFieldByIndex(static_cast<unsigned int>(fieldIndex));
                if (!fieldLayout)
                {
                    continue;
                }

                auto *fieldTypeLayout = fieldLayout->getTypeLayout();
                auto *fieldVariable = fieldLayout->getVariable();
                if (!fieldTypeLayout || !fieldVariable || !fieldVariable->getName())
                {
                    continue;
                }

                std::string name(fieldVariable->getName());
                auto inserted = layout.rootFields_.try_emplace(
                    name,
                    ShaderCursor::RootField{
                        .typeLayout = fieldTypeLayout,
                        .address = CursorAddress{
                            .uniformOffset = fieldLayout->getOffset(),
                            .bindingRangeIndex = baseBindingRangeIndex + detail::sanitizeRangeOffset(typeLayout->getFieldBindingRangeOffset(fieldIndex)),
                            .bindingArrayIndex = 0,
                        },
                        .debugPath = std::format("{}.{}", scopeName, name),
                    });

                nrAssert(
                    inserted.second,
                    std::format("Shader parameter name conflict detected for '{}'. Program-level and entrypoint-level resources must not share names.", name));
            }

            baseBindingRangeIndex += static_cast<uint32_t>(bindingRangeCount);
        };

        auto *globalParamsVarLayout = programLayout->getGlobalParamsVarLayout();
        nrAssert(globalParamsVarLayout != nullptr, "ShaderDescriptorLayout::create requires ProgramLayout::getGlobalParamsVarLayout().");
        collectFromTypeLayout(globalParamsVarLayout ? globalParamsVarLayout->getTypeLayout() : nullptr, std::nullopt, "$program");


        for (auto const &[key, value] : layout.bindingBySetAndBinding_)
        {
            auto setIndex = std::get<0>(key);
            if (layout.descriptorSets_.empty() || layout.descriptorSets_.back().set != setIndex)
            {
                layout.descriptorSets_.push_back(DescriptorSetLayoutInfo{.set = setIndex, .bindings = {}});
            }
            layout.descriptorSets_.back().bindings.push_back(value);
        }

        for (auto const &[_, value] : layout.pushConstantByOffsetAndSize_)
        {
            layout.pushConstantRanges_.push_back(value);
        }

        std::ranges::sort(layout.pushConstantRanges_, [](const PushConstantRangeInfo &lhs, const PushConstantRangeInfo &rhs) {
            if (lhs.offset == rhs.offset)
            {
                return lhs.size < rhs.size;
            }
            return lhs.offset < rhs.offset;
        });

        return layout;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return isValid_;
    }

    [[nodiscard]] std::span<const DescriptorSetLayoutInfo> descriptorSets() const noexcept
    {
        return descriptorSets_;
    }

    [[nodiscard]] std::vector<vk::DescriptorSetLayoutBinding> makeVkSetLayoutBindings(uint32_t setIndex) const
    {
        auto it = std::ranges::find_if(descriptorSets_, [setIndex](const DescriptorSetLayoutInfo &setLayout) { return setLayout.set == setIndex; });
        if (it == std::ranges::end(descriptorSets_))
        {
            return {};
        }

        return it->bindings |
               std::views::transform([](const DescriptorBindingInfo &bindingInfo) {
                   vk::DescriptorSetLayoutBinding binding{};
                   binding.binding = bindingInfo.binding;
                   binding.descriptorType = bindingInfo.descriptorType;
                   binding.descriptorCount = bindingInfo.descriptorCount;
                   binding.stageFlags = bindingInfo.stageFlags;
                   return binding;
               }) |
               std::ranges::to<std::vector>();
    }

    [[nodiscard]] std::vector<vk::DescriptorBindingFlags> makeVkSetLayoutBindingFlags(uint32_t setIndex) const
    {
        auto it = std::ranges::find_if(descriptorSets_, [setIndex](const DescriptorSetLayoutInfo &setLayout) { return setLayout.set == setIndex; });
        if (it == std::ranges::end(descriptorSets_))
        {
            return {};
        }

        return it->bindings |
               std::views::transform([](const DescriptorBindingInfo &bindingInfo) { return bindingInfo.bindingFlags; }) |
               std::ranges::to<std::vector>();
    }

    [[nodiscard]] bool requiresUpdateAfterBindPool() const
    {
        return std::ranges::any_of(descriptorSets_, [](const DescriptorSetLayoutInfo &setInfo) {
            return std::ranges::any_of(setInfo.bindings, [](const DescriptorBindingInfo &bindingInfo) {
                return (bindingInfo.bindingFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) == vk::DescriptorBindingFlagBits::eUpdateAfterBind;
            });
        });
    }

    [[nodiscard]] std::span<const PushConstantRangeInfo> pushConstantRanges() const noexcept
    {
        return pushConstantRanges_;
    }

    [[nodiscard]] std::optional<PushConstantRangeInfo> pushConstantRange(const ShaderCursor &cursor) const
    {
        if (!cursor.valid() || cursor.isRoot_)
        {
            return std::nullopt;
        }

        if (!cursor.layout_.has_value() || std::addressof(cursor.layout_->get()) != this)
        {
            return std::nullopt;
        }

        auto it = pushConstantByRangeIndex_.find(cursor.address_.bindingRangeIndex);
        if (it == pushConstantByRangeIndex_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::vector<vk::PushConstantRange> makeVkPushConstantRanges() const
    {
        return pushConstantRanges_ |
               std::views::transform([](const PushConstantRangeInfo &pushConstantInfo) {
                   vk::PushConstantRange range{};
                   range.stageFlags = pushConstantInfo.stageFlags;
                   range.offset = pushConstantInfo.offset;
                   range.size = pushConstantInfo.size;
                   return range;
               }) |
               std::ranges::to<std::vector>();
    }

    [[nodiscard]] ShaderCursor rootCursor() const
    {
        return ShaderCursor(*this);
    }

  private:
    friend class ShaderCursor;

    [[nodiscard]] std::optional<ShaderCursor::RootField> findRootField(std::string_view fieldName) const
    {
        auto it = rootFields_.find(std::string(fieldName));
        if (it == rootFields_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::optional<DescriptorBindingInfo> findBindingByRangeIndex(uint32_t bindingRangeIndex) const
    {
        auto it = bindingByRangeIndex_.find(bindingRangeIndex);
        if (it == bindingByRangeIndex_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    bool isValid_ = false;
    std::map<std::string, ShaderCursor::RootField> rootFields_;
    std::map<uint32_t, DescriptorBindingInfo> bindingByRangeIndex_;
    std::map<uint32_t, PushConstantRangeInfo> pushConstantByRangeIndex_;
    std::map<std::tuple<uint32_t, uint32_t>, PushConstantRangeInfo> pushConstantByOffsetAndSize_;
    std::map<std::tuple<uint32_t, uint32_t>, DescriptorBindingInfo> bindingBySetAndBinding_;
    std::vector<DescriptorSetLayoutInfo> descriptorSets_;
    std::vector<PushConstantRangeInfo> pushConstantRanges_;
};

[[nodiscard]] std::vector<ShaderBindingSet> allocateBindingSetsForLayout(const CursorPipelineLayout &layout, ShaderBindingPool &pool);

void bindResourcesToCommandBuffer(
    vk::CommandBuffer commandBuffer,
    vk::PipelineBindPoint bindPoint,
    const CursorPipelineLayout &layout,
    ShaderBindingPool &pool,
    std::span<const ShaderBindingSet> sets,
    const ShaderBindingSnapshot &snapshot,
    LogicalDescriptorResolver logicalResolver = {});

[[nodiscard]] std::vector<ShaderBindingSet> bindResourcesToCommandBuffer(
    vk::CommandBuffer commandBuffer,
    vk::PipelineBindPoint bindPoint,
    const CursorPipelineLayout &layout,
    ShaderBindingPool &pool,
    const ShaderBindingSnapshot &snapshot,
    LogicalDescriptorResolver logicalResolver = {});

void pushConstantsToCommandBuffer(
    vk::CommandBuffer commandBuffer,
    const CursorPipelineLayout &layout,
    const ShaderBindingSnapshot &snapshot);

[[nodiscard]] ShaderCursor ShaderCursor::field(std::string_view fieldName) const
{
    if (!valid())
    {
        return {};
    }

    if (isRoot_)
    {
        auto rootField = layoutRef().findRootField(fieldName);
        if (!rootField.has_value())
        {
            return {};
        }
        return ShaderCursor(layoutRef(), std::move(*rootField), bindingState_);
    }

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        auto *elementType = typeLayout_->getElementTypeLayout();
        if (!elementType)
        {
            return {};
        }

        ShaderCursor dereferenced = *this;
        dereferenced.typeLayout_ = elementType;
        return dereferenced.field(fieldName);
    }

    if (kind != slang::TypeReflection::Kind::Struct)
    {
        return {};
    }

    auto fieldIndex = detail::sanitizeFieldIndex(typeLayout_->findFieldIndexByName(fieldName.data(), fieldName.data() + fieldName.size()));
    if (fieldIndex == std::numeric_limits<uint32_t>::max())
    {
        return {};
    }

    auto *fieldLayout = typeLayout_->getFieldByIndex(fieldIndex);
    if (!fieldLayout)
    {
        return {};
    }

    auto *fieldTypeLayout = fieldLayout->getTypeLayout();
    if (!fieldTypeLayout)
    {
        return {};
    }

    ShaderCursor next = *this;
    next.typeLayout_ = fieldTypeLayout;
    next.address_.uniformOffset += fieldLayout->getOffset();
    next.address_.bindingRangeIndex += detail::sanitizeRangeOffset(typeLayout_->getFieldBindingRangeOffset(static_cast<SlangInt>(fieldIndex)));
    next.isRoot_ = false;
    next.debugPath_ = std::format("{}.{}", debugPath_, fieldName);
    return next;
}

[[nodiscard]] ShaderCursor ShaderCursor::element(uint32_t index) const
{
    if (!valid() || isRoot_)
    {
        return {};
    }

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::Struct)
    {
        auto *fieldLayout = typeLayout_->getFieldByIndex(index);
        if (!fieldLayout)
        {
            return {};
        }

        auto *fieldTypeLayout = fieldLayout->getTypeLayout();
        if (!fieldTypeLayout)
        {
            return {};
        }

        ShaderCursor next = *this;
        next.typeLayout_ = fieldTypeLayout;
        next.address_.uniformOffset += fieldLayout->getOffset();
        next.address_.bindingRangeIndex += detail::sanitizeRangeOffset(typeLayout_->getFieldBindingRangeOffset(static_cast<SlangInt>(index)));
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    if (kind != slang::TypeReflection::Kind::Array && kind != slang::TypeReflection::Kind::Vector && kind != slang::TypeReflection::Kind::Matrix)
    {
        return {};
    }

    auto *elementTypeLayout = typeLayout_->getElementTypeLayout();
    if (!elementTypeLayout)
    {
        return {};
    }

    ShaderCursor next = *this;
    next.typeLayout_ = elementTypeLayout;
    next.address_.uniformOffset += static_cast<size_t>(index) * typeLayout_->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
    next.address_.bindingArrayIndex = next.address_.bindingArrayIndex * detail::sanitizeElementCount(typeLayout_->getElementCount()) + index;
    next.debugPath_ = std::format("{}[{}]", debugPath_, index);
    return next;
}

[[nodiscard]] ShaderCursor ShaderCursor::getPath(std::string_view path) const
{
    ShaderCursor cursor = *this;
    size_t tokenBegin = 0;

    while (tokenBegin < path.size())
    {
        if (path[tokenBegin] == '.')
        {
            ++tokenBegin;
            continue;
        }

        if (path[tokenBegin] == '[')
        {
            auto tokenEnd = path.find(']', tokenBegin + 1);
            if (tokenEnd == std::string_view::npos)
            {
                return {};
            }

            auto indexText = path.substr(tokenBegin + 1, tokenEnd - tokenBegin - 1);
            uint32_t index = 0;
            auto indexParse = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
            if (indexParse.ec != std::errc{})
            {
                return {};
            }

            cursor = cursor.element(index);
            if (!cursor.valid())
            {
                return {};
            }

            tokenBegin = tokenEnd + 1;
            continue;
        }

        auto tokenEnd = path.find_first_of(".[", tokenBegin);
        auto token = path.substr(tokenBegin, tokenEnd == std::string_view::npos ? path.size() - tokenBegin : tokenEnd - tokenBegin);
        cursor = cursor.field(token);
        if (!cursor.valid())
        {
            return {};
        }

        if (tokenEnd == std::string_view::npos)
        {
            break;
        }
        tokenBegin = tokenEnd;
    }

    return cursor;
}

[[nodiscard]] std::optional<DescriptorBindingInfo> ShaderCursor::descriptorBinding() const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return layoutRef().findBindingByRangeIndex(address_.bindingRangeIndex);
}

[[nodiscard]] std::optional<PushConstantRangeInfo> ShaderCursor::pushConstantRange() const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return layoutRef().pushConstantRange(*this);
}

[[nodiscard]] bool ShaderCursor::writeDescriptorRecord(
    ShaderBindingRecordPayload payload,
    std::initializer_list<vk::DescriptorType> allowedTypes,
    std::optional<uint32_t> explicitArrayElement) const
{
    if (!valid() || isRoot_ || !bindingState_)
    {
        return false;
    }

    auto bindingInfo = descriptorBinding();
    if (!bindingInfo.has_value() || !acceptsDescriptorType(bindingInfo->descriptorType, allowedTypes))
    {
        return false;
    }

    auto arrayElement = explicitArrayElement.value_or(address_.bindingArrayIndex);
    bindingState_->writeDescriptor(ShaderBindingRecord{
        .binding = *bindingInfo,
        .arrayElement = arrayElement,
        .payload = std::move(payload),
    });
    return true;
}

[[nodiscard]] bool ShaderCursor::setData(std::span<const uint8_t> bytes) const
{
    if (!valid() || isRoot_ || !bindingState_ || bytes.empty())
    {
        return false;
    }

    auto byteCount = static_cast<uint32_t>(bytes.size());
    auto copiedBytes = std::vector<uint8_t>{};
    copiedBytes.assign(bytes.begin(), bytes.end());

    if (auto pushRange = pushConstantRange(); pushRange.has_value())
    {
        nrAssert(
            address_.uniformOffset <= std::numeric_limits<uint32_t>::max(),
            std::format("Push-constant cursor offset exceeds uint32 range (offset={}).", address_.uniformOffset));

        auto offset = static_cast<uint32_t>(address_.uniformOffset);
        auto rangeBegin = static_cast<uint64_t>(pushRange->offset);
        auto rangeEnd = rangeBegin + static_cast<uint64_t>(pushRange->size);
        auto writeBegin = static_cast<uint64_t>(offset);
        auto writeEnd = writeBegin + static_cast<uint64_t>(byteCount);

        nrAssert(
            writeBegin >= rangeBegin && writeEnd <= rangeEnd,
            std::format(
                "Push-constant write outside range. path={}, offset={}, size={}, rangeBegin={}, rangeEnd={}",
                debugPath_,
                offset,
                byteCount,
                pushRange->offset,
                pushRange->offset + pushRange->size));

        bindingState_->writePushConstant(PushConstantWriteRecord{
            .range = *pushRange,
            .offset = offset,
            .data = std::move(copiedBytes),
        });
        return true;
    }

    auto bindingInfo = descriptorBinding();
    if (!bindingInfo.has_value() || bindingInfo->descriptorType != vk::DescriptorType::eInlineUniformBlock)
    {
        return false;
    }

    nrAssert(
        detail::isInlineUniformByteCountValid(byteCount),
        std::format("Inline uniform write size must be > 0 and multiple of 4 (size={}).", byteCount));

    nrAssert(
        address_.uniformOffset <= std::numeric_limits<uint32_t>::max(),
        std::format("Inline uniform offset exceeds uint32 range (offset={}).", address_.uniformOffset));

    auto arrayElement = static_cast<uint32_t>(address_.uniformOffset);
    nrAssert(
        (arrayElement % 4u) == 0u,
        std::format(
            "Inline uniform dstArrayElement must be multiple of 4. path={}, dstArrayElement={}",
            debugPath_,
            arrayElement));

    return writeDescriptorRecord(
        InlineUniformDescriptorWrite{.data = std::move(copiedBytes)},
        {vk::DescriptorType::eInlineUniformBlock},
        arrayElement);
}

[[nodiscard]] bool ShaderCursor::setObject(
    const Buffer &buffer,
    vk::DeviceSize offset,
    vk::DeviceSize range) const
{
    nrAssert(buffer.valid(), "ShaderCursor::setObject(Buffer) requires a valid Buffer.");
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    return writeDescriptorRecord(
        BufferDescriptorWrite{.buffer = buffer.handle(), .offset = offset, .range = finalRange},
        {vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eUniformBufferDynamic, vk::DescriptorType::eStorageBuffer});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::BufferView view) const
{
    return writeDescriptorRecord(
        TexelBufferDescriptorWrite{.view = view},
        {vk::DescriptorType::eUniformTexelBuffer, vk::DescriptorType::eStorageTexelBuffer});
}

[[nodiscard]] bool ShaderCursor::setObject(
    Buffer &buffer,
    vk::Format format,
    vk::DeviceSize offset,
    vk::DeviceSize range,
    std::string_view viewName) const
{
    nrAssert(buffer.valid(), "ShaderCursor::setObject(Buffer,Format) requires a valid Buffer.");
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    auto view = buffer.addView(format, offset, finalRange, viewName);
    return setObject(*view.get());
}

[[nodiscard]] bool ShaderCursor::setObject(
    const Image &image,
    vk::ImageLayout imageLayout) const
{
    nrAssert(image.valid(), "ShaderCursor::setObject(Image) requires a valid Image.");
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = {}},
        {vk::DescriptorType::eSampledImage, vk::DescriptorType::eInputAttachment, vk::DescriptorType::eStorageImage});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::Sampler sampler) const
{
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = {}, .imageLayout = vk::ImageLayout::eUndefined, .sampler = sampler},
        {vk::DescriptorType::eSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(
    const Image &image,
    vk::Sampler sampler,
    vk::ImageLayout imageLayout) const
{
    nrAssert(image.valid(), "ShaderCursor::setObject(Image,Sampler) requires a valid Image.");
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = sampler},
        {vk::DescriptorType::eCombinedImageSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::AccelerationStructureKHR accelerationStructure) const
{
    return writeDescriptorRecord(
        AccelerationStructureDescriptorWrite{.accelerationStructure = accelerationStructure},
        {vk::DescriptorType::eAccelerationStructureKHR});
}

[[nodiscard]] bool ShaderCursor::setObject(const LogicalResourceDescriptorWrite &logicalResource) const
{
    return writeDescriptorRecord(
        logicalResource,
        {
            vk::DescriptorType::eUniformBuffer,
            vk::DescriptorType::eUniformBufferDynamic,
            vk::DescriptorType::eStorageBuffer,
            vk::DescriptorType::eUniformTexelBuffer,
            vk::DescriptorType::eStorageTexelBuffer,
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eStorageImage,
            vk::DescriptorType::eInputAttachment,
            vk::DescriptorType::eSampler,
            vk::DescriptorType::eCombinedImageSampler,
            vk::DescriptorType::eAccelerationStructureKHR,
        });
}

[[nodiscard]] ShaderBindingSnapshot ShaderCursor::snapshot() const
{
    if (!bindingState_)
    {
        return {};
    }
    return bindingState_->snapshot();
}

void ShaderCursor::clearSnapshot() const
{
    if (!bindingState_)
    {
        return;
    }
    bindingState_->clear();
}

[[nodiscard]] slang::TypeReflection::Kind ShaderCursor::kind() const noexcept
{
    if (!valid() || isRoot_)
    {
        return slang::TypeReflection::Kind::None;
    }
    return typeLayout_->getKind();
}

[[nodiscard]] std::string ShaderCursor::typeName() const
{
    if (!valid())
    {
        return {};
    }
    if (isRoot_)
    {
        return "$root";
    }

    auto *type = typeLayout_->getType();
    if (!type || !type->getName())
    {
        return {};
    }
    return std::string(type->getName());
}

[[nodiscard]] uint32_t ShaderCursor::fieldCount() const noexcept
{
    if (!valid() || isRoot_)
    {
        return 0u;
    }
    return static_cast<uint32_t>(std::max<SlangInt>(0, typeLayout_->getFieldCount()));
}

[[nodiscard]] std::optional<uint32_t> ShaderCursor::elementCount() const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }

    auto kindValue = typeLayout_->getKind();
    if (kindValue != slang::TypeReflection::Kind::Array && kindValue != slang::TypeReflection::Kind::Vector && kindValue != slang::TypeReflection::Kind::Matrix)
    {
        return std::nullopt;
    }

    return detail::tryElementCount(typeLayout_->getElementCount());
}

[[nodiscard]] std::optional<size_t> ShaderCursor::size(slang::ParameterCategory category) const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return detail::tryLayoutSize(typeLayout_->getSize(category));
}

[[nodiscard]] std::optional<size_t> ShaderCursor::stride(slang::ParameterCategory category) const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return detail::tryLayoutSize(typeLayout_->getStride(category));
}

[[nodiscard]] std::optional<int32_t> ShaderCursor::alignment(slang::ParameterCategory category) const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return typeLayout_->getAlignment(category);
}

[[nodiscard]] std::vector<slang::ParameterCategory> ShaderCursor::categories() const
{
    if (!valid() || isRoot_)
    {
        return {};
    }

    auto categoryCount = typeLayout_->getCategoryCount();
    if (categoryCount == 0)
    {
        return {};
    }

    auto result = std::vector<slang::ParameterCategory>{};
    result.reserve(categoryCount);
    for (unsigned int i = 0; i < categoryCount; ++i)
    {
        result.push_back(typeLayout_->getCategoryByIndex(i));
    }
    return result;
}

[[nodiscard]] std::optional<SlangResourceShape> ShaderCursor::resourceShape() const
{
    if (!valid() || isRoot_ || typeLayout_->getKind() != slang::TypeReflection::Kind::Resource)
    {
        return std::nullopt;
    }
    return typeLayout_->getResourceShape();
}

[[nodiscard]] std::optional<SlangResourceAccess> ShaderCursor::resourceAccess() const
{
    if (!valid() || isRoot_ || typeLayout_->getKind() != slang::TypeReflection::Kind::Resource)
    {
        return std::nullopt;
    }
    return typeLayout_->getResourceAccess();
}

[[nodiscard]] slang::TypeReflection *ShaderCursor::resourceResultType() const noexcept
{
    if (!valid() || isRoot_ || typeLayout_->getKind() != slang::TypeReflection::Kind::Resource)
    {
        return nullptr;
    }
    return typeLayout_->getResourceResultType();
}

[[nodiscard]] std::optional<uint32_t> ShaderCursor::resourceResultElementCount() const
{
    auto *resultType = resourceResultType();
    if (!resultType)
    {
        return std::nullopt;
    }

    auto resultKind = resultType->getKind();
    if (resultKind != slang::TypeReflection::Kind::Array && resultKind != slang::TypeReflection::Kind::Vector && resultKind != slang::TypeReflection::Kind::Matrix)
    {
        return std::nullopt;
    }

    return detail::tryElementCount(resultType->getElementCount());
}

[[nodiscard]] ShaderBindingPool ShaderBindingPool::create(
    const vk::raii::Device &device,
    const ShaderDescriptorLayout &descriptorLayout,
    ShaderBindingPoolConfig config)
{
    ShaderBindingPool pool;
    pool.device_ = std::cref(device);

    const auto maxSets = std::max(config.maxSets, 1u);

    auto descriptorCounts = std::map<vk::DescriptorType, uint32_t>{};
    uint32_t inlineUniformBindingCount = 0;
    std::ranges::for_each(descriptorLayout.descriptorSets(), [&](const DescriptorSetLayoutInfo &setInfo) {
        std::ranges::for_each(setInfo.bindings, [&](const DescriptorBindingInfo &bindingInfo) {
            const bool isVariableCount = (bindingInfo.bindingFlags & vk::DescriptorBindingFlagBits::eVariableDescriptorCount) == vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
            const uint32_t effectiveDescriptorCount = isVariableCount ? std::max(config.defaultVariableDescriptorCount, 1u) : bindingInfo.descriptorCount;
            descriptorCounts[bindingInfo.descriptorType] += effectiveDescriptorCount * maxSets;
            if (isVariableCount)
            {
                auto &cap = pool.variableDescriptorCapBySet_[setInfo.set];
                cap = std::max(cap, effectiveDescriptorCount);
            }
            if (bindingInfo.descriptorType == vk::DescriptorType::eInlineUniformBlock)
            {
                inlineUniformBindingCount += maxSets;
            }
        });
    });

    auto poolSizes = descriptorCounts |
                     std::views::transform([](const auto &pair) {
                         return vk::DescriptorPoolSize{pair.first, pair.second};
                     }) |
                     std::ranges::to<std::vector>();

    if (poolSizes.empty())
    {
        poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, maxSets});
    }

    vk::DescriptorPoolCreateFlags flags = config.extraFlags;
    if (config.policy == ShaderBindingPoolPolicy::PersistentFreeable)
    {
        flags |= vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    }
    if (descriptorLayout.requiresUpdateAfterBindPool())
    {
        flags |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
    }

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = flags;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    vk::DescriptorPoolInlineUniformBlockCreateInfo inlineUniformPoolInfo{};
    if (inlineUniformBindingCount > 0)
    {
        inlineUniformPoolInfo.maxInlineUniformBlockBindings = inlineUniformBindingCount;
        poolInfo.pNext = &inlineUniformPoolInfo;
    }

    pool.pool_ = vk::raii::DescriptorPool(device, poolInfo);
    return pool;
}

[[nodiscard]] ShaderBindingSet ShaderBindingPool::allocate(vk::DescriptorSetLayout descriptorSetLayout, uint32_t setIndex, std::optional<uint32_t> variableDescriptorCount) const
{
    nrAssert(device_.has_value(), "ShaderBindingPool::allocate requires an owning device reference.");

    ShaderBindingSet set;
    set.setIndex_ = setIndex;
    if (!descriptorSetLayout)
    {
        return set;
    }

    vk::DescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.descriptorPool = *pool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout;

    vk::DescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo{};
    if (auto it = variableDescriptorCapBySet_.find(setIndex); it != variableDescriptorCapBySet_.end())
    {
        const auto requestedCount = variableDescriptorCount.value_or(it->second);
        const auto resolvedCount = std::clamp(requestedCount, 1u, it->second);
        variableCountInfo.descriptorSetCount = 1;
        variableCountInfo.pDescriptorCounts = &resolvedCount;
        allocateInfo.pNext = &variableCountInfo;
    }

    auto allocatedSets = device_->get().allocateDescriptorSets(allocateInfo);
    if (!allocatedSets.empty())
    {
        // allocateDescriptorSets returns RAII wrappers; release ownership so the handle
        // remains valid for pool-managed lifetime instead of being freed at scope exit.
        set.set_ = allocatedSets.front().release();
    }
    return set;
}

void ShaderBindingPool::update(const ShaderBindingSet &set, std::span<const DescriptorWriteRequest> writeRequests) const
{
    nrAssert(device_.has_value(), "ShaderBindingPool::update requires an owning device reference.");
    if (!set.valid() || writeRequests.empty())
    {
        return;
    }

    auto vkWrites = std::vector<vk::WriteDescriptorSet>{};
    auto bufferInfos = std::vector<vk::DescriptorBufferInfo>{};
    auto imageInfos = std::vector<vk::DescriptorImageInfo>{};
    auto texelBufferViews = std::vector<vk::BufferView>{};
    auto accelerationHandles = std::vector<vk::AccelerationStructureKHR>{};
    auto accelerationInfos = std::vector<vk::WriteDescriptorSetAccelerationStructureKHR>{};
    auto inlineUniformInfos = std::vector<vk::WriteDescriptorSetInlineUniformBlock>{};

    vkWrites.reserve(writeRequests.size());
    bufferInfos.reserve(writeRequests.size());
    imageInfos.reserve(writeRequests.size());
    texelBufferViews.reserve(writeRequests.size());
    accelerationHandles.reserve(writeRequests.size());
    accelerationInfos.reserve(writeRequests.size());
    inlineUniformInfos.reserve(writeRequests.size());

    for (const auto &request : writeRequests)
    {
        nrAssert(
            request.binding.set == set.setIndex(),
            std::format(
                "Descriptor write set mismatch. request set={}, target set={}",
                request.binding.set,
                set.setIndex()));

        nrAssert(
            request.arrayElement < request.binding.descriptorCount,
            std::format(
                "Descriptor write array index out of range. set={}, binding={}, arrayElement={}, descriptorCount={}",
                request.binding.set,
                request.binding.binding,
                request.arrayElement,
                request.binding.descriptorCount));

        vk::WriteDescriptorSet write{};
        write.dstSet = set.raw();
        write.dstBinding = request.binding.binding;
        write.dstArrayElement = request.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = request.binding.descriptorType;

        std::visit(
            [&](const auto &payload) {
                using PayloadT = std::remove_cvref_t<decltype(payload)>;

                if constexpr (std::same_as<PayloadT, BufferDescriptorWrite>)
                {
                    vk::DescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = payload.buffer;
                    bufferInfo.offset = payload.offset;
                    bufferInfo.range = payload.range;
                    bufferInfos.push_back(bufferInfo);
                    write.pBufferInfo = &bufferInfos.back();
                }
                else if constexpr (std::same_as<PayloadT, TexelBufferDescriptorWrite>)
                {
                    texelBufferViews.push_back(payload.view);
                    write.pTexelBufferView = &texelBufferViews.back();
                }
                else if constexpr (std::same_as<PayloadT, ImageDescriptorWrite>)
                {
                    vk::DescriptorImageInfo imageInfo{};
                    imageInfo.sampler = payload.sampler;
                    imageInfo.imageView = payload.imageView;
                    imageInfo.imageLayout = payload.imageLayout;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                }
                else if constexpr (std::same_as<PayloadT, AccelerationStructureDescriptorWrite>)
                {
                    accelerationHandles.push_back(payload.accelerationStructure);
                    vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{};
                    accelerationInfo.accelerationStructureCount = 1;
                    accelerationInfo.pAccelerationStructures = &accelerationHandles.back();
                    accelerationInfos.push_back(accelerationInfo);
                    write.pNext = &accelerationInfos.back();
                }
                else if constexpr (std::same_as<PayloadT, InlineUniformDescriptorWrite>)
                {
                    nrAssert(
                        write.descriptorType == vk::DescriptorType::eInlineUniformBlock,
                        std::format(
                            "Inline uniform payload requires eInlineUniformBlock descriptor type. set={}, binding={}, type={}",
                            request.binding.set,
                            request.binding.binding,
                            vk::to_string(write.descriptorType)));

                    auto byteCount = static_cast<uint32_t>(payload.data.size());
                    nrAssert(
                        detail::isInlineUniformByteCountValid(byteCount),
                        std::format(
                            "Inline uniform write size must be > 0 and multiple of 4. set={}, binding={}, size={}",
                            request.binding.set,
                            request.binding.binding,
                            byteCount));
                    nrAssert(
                        (write.dstArrayElement % 4u) == 0u,
                        std::format(
                            "Inline uniform dstArrayElement must be multiple of 4. set={}, binding={}, dstArrayElement={}",
                            request.binding.set,
                            request.binding.binding,
                            write.dstArrayElement));
                    nrAssert(
                        write.dstArrayElement + byteCount <= request.binding.descriptorCount,
                        std::format(
                            "Inline uniform write out of range. set={}, binding={}, dstArrayElement={}, size={}, bindingByteCapacity={}",
                            request.binding.set,
                            request.binding.binding,
                            write.dstArrayElement,
                            byteCount,
                            request.binding.descriptorCount));

                    write.descriptorCount = byteCount;

                    vk::WriteDescriptorSetInlineUniformBlock inlineUniformInfo{};
                    inlineUniformInfo.dataSize = byteCount;
                    inlineUniformInfo.pData = payload.data.data();
                    inlineUniformInfos.push_back(inlineUniformInfo);
                    write.pNext = &inlineUniformInfos.back();
                }
            },
            request.payload);

        vkWrites.push_back(write);
    }

    device_->get().updateDescriptorSets(vkWrites, {});
}

void ShaderBindingPool::update(const ShaderBindingSet &set, const DescriptorWriteRequest &writeRequest) const
{
    update(set, std::span<const DescriptorWriteRequest>{&writeRequest, 1});
}

[[nodiscard]] std::vector<DescriptorWriteRequest> resolveDescriptorWriteRequests(
    const ShaderBindingSnapshot &snapshot,
    LogicalDescriptorResolver logicalResolver)
{
    auto requests = std::vector<DescriptorWriteRequest>{};
    requests.reserve(snapshot.descriptorWriteCount());

    std::ranges::for_each(snapshot.descriptorWrites(), [&](const ShaderBindingRecord &record) {
        auto resolvedPayload = DescriptorWritePayload{};
        auto resolved = std::visit(
            [&](const auto &payload) -> bool {
                using PayloadT = std::remove_cvref_t<decltype(payload)>;
                if constexpr (std::same_as<PayloadT, LogicalResourceDescriptorWrite>)
                {
                    if (!logicalResolver)
                    {
                        return false;
                    }

                    auto resolvedLogical = logicalResolver(payload, record.binding, record.arrayElement);
                    if (!resolvedLogical.has_value())
                    {
                        return false;
                    }

                    resolvedPayload = std::move(*resolvedLogical);
                    return true;
                }
                else
                {
                    resolvedPayload = payload;
                    return true;
                }
            },
            record.payload);

        nrAssert(
            resolved,
            std::format(
                "resolveDescriptorWriteRequests failed to resolve descriptor record at set={}, binding={}, path='{}'.",
                record.binding.set,
                record.binding.binding,
                record.binding.debugPath));

        if (!resolved)
        {
            return;
        }

        requests.push_back(DescriptorWriteRequest{
            .binding = record.binding,
            .arrayElement = record.arrayElement,
            .payload = std::move(resolvedPayload),
        });
    });

    return requests;
}

} // namespace nr::rhi
