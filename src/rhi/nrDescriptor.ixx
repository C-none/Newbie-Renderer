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
    case slang::BindingType::CombinedTextureSampler:
        return bindingType == slang::BindingType::Sampler ? vk::DescriptorType::eSampler : vk::DescriptorType::eCombinedImageSampler;
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

[[nodiscard]] uint32_t sanitizePushConstantSize(size_t byteSize)
{
    if (byteSize == 0 || byteSize == std::numeric_limits<size_t>::max() || byteSize > std::numeric_limits<uint32_t>::max())
    {
        return 0u;
    }
    return static_cast<uint32_t>(byteSize);
}

[[nodiscard]] vk::ShaderStageFlags toVkShaderStageFlags(std::optional<SlangStage> stage)
{
    if (!stage.has_value() || *stage == SLANG_STAGE_NONE)
    {
        return vk::ShaderStageFlagBits::eAll;
    }
    return vk::ShaderStageFlags(toVkShaderStage(*stage));
}

[[nodiscard]] uint32_t sanitizeDescriptorCount(SlangInt descriptorCount)
{
    if (descriptorCount <= 0 || descriptorCount > std::numeric_limits<uint32_t>::max())
    {
        return 1u;
    }
    return static_cast<uint32_t>(descriptorCount);
}

[[nodiscard]] uint32_t sanitizeRangeOffset(SlangInt rangeOffset)
{
    if (rangeOffset <= 0)
    {
        return 0u;
    }
    return static_cast<uint32_t>(rangeOffset);
}

[[nodiscard]] uint32_t sanitizeFieldIndex(SlangInt fieldIndex)
{
    if (fieldIndex < 0)
    {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(fieldIndex);
}

[[nodiscard]] uint32_t sanitizeElementCount(size_t elementCount)
{
    if (elementCount == 0 || elementCount == std::numeric_limits<size_t>::max() || elementCount > std::numeric_limits<uint32_t>::max())
    {
        return 1u;
    }
    return static_cast<uint32_t>(elementCount);
}

[[nodiscard]] std::optional<uint32_t> tryElementCount(size_t elementCount)
{
    if (elementCount == 0 || elementCount == std::numeric_limits<size_t>::max() || elementCount > std::numeric_limits<uint32_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(elementCount);
}

[[nodiscard]] std::optional<size_t> tryLayoutSize(size_t value)
{
    if (value == std::numeric_limits<size_t>::max())
    {
        return std::nullopt;
    }
    return value;
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

using DescriptorWritePayload =
    std::variant<BufferDescriptorWrite, TexelBufferDescriptorWrite, ImageDescriptorWrite, AccelerationStructureDescriptorWrite>;

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

class ShaderBindingPool
{
  public:
    [[nodiscard]] static ShaderBindingPool create(
        const vk::raii::Device &device,
        const ShaderDescriptorLayout &descriptorLayout,
        uint32_t maxSets = 64,
        vk::DescriptorPoolCreateFlags flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);

    [[nodiscard]] ShaderBindingSet allocate(vk::DescriptorSetLayout descriptorSetLayout, uint32_t setIndex) const;

    void update(const ShaderBindingSet &set, std::span<const DescriptorWriteRequest> writeRequests) const;

    void update(const ShaderBindingSet &set, const DescriptorWriteRequest &writeRequest) const;

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    vk::raii::DescriptorPool pool_ = {nullptr};
};

class ShaderResourceWriter
{
  public:
    [[nodiscard]] bool empty() const noexcept { return requests_.empty(); }
    [[nodiscard]] size_t size() const noexcept { return requests_.size(); }
    void clear() { requests_.clear(); }

    [[nodiscard]] std::span<const DescriptorWriteRequest> requests() const noexcept { return requests_; }

    [[nodiscard]] bool bindUniformBuffer(
        const ShaderCursor &cursor,
        const Buffer &buffer,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize);

    [[nodiscard]] bool bindStorageBuffer(
        const ShaderCursor &cursor,
        const Buffer &buffer,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize);

    [[nodiscard]] bool bindUniformTexelBuffer(const ShaderCursor &cursor, vk::BufferView view);

    [[nodiscard]] bool bindStorageTexelBuffer(const ShaderCursor &cursor, vk::BufferView view);

    [[nodiscard]] bool bindUniformTexelBuffer(
        const ShaderCursor &cursor,
        Buffer &buffer,
        vk::Format format,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize,
        std::string_view viewName = {});

    [[nodiscard]] bool bindStorageTexelBuffer(
        const ShaderCursor &cursor,
        Buffer &buffer,
        vk::Format format,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize,
        std::string_view viewName = {});

    [[nodiscard]] bool bindSampledImage(
        const ShaderCursor &cursor,
        const Image &image,
        vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

    [[nodiscard]] bool bindStorageImage(
        const ShaderCursor &cursor,
        const Image &image,
        vk::ImageLayout imageLayout = vk::ImageLayout::eGeneral);

    [[nodiscard]] bool bindSampler(const ShaderCursor &cursor, vk::Sampler sampler);

    [[nodiscard]] bool bindCombinedImageSampler(
        const ShaderCursor &cursor,
        const Image &image,
        vk::Sampler sampler,
        vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

    [[nodiscard]] bool bindAccelerationStructure(const ShaderCursor &cursor, vk::AccelerationStructureKHR accelerationStructure);

    void commit(const ShaderBindingPool &pool, const ShaderBindingSet &set) const;

    void commit(const ShaderBindingPool &pool, std::span<const ShaderBindingSet> sets) const;

  private:
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

    [[nodiscard]] static bool acceptsType(vk::DescriptorType descriptorType, std::initializer_list<vk::DescriptorType> allowed)
    {
        return std::ranges::find(allowed, descriptorType) != allowed.end();
    }

    [[nodiscard]] static std::optional<DescriptorWriteRequest>
    makeRequest(const ShaderCursor &cursor, DescriptorWritePayload payload, std::initializer_list<vk::DescriptorType> allowedTypes);

    bool append(std::optional<DescriptorWriteRequest> request)
    {
        if (!request.has_value())
        {
            return false;
        }
        requests_.push_back(std::move(*request));
        return true;
    }

    std::vector<DescriptorWriteRequest> requests_;
};

class ShaderDescriptorLayout;

class ShaderCursor
{
  public:
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

    struct RootField
    {
        slang::TypeLayoutReflection *typeLayout = nullptr;
        CursorAddress address{};
        std::string debugPath;
    };

    ShaderCursor(const ShaderDescriptorLayout &layout, RootField field)
        : layout_(std::cref(layout)),
          typeLayout_(field.typeLayout),
          address_(field.address),
          isRoot_(false),
          debugPath_(std::move(field.debugPath))
    {
    }

    explicit ShaderCursor(const ShaderDescriptorLayout &layout)
        : layout_(std::cref(layout)),
          typeLayout_(nullptr),
          address_({}),
          isRoot_(true),
          debugPath_("$root")
    {
    }

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
};

class ShaderDescriptorLayout
{
  public:
    [[nodiscard]] static ShaderDescriptorLayout create(const SlangProgram &program)
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

                DescriptorBindingInfo info{};
                info.set = static_cast<uint32_t>(setIndex);
                info.binding = static_cast<uint32_t>(bindingIndex);
                info.descriptorCount = detail::sanitizeDescriptorCount(typeLayout->getBindingRangeBindingCount(rangeIndex));
                info.descriptorType = *descriptorType;
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
                        mergedIt->second.descriptorType == info.descriptorType && mergedIt->second.descriptorCount == info.descriptorCount,
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


        auto groupedSetIndices = layout.bindingBySetAndBinding_ |
                                 std::views::keys |
                                 std::views::transform([](const auto &key) { return std::get<0>(key); }) |
                                 std::ranges::to<std::vector>();
        std::ranges::sort(groupedSetIndices);
        groupedSetIndices.erase(std::unique(groupedSetIndices.begin(), groupedSetIndices.end()), groupedSetIndices.end());

        for (auto setIndex : groupedSetIndices)
        {
            DescriptorSetLayoutInfo setLayout{};
            setLayout.set = setIndex;

            for (auto const &[key, value] : layout.bindingBySetAndBinding_)
            {
                if (std::get<0>(key) == setIndex)
                {
                    setLayout.bindings.push_back(value);
                }
            }

            std::ranges::sort(setLayout.bindings, [](const DescriptorBindingInfo &lhs, const DescriptorBindingInfo &rhs) {
                return lhs.binding < rhs.binding;
            });
            layout.descriptorSets_.push_back(std::move(setLayout));
        }

        std::ranges::sort(layout.descriptorSets_, [](const DescriptorSetLayoutInfo &lhs, const DescriptorSetLayoutInfo &rhs) {
            return lhs.set < rhs.set;
        });

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

    [[nodiscard]] std::span<const PushConstantRangeInfo> pushConstantRanges() const noexcept
    {
        return pushConstantRanges_;
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
    std::unordered_map<std::string, ShaderCursor::RootField> rootFields_;
    std::unordered_map<uint32_t, DescriptorBindingInfo> bindingByRangeIndex_;
    std::unordered_map<uint32_t, PushConstantRangeInfo> pushConstantByRangeIndex_;
    std::map<std::tuple<uint32_t, uint32_t>, PushConstantRangeInfo> pushConstantByOffsetAndSize_;
    std::map<std::tuple<uint32_t, uint32_t>, DescriptorBindingInfo> bindingBySetAndBinding_;
    std::vector<DescriptorSetLayoutInfo> descriptorSets_;
    std::vector<PushConstantRangeInfo> pushConstantRanges_;
};

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
        return ShaderCursor(layoutRef(), std::move(*rootField));
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
    uint32_t maxSets,
    vk::DescriptorPoolCreateFlags flags)
{
    ShaderBindingPool pool;
    pool.device_ = std::cref(device);

    auto descriptorCounts = std::map<vk::DescriptorType, uint32_t>{};
    std::ranges::for_each(descriptorLayout.descriptorSets(), [&](const DescriptorSetLayoutInfo &setInfo) {
        std::ranges::for_each(setInfo.bindings, [&](const DescriptorBindingInfo &bindingInfo) {
            descriptorCounts[bindingInfo.descriptorType] += bindingInfo.descriptorCount * std::max(maxSets, 1u);
        });
    });

    auto poolSizes = descriptorCounts |
                     std::views::transform([](const auto &pair) {
                         return vk::DescriptorPoolSize{pair.first, pair.second};
                     }) |
                     std::ranges::to<std::vector>();

    if (poolSizes.empty())
    {
        poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, std::max(maxSets, 1u)});
    }

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = flags;
    poolInfo.maxSets = std::max(maxSets, 1u);
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    pool.pool_ = vk::raii::DescriptorPool(device, poolInfo);
    return pool;
}

[[nodiscard]] ShaderBindingSet ShaderBindingPool::allocate(vk::DescriptorSetLayout descriptorSetLayout, uint32_t setIndex) const
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

    auto allocatedSets = device_->get().allocateDescriptorSets(allocateInfo);
    if (!allocatedSets.empty())
    {
        set.set_ = allocatedSets.front();
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

    auto bufferWriteCount = std::count_if(
        writeRequests.begin(),
        writeRequests.end(),
        [](const DescriptorWriteRequest &request) {
            return std::holds_alternative<BufferDescriptorWrite>(request.payload);
        });
    auto texelWriteCount = std::count_if(
        writeRequests.begin(),
        writeRequests.end(),
        [](const DescriptorWriteRequest &request) {
            return std::holds_alternative<TexelBufferDescriptorWrite>(request.payload);
        });
    auto imageWriteCount = std::count_if(
        writeRequests.begin(),
        writeRequests.end(),
        [](const DescriptorWriteRequest &request) {
            return std::holds_alternative<ImageDescriptorWrite>(request.payload);
        });
    auto accelerationWriteCount = std::count_if(
        writeRequests.begin(),
        writeRequests.end(),
        [](const DescriptorWriteRequest &request) {
            return std::holds_alternative<AccelerationStructureDescriptorWrite>(request.payload);
        });

    auto vkWrites = std::vector<vk::WriteDescriptorSet>{};
    auto bufferInfos = std::vector<vk::DescriptorBufferInfo>{};
    auto imageInfos = std::vector<vk::DescriptorImageInfo>{};
    auto texelBufferViews = std::vector<vk::BufferView>{};
    auto accelerationHandles = std::vector<vk::AccelerationStructureKHR>{};
    auto accelerationInfos = std::vector<vk::WriteDescriptorSetAccelerationStructureKHR>{};

    vkWrites.reserve(writeRequests.size());
    bufferInfos.reserve(static_cast<size_t>(bufferWriteCount));
    imageInfos.reserve(static_cast<size_t>(imageWriteCount));
    texelBufferViews.reserve(static_cast<size_t>(texelWriteCount));
    accelerationHandles.reserve(static_cast<size_t>(accelerationWriteCount));
    accelerationInfos.reserve(static_cast<size_t>(accelerationWriteCount));

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

[[nodiscard]] bool ShaderResourceWriter::bindUniformBuffer(
    const ShaderCursor &cursor,
    const Buffer &buffer,
    vk::DeviceSize offset,
    vk::DeviceSize range)
{
    nrAssert(buffer.valid(), "ShaderResourceWriter::bindUniformBuffer requires a valid Buffer.");
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    return append(makeRequest(
        cursor,
        BufferDescriptorWrite{.buffer = buffer.handle(), .offset = offset, .range = finalRange},
        {vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eUniformBufferDynamic}));
}

[[nodiscard]] bool ShaderResourceWriter::bindStorageBuffer(
    const ShaderCursor &cursor,
    const Buffer &buffer,
    vk::DeviceSize offset,
    vk::DeviceSize range)
{
    nrAssert(buffer.valid(), "ShaderResourceWriter::bindStorageBuffer requires a valid Buffer.");
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    return append(makeRequest(
        cursor,
        BufferDescriptorWrite{.buffer = buffer.handle(), .offset = offset, .range = finalRange},
        {vk::DescriptorType::eStorageBuffer}));
}

[[nodiscard]] bool ShaderResourceWriter::bindUniformTexelBuffer(const ShaderCursor &cursor, vk::BufferView view)
{
    return append(makeRequest(cursor, TexelBufferDescriptorWrite{.view = view}, {vk::DescriptorType::eUniformTexelBuffer}));
}

[[nodiscard]] bool ShaderResourceWriter::bindStorageTexelBuffer(const ShaderCursor &cursor, vk::BufferView view)
{
    return append(makeRequest(cursor, TexelBufferDescriptorWrite{.view = view}, {vk::DescriptorType::eStorageTexelBuffer}));
}

[[nodiscard]] bool ShaderResourceWriter::bindUniformTexelBuffer(
    const ShaderCursor &cursor,
    Buffer &buffer,
    vk::Format format,
    vk::DeviceSize offset,
    vk::DeviceSize range,
    std::string_view viewName)
{
    nrAssert(buffer.valid(), "ShaderResourceWriter::bindUniformTexelBuffer requires a valid Buffer.");
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    auto view = buffer.addView(format, offset, finalRange, viewName);
    return bindUniformTexelBuffer(cursor, *view.get());
}

[[nodiscard]] bool ShaderResourceWriter::bindStorageTexelBuffer(
    const ShaderCursor &cursor,
    Buffer &buffer,
    vk::Format format,
    vk::DeviceSize offset,
    vk::DeviceSize range,
    std::string_view viewName)
{
    nrAssert(buffer.valid(), "ShaderResourceWriter::bindStorageTexelBuffer requires a valid Buffer.");
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    auto view = buffer.addView(format, offset, finalRange, viewName);
    return bindStorageTexelBuffer(cursor, *view.get());
}

[[nodiscard]] bool ShaderResourceWriter::bindSampledImage(
    const ShaderCursor &cursor,
    const Image &image,
    vk::ImageLayout imageLayout)
{
    nrAssert(image.valid(), "ShaderResourceWriter::bindSampledImage requires a valid Image.");
    return append(makeRequest(
        cursor,
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = {}},
        {vk::DescriptorType::eSampledImage, vk::DescriptorType::eInputAttachment}));
}

[[nodiscard]] bool ShaderResourceWriter::bindStorageImage(
    const ShaderCursor &cursor,
    const Image &image,
    vk::ImageLayout imageLayout)
{
    nrAssert(image.valid(), "ShaderResourceWriter::bindStorageImage requires a valid Image.");
    return append(makeRequest(
        cursor,
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = {}},
        {vk::DescriptorType::eStorageImage}));
}

[[nodiscard]] bool ShaderResourceWriter::bindSampler(const ShaderCursor &cursor, vk::Sampler sampler)
{
    return append(makeRequest(
        cursor,
        ImageDescriptorWrite{.imageView = {}, .imageLayout = vk::ImageLayout::eUndefined, .sampler = sampler},
        {vk::DescriptorType::eSampler}));
}

[[nodiscard]] bool ShaderResourceWriter::bindCombinedImageSampler(
    const ShaderCursor &cursor,
    const Image &image,
    vk::Sampler sampler,
    vk::ImageLayout imageLayout)
{
    nrAssert(image.valid(), "ShaderResourceWriter::bindCombinedImageSampler requires a valid Image.");
    return append(makeRequest(
        cursor,
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = sampler},
        {vk::DescriptorType::eCombinedImageSampler}));
}

[[nodiscard]] bool ShaderResourceWriter::bindAccelerationStructure(const ShaderCursor &cursor, vk::AccelerationStructureKHR accelerationStructure)
{
    return append(makeRequest(
        cursor,
        AccelerationStructureDescriptorWrite{.accelerationStructure = accelerationStructure},
        {vk::DescriptorType::eAccelerationStructureKHR}));
}

void ShaderResourceWriter::commit(const ShaderBindingPool &pool, const ShaderBindingSet &set) const
{
    if (!set.valid() || requests_.empty())
    {
        return;
    }

    auto filtered = requests_ |
        std::views::filter([setIndex = set.setIndex()](const DescriptorWriteRequest &request) {
            return request.binding.set == setIndex;
        }) |
        std::ranges::to<std::vector>();

    if (!filtered.empty())
    {
        pool.update(set, filtered);
    }
}

void ShaderResourceWriter::commit(const ShaderBindingPool &pool, std::span<const ShaderBindingSet> sets) const
{
    for (const auto &set : sets)
    {
        commit(pool, set);
    }
}

[[nodiscard]] std::optional<DescriptorWriteRequest>
ShaderResourceWriter::makeRequest(const ShaderCursor &cursor, DescriptorWritePayload payload, std::initializer_list<vk::DescriptorType> allowedTypes)
{
    auto bindingInfo = cursor.descriptorBinding();
    if (!bindingInfo.has_value() || !acceptsType(bindingInfo->descriptorType, allowedTypes))
    {
        return std::nullopt;
    }

    return DescriptorWriteRequest{
        .binding = *bindingInfo,
        .arrayElement = cursor.address().bindingArrayIndex,
        .payload = std::move(payload),
    };
}

} // namespace nr::rhi
