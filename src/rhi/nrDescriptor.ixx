module;
export module nr.rhi:descriptor;

import dependency;
import :slang;
import std;

namespace nr::rhi::detail
{
[[nodiscard]] vk::DescriptorType toVkDescriptorType(slang::BindingType bindingType)
{
    switch (bindingType)
    {
    case slang::BindingType::Sampler:
        return vk::DescriptorType::eSampler;
    case slang::BindingType::Texture:
    case slang::BindingType::CombinedTextureSampler:
    case slang::BindingType::InputRenderTarget:
        return vk::DescriptorType::eSampledImage;
    case slang::BindingType::ConstantBuffer:
        return vk::DescriptorType::eUniformBuffer;
    case slang::BindingType::ParameterBlock:
        return vk::DescriptorType::eUniformBufferDynamic;
    case slang::BindingType::TypedBuffer:
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::MutableTypedBuffer:
        return vk::DescriptorType::eStorageBuffer;
    case slang::BindingType::RayTracingAccelerationStructure:
        return vk::DescriptorType::eAccelerationStructureKHR;
    default:
        return vk::DescriptorType::eStorageBuffer;
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

class ShaderDescriptorLayout;

class ShaderCursor
{
  public:
    ShaderCursor() = default;

    [[nodiscard]] bool valid() const noexcept
    {
        return layout_ != nullptr && (isRoot_ || typeLayout_ != nullptr);
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

  private:
    friend class ShaderDescriptorLayout;

    struct RootField
    {
        slang::TypeLayoutReflection *typeLayout = nullptr;
        CursorAddress address{};
        std::string debugPath;
    };

    ShaderCursor(const ShaderDescriptorLayout *layout, RootField field)
        : layout_(layout),
          typeLayout_(field.typeLayout),
          address_(field.address),
          isRoot_(false),
          debugPath_(std::move(field.debugPath))
    {
    }

    explicit ShaderCursor(const ShaderDescriptorLayout *layout)
        : layout_(layout),
          typeLayout_(nullptr),
          address_({}),
          isRoot_(true),
          debugPath_("$root")
    {
    }

    const ShaderDescriptorLayout *layout_ = nullptr;
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

        layout.programLayout_ = programLayout;

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
                if (setIndex < 0 || bindingIndex < 0)
                {
                    continue;
                }

                DescriptorBindingInfo info{};
                info.set = static_cast<uint32_t>(setIndex);
                info.binding = static_cast<uint32_t>(bindingIndex);
                info.descriptorCount = detail::sanitizeDescriptorCount(typeLayout->getBindingRangeBindingCount(rangeIndex));
                info.descriptorType = detail::toVkDescriptorType(typeLayout->getBindingRangeType(rangeIndex));
                info.stageFlags = stageFlags;
                info.bindingRangeIndex = baseBindingRangeIndex + static_cast<uint32_t>(rangeIndex);
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

        collectFromTypeLayout(programLayout->getGlobalParamsTypeLayout(), std::nullopt, "$program");

        if (auto *entryPointLayout = program.entryPointLayout())
        {
            std::optional<SlangStage> stage = std::nullopt;
            std::string entryPointName = "entrypoint";
            if (auto *entryPointBinary = program.entryPointBinary())
            {
                stage = entryPointBinary->stage;
                entryPointName = entryPointBinary->entryPointName.empty() ? std::string("entrypoint") : entryPointBinary->entryPointName;
            }
            collectFromTypeLayout(entryPointLayout->getTypeLayout(), stage, std::format("$entry.{}", entryPointName));
        }

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

        return layout;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return programLayout_ != nullptr;
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

    [[nodiscard]] ShaderCursor rootCursor() const
    {
        return ShaderCursor(this);
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

    slang::ProgramLayout *programLayout_ = nullptr;
    std::unordered_map<std::string, ShaderCursor::RootField> rootFields_;
    std::unordered_map<uint32_t, DescriptorBindingInfo> bindingByRangeIndex_;
    std::map<std::tuple<uint32_t, uint32_t>, DescriptorBindingInfo> bindingBySetAndBinding_;
    std::vector<DescriptorSetLayoutInfo> descriptorSets_;
};

[[nodiscard]] ShaderCursor ShaderCursor::field(std::string_view fieldName) const
{
    if (!valid())
    {
        return {};
    }

    if (isRoot_)
    {
        auto rootField = layout_->findRootField(fieldName);
        if (!rootField.has_value())
        {
            return {};
        }
        return ShaderCursor(layout_, std::move(*rootField));
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
    return layout_->findBindingByRangeIndex(address_.bindingRangeIndex);
}

} // namespace nr::rhi
