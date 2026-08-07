module nr.rhi;
import :descriptor;
import dependency.slang;
import dependency.vulkan;
import :slang;
import :resource;
import std;

namespace nr::rhi::detail
{
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

[[nodiscard]] bool isUnboundedDescriptorCount(SlangInt descriptorCount)
{
    return static_cast<std::size_t>(descriptorCount) == slang::unboundedSize;
}

[[nodiscard]] bool isInlineUniformByteCountValid(std::uint32_t byteCount)
{
    return byteCount > 0u && (byteCount % 4u) == 0u;
}

[[nodiscard]] auto makeDescriptorWriteSlotKey(const DescriptorWriteRequest &request) noexcept
{
    return std::tuple{request.binding.set, request.binding.binding, request.arrayElement,
                      request.binding.descriptorType};
}

[[nodiscard]] bool descriptorPayloadMatchesType(const DescriptorWritePayload &payload,
                                                vk::DescriptorType descriptorType) noexcept
{
    return std::visit(
        [descriptorType]<typename Payload>(const Payload &) {
            if constexpr (std::same_as<Payload, BufferDescriptorWrite>)
            {
                return descriptorType == vk::DescriptorType::eUniformBuffer ||
                       descriptorType == vk::DescriptorType::eUniformBufferDynamic ||
                       descriptorType == vk::DescriptorType::eStorageBuffer ||
                       descriptorType == vk::DescriptorType::eStorageBufferDynamic;
            }
            else if constexpr (std::same_as<Payload, TexelBufferDescriptorWrite>)
            {
                return descriptorType == vk::DescriptorType::eUniformTexelBuffer ||
                       descriptorType == vk::DescriptorType::eStorageTexelBuffer;
            }
            else if constexpr (std::same_as<Payload, ImageDescriptorWrite>)
            {
                return descriptorType == vk::DescriptorType::eSampler ||
                       descriptorType == vk::DescriptorType::eCombinedImageSampler ||
                       descriptorType == vk::DescriptorType::eSampledImage ||
                       descriptorType == vk::DescriptorType::eStorageImage ||
                       descriptorType == vk::DescriptorType::eInputAttachment;
            }
            else if constexpr (std::same_as<Payload, AccelerationStructureDescriptorWrite>)
            {
                return descriptorType == vk::DescriptorType::eAccelerationStructureKHR;
            }
            else
            {
                return descriptorType == vk::DescriptorType::eInlineUniformBlock;
            }
        },
        payload);
}

[[nodiscard]] std::uint32_t sanitizePushConstantSize(std::size_t byteSize)
{
    if (byteSize == 0u || byteSize == std::numeric_limits<std::size_t>::max() ||
        !std::in_range<std::uint32_t>(byteSize))
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(byteSize);
}

[[nodiscard]] std::uint32_t sanitizeDescriptorCount(SlangInt descriptorCount)
{
    if (isUnboundedDescriptorCount(descriptorCount))
    {
        return 1u;
    }
    nrAssert(descriptorCount > 0, "Slang reported an invalid descriptor count: {}", descriptorCount);
    nrAssert(std::in_range<std::uint32_t>(descriptorCount),
             "Slang descriptor count exceeds the Vulkan uint32 range: {}", descriptorCount);
    return static_cast<std::uint32_t>(descriptorCount);
}

[[nodiscard]] std::uint32_t sanitizeRangeOffset(SlangInt rangeOffset)
{
    if (rangeOffset < 0)
    {
        return 0u;
    }
    nrAssert(std::in_range<std::uint32_t>(rangeOffset),
             "Slang binding-range offset exceeds uint32: {}", rangeOffset);
    return static_cast<std::uint32_t>(rangeOffset);
}

[[nodiscard]] std::uint32_t sanitizeFieldIndex(SlangInt fieldIndex)
{
    if (fieldIndex < 0 || !std::in_range<std::uint32_t>(fieldIndex))
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(fieldIndex);
}

[[nodiscard]] std::uint32_t sanitizeElementCount(std::size_t elementCount)
{
    if (elementCount == 0u || elementCount == std::numeric_limits<std::size_t>::max() ||
        !std::in_range<std::uint32_t>(elementCount))
    {
        return 1u;
    }
    return static_cast<std::uint32_t>(elementCount);
}

[[nodiscard]] std::optional<std::uint32_t> tryElementCount(std::size_t elementCount)
{
    if (elementCount == 0 || elementCount == std::numeric_limits<std::size_t>::max() ||
        elementCount > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    return static_cast<std::uint32_t>(elementCount);
}

[[nodiscard]] std::optional<std::size_t> tryLayoutSize(std::size_t value)
{
    return value == std::numeric_limits<std::size_t>::max() ? std::nullopt : std::optional<std::size_t>(value);
}

[[nodiscard]] std::optional<std::uint32_t> trySlangLayoutIndex(std::size_t value)
{
    constexpr auto unboundedSize = std::numeric_limits<std::size_t>::max();
    constexpr auto unknownSize = unboundedSize - 1u;
    if (value == unknownSize || value == unboundedSize || value > std::numeric_limits<std::uint32_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::optional<slang::ParameterCategory> bindingLayoutCategory(slang::BindingType bindingType) noexcept
{
    switch (bindingType)
    {
    case slang::BindingType::Sampler:
        return slang::ParameterCategory::SamplerState;
    case slang::BindingType::Texture:
    case slang::BindingType::CombinedTextureSampler:
    case slang::BindingType::TypedBuffer:
    case slang::BindingType::RawBuffer:
    case slang::BindingType::RayTracingAccelerationStructure:
        return slang::ParameterCategory::ShaderResource;
    case slang::BindingType::MutableTexture:
    case slang::BindingType::MutableTypedBuffer:
    case slang::BindingType::MutableRawBuffer:
        return slang::ParameterCategory::UnorderedAccess;
    case slang::BindingType::ConstantBuffer:
    case slang::BindingType::ParameterBlock:
        return slang::ParameterCategory::ConstantBuffer;
    case slang::BindingType::InlineUniformData:
        return slang::ParameterCategory::Uniform;
    case slang::BindingType::InputRenderTarget:
        return slang::ParameterCategory::InputAttachmentIndex;
    default:
        return std::nullopt;
    }
}
} // namespace nr::rhi::detail

namespace nr::rhi
{
void DescriptorWriteCache::clear() noexcept
{
    payloadsBySlot_.clear();
}

[[nodiscard]] std::vector<DescriptorWriteRequest> DescriptorWriteCache::filterChanged(
    std::span<const DescriptorWriteRequest> writeRequests) const
{
    auto changedWrites = std::vector<DescriptorWriteRequest>{};
    changedWrites.reserve(writeRequests.size());

    std::ranges::for_each(writeRequests, [&](const DescriptorWriteRequest &request) {
        auto slotKey = detail::makeDescriptorWriteSlotKey(request);
        auto cachedPayload = payloadsBySlot_.find(slotKey);
        if (!request.forceWrite && cachedPayload != payloadsBySlot_.end() && cachedPayload->second == request.payload)
        {
            return;
        }

        changedWrites.push_back(request);
    });

    return changedWrites;
}

void DescriptorWriteCache::commit(std::span<const DescriptorWriteRequest> writeRequests)
{
    std::ranges::for_each(writeRequests, [&](const DescriptorWriteRequest &request) {
        auto slotKey = detail::makeDescriptorWriteSlotKey(request);
        payloadsBySlot_.insert_or_assign(std::move(slotKey), request.payload);
    });
}

[[nodiscard]] std::string_view shaderDescriptorSemanticName(ShaderDescriptorSemantic semantic) noexcept
{
    switch (semantic)
    {
    case ShaderDescriptorSemantic::Sampler:
        return "Sampler";
    case ShaderDescriptorSemantic::CombinedImageSampler:
        return "CombinedImageSampler";
    case ShaderDescriptorSemantic::SampledImage:
        return "SampledImage";
    case ShaderDescriptorSemantic::StorageImage:
        return "StorageImage";
    case ShaderDescriptorSemantic::UniformTexelBuffer:
        return "UniformTexelBuffer";
    case ShaderDescriptorSemantic::StorageTexelBuffer:
        return "StorageTexelBuffer";
    case ShaderDescriptorSemantic::UniformBuffer:
        return "UniformBuffer";
    case ShaderDescriptorSemantic::StorageBuffer:
        return "StorageBuffer";
    case ShaderDescriptorSemantic::DynamicUniformBuffer:
        return "DynamicUniformBuffer";
    case ShaderDescriptorSemantic::DynamicStorageBuffer:
        return "DynamicStorageBuffer";
    case ShaderDescriptorSemantic::InputAttachment:
        return "InputAttachment";
    case ShaderDescriptorSemantic::InlineUniformBlock:
        return "InlineUniformBlock";
    case ShaderDescriptorSemantic::AccelerationStructure:
        return "AccelerationStructure";
    case ShaderDescriptorSemantic::Unsupported:
        return "Unsupported";
    default:
        return "Unknown";
    }
}

[[nodiscard]] ShaderDescriptorSemantic descriptorSemantic(vk::DescriptorType descriptorType) noexcept
{
    switch (descriptorType)
    {
    case vk::DescriptorType::eSampler:
        return ShaderDescriptorSemantic::Sampler;
    case vk::DescriptorType::eCombinedImageSampler:
        return ShaderDescriptorSemantic::CombinedImageSampler;
    case vk::DescriptorType::eSampledImage:
        return ShaderDescriptorSemantic::SampledImage;
    case vk::DescriptorType::eStorageImage:
        return ShaderDescriptorSemantic::StorageImage;
    case vk::DescriptorType::eUniformTexelBuffer:
        return ShaderDescriptorSemantic::UniformTexelBuffer;
    case vk::DescriptorType::eStorageTexelBuffer:
        return ShaderDescriptorSemantic::StorageTexelBuffer;
    case vk::DescriptorType::eUniformBuffer:
        return ShaderDescriptorSemantic::UniformBuffer;
    case vk::DescriptorType::eStorageBuffer:
        return ShaderDescriptorSemantic::StorageBuffer;
    case vk::DescriptorType::eUniformBufferDynamic:
        return ShaderDescriptorSemantic::DynamicUniformBuffer;
    case vk::DescriptorType::eStorageBufferDynamic:
        return ShaderDescriptorSemantic::DynamicStorageBuffer;
    case vk::DescriptorType::eInputAttachment:
        return ShaderDescriptorSemantic::InputAttachment;
    case vk::DescriptorType::eInlineUniformBlock:
        return ShaderDescriptorSemantic::InlineUniformBlock;
    case vk::DescriptorType::eAccelerationStructureKHR:
        return ShaderDescriptorSemantic::AccelerationStructure;
    default:
        return ShaderDescriptorSemantic::Unsupported;
    }
}

[[nodiscard]] bool supportsImmutableSampler(vk::DescriptorType descriptorType) noexcept
{
    return descriptorType == vk::DescriptorType::eSampler ||
           descriptorType == vk::DescriptorType::eCombinedImageSampler;
}

[[nodiscard]] std::optional<std::uint32_t> runtimeDescriptorArraySetFor(ShaderDescriptorSemantic semantic) noexcept
{
    switch (semantic)
    {
    case ShaderDescriptorSemantic::Sampler:
        return 0u;
    case ShaderDescriptorSemantic::CombinedImageSampler:
    case ShaderDescriptorSemantic::SampledImage:
        return 1u;
    case ShaderDescriptorSemantic::StorageImage:
        return 2u;
    case ShaderDescriptorSemantic::UniformTexelBuffer:
    case ShaderDescriptorSemantic::StorageTexelBuffer:
    case ShaderDescriptorSemantic::UniformBuffer:
    case ShaderDescriptorSemantic::StorageBuffer:
    case ShaderDescriptorSemantic::DynamicUniformBuffer:
    case ShaderDescriptorSemantic::DynamicStorageBuffer:
        return 3u;
    case ShaderDescriptorSemantic::AccelerationStructure:
        return 4u;
    case ShaderDescriptorSemantic::InputAttachment:
    case ShaderDescriptorSemantic::InlineUniformBlock:
    case ShaderDescriptorSemantic::Unsupported:
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool DescriptorBindingInfo::supportsVariableDescriptorCount() const noexcept
{
    return (bindingFlags & vk::DescriptorBindingFlagBits::eVariableDescriptorCount) ==
           vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
}

[[nodiscard]] bool DescriptorBindingInfo::isPartiallyBound() const noexcept
{
    return (bindingFlags & vk::DescriptorBindingFlagBits::ePartiallyBound) ==
           vk::DescriptorBindingFlagBits::ePartiallyBound;
}

[[nodiscard]] ShaderDescriptorSemantic DescriptorBindingInfo::semantic() const noexcept
{
    return descriptorSemantic(descriptorType);
}

[[nodiscard]] bool DescriptorBindingInfo::supportsImmutableSampler() const noexcept
{
    return nr::rhi::supportsImmutableSampler(descriptorType);
}

[[nodiscard]] bool ShaderBindingSet::valid() const noexcept
{
    return *set_ != nullptr;
}

[[nodiscard]] vk::DescriptorSet ShaderBindingSet::raw() const noexcept
{
    return *set_;
}

[[nodiscard]] std::uint32_t ShaderBindingSet::setIndex() const noexcept
{
    return setIndex_;
}

[[nodiscard]] std::uint32_t ShaderBindingSet::descriptorCapacity(
    const DescriptorBindingInfo &bindingInfo) const noexcept
{
    auto it = allocatedDescriptorCountByBinding_.find(bindingInfo.binding);
    if (it != allocatedDescriptorCountByBinding_.end())
    {
        return it->second;
    }
    return bindingInfo.descriptorCount;
}

[[nodiscard]] ShaderDescriptorLayout ShaderDescriptorLayout::create(const SlangProgram &program,
                                                                    DescriptorBindingPolicy policy,
                                                                    std::span<const SlangImmutableSamplerBinding> immutableSamplers)
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
    layout.reflectionProgram_ = program;

    constexpr auto scopeName = std::string_view{"$program"};
    constexpr auto stageFlags = vk::ShaderStageFlags{vk::ShaderStageFlagBits::eAll};
    auto collectFromTypeLayout = [&](slang::TypeLayoutReflection *typeLayout) {
        if (!typeLayout)
        {
            return;
        }

        auto bindingRangeCount = std::max<SlangInt>(0, typeLayout->getBindingRangeCount());
        auto fieldCount = std::max<SlangInt>(0, typeLayout->getFieldCount());
        nrAssert(std::in_range<std::uint32_t>(bindingRangeCount) && std::in_range<unsigned int>(fieldCount),
                 "Slang program reflection counts exceed host index ranges. bindingRanges={}, fields={}",
                 bindingRangeCount, fieldCount);
        auto fieldLayoutByBindingRangeOffset =
            std::map<std::uint32_t, std::reference_wrapper<slang::VariableLayoutReflection>>{};

        std::ranges::for_each(std::views::iota(SlangInt{0}, fieldCount), [&](SlangInt fieldIndex) {
            auto *fieldLayout = typeLayout->getFieldByIndex(static_cast<unsigned int>(fieldIndex));
            auto fieldBindingRangeOffset = typeLayout->getFieldBindingRangeOffset(fieldIndex);
            if (!fieldLayout || fieldBindingRangeOffset < 0 || fieldBindingRangeOffset >= bindingRangeCount)
            {
                return;
            }
            fieldLayoutByBindingRangeOffset.try_emplace(static_cast<std::uint32_t>(fieldBindingRangeOffset),
                                                        std::ref(*fieldLayout));
        });

        for (SlangInt rangeIndex = 0; rangeIndex < bindingRangeCount; ++rangeIndex)
        {
            auto *leafVariable = typeLayout->getBindingRangeLeafVariable(rangeIndex);
            auto bindingRangeDebugPath =
                leafVariable && leafVariable->getName()
                    ? std::format("{}::{}::bindingRange[{}]", scopeName, leafVariable->getName(), rangeIndex)
                    : std::format("{}::bindingRange[{}]", scopeName, rangeIndex);
            auto bindingType = typeLayout->getBindingRangeType(rangeIndex);
            auto bindingRangeIndex = static_cast<std::uint32_t>(rangeIndex);

            if (bindingType == slang::BindingType::PushConstant)
            {
                auto *pushConstantBufferTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(rangeIndex);
                nrAssert(pushConstantBufferTypeLayout != nullptr,
                         "PushConstant binding range {} has null leaf type layout in '{}'", rangeIndex, scopeName);

                auto *elementTypeLayout =
                    pushConstantBufferTypeLayout ? pushConstantBufferTypeLayout->getElementTypeLayout() : nullptr;
                if (elementTypeLayout)
                {
                    pushConstantBufferTypeLayout = elementTypeLayout;
                }

                auto pushConstantSize = detail::sanitizePushConstantSize(
                    pushConstantBufferTypeLayout
                        ? pushConstantBufferTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM)
                        : 0);
                nrAssert(pushConstantSize > 0, "Invalid push constant size in '{}' (size={})", bindingRangeDebugPath,
                         pushConstantBufferTypeLayout
                             ? pushConstantBufferTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM)
                             : std::size_t(0));
                nrAssert(pushConstantSize <= kMaxPushConstantBytes,
                         "Push constant range in '{}' is {} bytes, but Newbie-Renderer allows at most {} bytes. "
                         "Move larger payload fields to frame uniforms or buffer/texture upload paths.",
                         bindingRangeDebugPath, pushConstantSize, kMaxPushConstantBytes);

                auto key = std::tuple<std::uint32_t, std::uint32_t>{0u, pushConstantSize};
                auto mergedIt = layout.pushConstantByOffsetAndSize_.find(key);
                PushConstantRangeInfo info{
                    .size = pushConstantSize,
                    .stageFlags = stageFlags,
                    .bindingRangeIndex = bindingRangeIndex,
                    .debugPath = bindingRangeDebugPath,
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

            auto descriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(rangeIndex);
            auto firstDescriptorRangeIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(rangeIndex);
            auto descriptorRangeCount = typeLayout->getBindingRangeDescriptorRangeCount(rangeIndex);
            nrAssert(descriptorSetIndex >= 0 && firstDescriptorRangeIndex >= 0 && descriptorRangeCount > 0,
                     "Descriptor binding range '{}' has invalid Slang descriptor indexing: setRange={}, "
                     "firstRange={}, rangeCount={}.",
                     bindingRangeDebugPath, descriptorSetIndex, firstDescriptorRangeIndex, descriptorRangeCount);
            nrAssert(descriptorRangeCount == 1,
                     "Descriptor binding range '{}' expands to {} descriptor ranges; exactly one is "
                     "required by the RHI binding model.",
                     bindingRangeDebugPath, descriptorRangeCount);

            auto descriptorRangeIndex = firstDescriptorRangeIndex;
            auto setIndex = typeLayout->getDescriptorSetSpaceOffset(descriptorSetIndex);
            auto bindingIndex =
                typeLayout->getDescriptorSetDescriptorRangeIndexOffset(descriptorSetIndex, descriptorRangeIndex);
            auto descriptorBindingType =
                typeLayout->getDescriptorSetDescriptorRangeType(descriptorSetIndex, descriptorRangeIndex);
            auto descriptorCategory = detail::bindingLayoutCategory(descriptorBindingType)
                                          .value_or(typeLayout->getDescriptorSetDescriptorRangeCategory(
                                              descriptorSetIndex, descriptorRangeIndex));
            if (auto fieldLayoutIt = fieldLayoutByBindingRangeOffset.find(static_cast<std::uint32_t>(rangeIndex));
                fieldLayoutIt != fieldLayoutByBindingRangeOffset.end() &&
                descriptorCategory != slang::ParameterCategory::None)
            {
                auto &fieldLayout = fieldLayoutIt->second.get();
                auto fieldSetIndex = detail::trySlangLayoutIndex(fieldLayout.getBindingSpace(descriptorCategory));
                auto fieldBindingIndex = detail::trySlangLayoutIndex(fieldLayout.getOffset(descriptorCategory));
                if (fieldSetIndex.has_value() && fieldBindingIndex.has_value())
                {
                    if (setIndex == 0 && *fieldSetIndex != 0u)
                    {
                        // Slang descriptor-set ranges can report fallback set0/binding0 for globals
                        // from imported modules; the field layout preserves the source set. A
                        // non-zero descriptor-range set remains authoritative because Slang can
                        // report category-local set0 for explicitly bound UAV/SRV globals.
                        setIndex = static_cast<SlangInt>(*fieldSetIndex);
                        bindingIndex = static_cast<SlangInt>(*fieldBindingIndex);
                    }
                }
            }
            nrAssert(setIndex >= 0 && bindingIndex >= 0,
                     "Descriptor binding range '{}' has negative Vulkan coordinates: set={}, binding={}.",
                     bindingRangeDebugPath, setIndex, bindingIndex);
            nrAssert(std::in_range<std::uint32_t>(setIndex) && std::in_range<std::uint32_t>(bindingIndex),
                     "Descriptor set/binding index exceeds Vulkan uint32 range in '{}': set={}, binding={}",
                     bindingRangeDebugPath, setIndex, bindingIndex);

            auto descriptorType = detail::toVkDescriptorType(descriptorBindingType);
            nrAssert(descriptorType.has_value(), "Unsupported Slang descriptor binding type {} in '{}'.",
                     static_cast<std::int32_t>(descriptorBindingType), bindingRangeDebugPath);

            auto descriptorCountRaw =
                typeLayout->getDescriptorSetDescriptorRangeDescriptorCount(descriptorSetIndex, descriptorRangeIndex);

            DescriptorBindingInfo info{};
            info.set = static_cast<std::uint32_t>(setIndex);
            info.binding = static_cast<std::uint32_t>(bindingIndex);
            info.descriptorCount = detail::sanitizeDescriptorCount(descriptorCountRaw);
            info.isRuntimeSized = detail::isUnboundedDescriptorCount(descriptorCountRaw);
            info.descriptorType = *descriptorType;
            info.stageFlags = info.descriptorType == vk::DescriptorType::eInputAttachment
                                  ? vk::ShaderStageFlags{vk::ShaderStageFlagBits::eFragment}
                                  : stageFlags;
            info.bindingRangeIndex = bindingRangeIndex;
            info.debugPath = bindingRangeDebugPath;
            if (info.isRuntimeSized)
            {
                auto expectedSet = runtimeDescriptorArraySetFor(info.semantic());
                nrAssert(expectedSet.has_value(), "Runtime descriptor array '{}' has unsupported descriptor semantic {}.",
                         info.debugPath, shaderDescriptorSemanticName(info.semantic()));
                nrAssert(info.set == *expectedSet,
                         "Runtime descriptor array '{}' uses set {}, but the semantic multi-set ABI requires set {} for "
                         "{} descriptors. "
                         "Update the shader [[vk::binding(binding, set)]] declaration instead of remapping it in RHI.",
                         info.debugPath, info.set, *expectedSet, shaderDescriptorSemanticName(info.semantic()));
            }
            if (info.isRuntimeSized)
            {
                nrAssert(policy.defaultRuntimeDescriptorCount > 0u,
                         "Runtime descriptor arrays require a non-zero configured descriptor count.");
                info.descriptorCount = policy.defaultRuntimeDescriptorCount;
                info.bindingFlags |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
                info.bindingFlags |= vk::DescriptorBindingFlagBits::ePartiallyBound;
            }
            if (info.descriptorType == vk::DescriptorType::eInlineUniformBlock)
            {
                nrAssert(detail::isInlineUniformByteCountValid(info.descriptorCount),
                         "Inline uniform descriptor byte count must be > 0 and multiple of 4. set={}, "
                         "binding={}, count={}",
                         info.set, info.binding, info.descriptorCount);
            }

            auto key = std::tuple<std::uint32_t, std::uint32_t>{info.set, info.binding};
            auto mergedIt = layout.bindingBySetAndBinding_.find(key);
            if (mergedIt == layout.bindingBySetAndBinding_.end())
            {
                layout.bindingBySetAndBinding_.insert_or_assign(key, info);
            }
            else
            {
                nrAssert(mergedIt->second.descriptorType == info.descriptorType &&
                             mergedIt->second.descriptorCount == info.descriptorCount &&
                             mergedIt->second.bindingFlags == info.bindingFlags &&
                             mergedIt->second.isRuntimeSized == info.isRuntimeSized,
                         "Descriptor layout mismatch at set={}, binding={} when merging '{}' (type={}, "
                         "count={}, runtime={}) with existing '{}' (type={}, count={}, runtime={}).",
                         info.set, info.binding, info.debugPath, vk::to_string(info.descriptorType),
                         info.descriptorCount, info.isRuntimeSized, mergedIt->second.debugPath,
                         vk::to_string(mergedIt->second.descriptorType), mergedIt->second.descriptorCount,
                         mergedIt->second.isRuntimeSized);
                mergedIt->second.stageFlags |= info.stageFlags;
                info = mergedIt->second;
            }

            layout.bindingByRangeIndex_.insert_or_assign(info.bindingRangeIndex, info);
        }

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
                name, ShaderCursor::RootField{
                          .typeLayout = fieldTypeLayout,
                          .address =
                              CursorAddress{
                                  .uniformOffset = fieldLayout->getOffset(),
                                  .bindingRangeIndex =
                                      detail::sanitizeRangeOffset(typeLayout->getFieldBindingRangeOffset(fieldIndex)),
                              },
                          .debugPath = std::format("{}.{}", scopeName, name),
                      });

            nrAssert(inserted.second,
                     "Shader parameter name conflict detected for '{}'. Program-level and entrypoint-level resources "
                     "must not share names.",
                     name);
        }
    };

    auto *globalParamsVarLayout = programLayout->getGlobalParamsVarLayout();
    nrAssert(globalParamsVarLayout != nullptr,
             "ShaderDescriptorLayout::create requires ProgramLayout::getGlobalParamsVarLayout().");
    collectFromTypeLayout(globalParamsVarLayout ? globalParamsVarLayout->getTypeLayout() : nullptr);

    auto immutableSamplerTargets = std::set<std::tuple<std::uint32_t, std::uint32_t>>{};
    std::ranges::for_each(immutableSamplers, [&](const SlangImmutableSamplerBinding &immutableSampler) {
        auto key = std::tuple{immutableSampler.set, immutableSampler.binding};
        nrAssert(immutableSamplerTargets.insert(key).second,
                 "Duplicate immutable sampler binding at set={}, binding={}.", immutableSampler.set,
                 immutableSampler.binding);
        auto bindingIt = layout.bindingBySetAndBinding_.find(key);
        nrAssert(bindingIt != layout.bindingBySetAndBinding_.end(),
                 "Immutable sampler binding not found at set={}, binding={}.", immutableSampler.set,
                 immutableSampler.binding);
        nrAssert(bindingIt->second.supportsImmutableSampler(),
                 "Immutable sampler binding at set={}, binding={} targets descriptor type {}.", immutableSampler.set,
                 immutableSampler.binding, vk::to_string(bindingIt->second.descriptorType));
        nrAssert(bindingIt->second.descriptorCount == immutableSampler.descriptorCount,
                 "Immutable sampler descriptor count mismatch at set={}, binding={}: layout={}, immutable={}.",
                 immutableSampler.set, immutableSampler.binding, bindingIt->second.descriptorCount,
                 immutableSampler.descriptorCount);
        bindingIt->second.usesImmutableSampler = true;
        std::ranges::for_each(layout.bindingByRangeIndex_, [&](auto &entry) {
            auto &binding = entry.second;
            if (binding.set == immutableSampler.set && binding.binding == immutableSampler.binding)
            {
                binding.usesImmutableSampler = true;
            }
        });
    });

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
    nrAssert(layout.pushConstantRanges_.size() <= 1u,
             "ShaderDescriptorLayout supports one canonical push-constant block per shader compile unit; reflected "
             "blocks with different sizes would produce overlapping Vulkan ranges.");

    std::ranges::for_each(layout.descriptorSets_, [](const DescriptorSetLayoutInfo &setInfo) {
        auto variableBindings = setInfo.bindings | std::views::filter([](const DescriptorBindingInfo &bindingInfo) {
                                    return bindingInfo.supportsVariableDescriptorCount();
                                }) |
                                std::ranges::to<std::vector>();
        nrAssert(variableBindings.size() <= 1u,
                 "ShaderDescriptorLayout::create supports at most one variable descriptor-count binding per set. "
                 "set={}, count={}",
                 setInfo.set, variableBindings.size());
        if (variableBindings.empty())
        {
            return;
        }

        auto maxBinding =
            std::ranges::max(setInfo.bindings | std::views::transform([](const DescriptorBindingInfo &bindingInfo) {
                                 return bindingInfo.binding;
                             }));
        nrAssert(variableBindings.front().binding == maxBinding,
                 "Variable descriptor-count binding must be the largest binding number in the set. set={}, "
                 "binding={}, maxBinding={}",
                 setInfo.set, variableBindings.front().binding, maxBinding);
    });

    std::ranges::sort(layout.pushConstantRanges_,
                      [](const PushConstantRangeInfo &lhs, const PushConstantRangeInfo &rhs) {
                          if (lhs.offset == rhs.offset)
                          {
                              return lhs.size < rhs.size;
                          }
                          return lhs.offset < rhs.offset;
                      });

    return layout;
}

[[nodiscard]] bool ShaderDescriptorLayout::valid() const noexcept
{
    return isValid_;
}

[[nodiscard]] std::span<const DescriptorSetLayoutInfo> ShaderDescriptorLayout::descriptorSets() const noexcept
{
    return descriptorSets_;
}

[[nodiscard]] std::vector<vk::DescriptorSetLayoutBinding> ShaderDescriptorLayout::makeVkSetLayoutBindings(
    std::uint32_t setIndex) const
{
    auto it = std::ranges::find_if(
        descriptorSets_, [setIndex](const DescriptorSetLayoutInfo &setLayout) { return setLayout.set == setIndex; });
    if (it == std::ranges::end(descriptorSets_))
    {
        return {};
    }

    return it->bindings | std::views::transform([](const DescriptorBindingInfo &bindingInfo) {
               vk::DescriptorSetLayoutBinding binding{};
               binding.binding = bindingInfo.binding;
               binding.descriptorType = bindingInfo.descriptorType;
               binding.descriptorCount = bindingInfo.descriptorCount;
               binding.stageFlags = bindingInfo.stageFlags;
               return binding;
           }) |
           std::ranges::to<std::vector>();
}

[[nodiscard]] std::vector<vk::DescriptorBindingFlags> ShaderDescriptorLayout::makeVkSetLayoutBindingFlags(
    std::uint32_t setIndex) const
{
    auto it = std::ranges::find_if(
        descriptorSets_, [setIndex](const DescriptorSetLayoutInfo &setLayout) { return setLayout.set == setIndex; });
    if (it == std::ranges::end(descriptorSets_))
    {
        return {};
    }

    return it->bindings |
           std::views::transform([](const DescriptorBindingInfo &bindingInfo) { return bindingInfo.bindingFlags; }) |
           std::ranges::to<std::vector>();
}

[[nodiscard]] std::optional<PushConstantRangeInfo> ShaderDescriptorLayout::pushConstantRange(
    const ShaderCursor &cursor) const
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

[[nodiscard]] ShaderLayoutAbiSignature ShaderDescriptorLayout::abiSignature() const
{
    ShaderLayoutAbiSignature signature;

    std::ranges::for_each(descriptorSets_, [&](const DescriptorSetLayoutInfo &setInfo) {
        std::ranges::for_each(setInfo.bindings, [&](const DescriptorBindingInfo &bindingInfo) {
            signature.descriptorBindings.push_back(ShaderDescriptorAbiBinding{
                .set = bindingInfo.set,
                .binding = bindingInfo.binding,
                .descriptorCount = bindingInfo.descriptorCount,
                .isRuntimeSized = bindingInfo.isRuntimeSized,
                .descriptorType = bindingInfo.descriptorType,
                .stageFlags = bindingInfo.stageFlags,
                .bindingFlags = bindingInfo.bindingFlags,
            });
        });
    });

    signature.pushConstantRanges = pushConstantRanges_ |
                                   std::views::transform([](const PushConstantRangeInfo &rangeInfo) {
                                       return ShaderPushConstantAbiRange{
                                           .offset = rangeInfo.offset,
                                           .size = rangeInfo.size,
                                           .stageFlags = rangeInfo.stageFlags,
                                       };
                                   }) |
                                   std::ranges::to<std::vector>();

    std::ranges::sort(signature.descriptorBindings, [](const ShaderDescriptorAbiBinding &lhs,
                                                       const ShaderDescriptorAbiBinding &rhs) { return lhs < rhs; });
    std::ranges::sort(signature.pushConstantRanges, [](const ShaderPushConstantAbiRange &lhs,
                                                       const ShaderPushConstantAbiRange &rhs) { return lhs < rhs; });
    return signature;
}

[[nodiscard]] std::vector<vk::PushConstantRange> ShaderDescriptorLayout::makeVkPushConstantRanges() const
{
    return pushConstantRanges_ | std::views::transform([](const PushConstantRangeInfo &pushConstantInfo) {
               vk::PushConstantRange range{};
               range.stageFlags = pushConstantInfo.stageFlags;
               range.offset = pushConstantInfo.offset;
               range.size = pushConstantInfo.size;
               return range;
           }) |
           std::ranges::to<std::vector>();
}

[[nodiscard]] ShaderCursor ShaderDescriptorLayout::rootCursor() const
{
    nrAssert(valid(), "ShaderDescriptorLayout::rootCursor requires a valid descriptor layout.");
    return ShaderCursor(*this);
}

[[nodiscard]] bool shaderLayoutAbiEquivalent(const ShaderLayoutAbiSignature &lhs,
                                             const ShaderLayoutAbiSignature &rhs) noexcept
{
    return lhs == rhs;
}

namespace
{
[[nodiscard]] std::string describeAbiBinding(const nr::rhi::ShaderDescriptorAbiBinding &binding)
{
    return std::format("set={}, binding={}, type={}, count={}, runtime={}, bindingFlags=0x{:x}, stageFlags=0x{:x}",
                       binding.set, binding.binding, vk::to_string(binding.descriptorType), binding.descriptorCount,
                       binding.isRuntimeSized, static_cast<std::uint32_t>(binding.bindingFlags),
                       static_cast<std::uint32_t>(binding.stageFlags));
}

[[nodiscard]] std::string describeAbiPushConstantRange(const nr::rhi::ShaderPushConstantAbiRange &range)
{
    return std::format("offset={}, size={}, stageFlags=0x{:x}", range.offset, range.size,
                       static_cast<std::uint32_t>(range.stageFlags));
}
} // namespace

[[nodiscard]] std::string describeShaderLayoutAbiDifference(const ShaderLayoutAbiSignature &baseline,
                                                            const ShaderLayoutAbiSignature &variant)
{
    if (baseline.descriptorBindings.size() != variant.descriptorBindings.size())
    {
        return std::format("descriptor binding count differs: baseline={}, variant={}",
                           baseline.descriptorBindings.size(), variant.descriptorBindings.size());
    }

    auto bindingMismatch = std::ranges::mismatch(baseline.descriptorBindings, variant.descriptorBindings);
    if (bindingMismatch.in1 != baseline.descriptorBindings.end() &&
        bindingMismatch.in2 != variant.descriptorBindings.end())
    {
        return std::format("descriptor binding differs: baseline=[{}], variant=[{}]",
                           describeAbiBinding(*bindingMismatch.in1), describeAbiBinding(*bindingMismatch.in2));
    }

    if (baseline.pushConstantRanges.size() != variant.pushConstantRanges.size())
    {
        return std::format("push constant range count differs: baseline={}, variant={}",
                           baseline.pushConstantRanges.size(), variant.pushConstantRanges.size());
    }

    auto pushConstantMismatch = std::ranges::mismatch(baseline.pushConstantRanges, variant.pushConstantRanges);
    if (pushConstantMismatch.in1 != baseline.pushConstantRanges.end() &&
        pushConstantMismatch.in2 != variant.pushConstantRanges.end())
    {
        return std::format("push constant range differs: baseline=[{}], variant=[{}]",
                           describeAbiPushConstantRange(*pushConstantMismatch.in1),
                           describeAbiPushConstantRange(*pushConstantMismatch.in2));
    }

    return {};
}

void assertShaderLayoutAbiStable(const SlangProgram &baselineProgram, const SlangProgram &variantProgram,
                                 DescriptorBindingPolicy policy, std::string_view debugName)
{
    auto baselineLayout = ShaderDescriptorLayout::create(baselineProgram, policy);
    auto variantLayout = ShaderDescriptorLayout::create(variantProgram, policy);
    nrAssert(baselineLayout.valid(), "Shader variant ABI validation requires a valid baseline layout.");
    nrAssert(variantLayout.valid(), "Shader variant ABI validation requires a valid variant layout.");

    auto baselineSignature = baselineLayout.abiSignature();
    auto variantSignature = variantLayout.abiSignature();
    if (shaderLayoutAbiEquivalent(baselineSignature, variantSignature))
    {
        return;
    }

    auto diff = describeShaderLayoutAbiDifference(baselineSignature, variantSignature);
    auto message = std::format("Shader variant changed descriptor/push-constant ABI. debugName='{}', difference='{}'",
                               debugName, diff.empty() ? "unknown" : diff);
    nrLog<LogLevel::warning, "RHI">("{}", message);
    nrAssert(false, "{}", message);
}

[[nodiscard]] std::optional<ShaderCursor::RootField> ShaderDescriptorLayout::findRootField(
    std::string_view fieldName) const
{
    auto it = rootFields_.find(std::string(fieldName));
    if (it == rootFields_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

[[nodiscard]] std::optional<DescriptorBindingInfo> ShaderDescriptorLayout::findBindingByRangeIndex(
    std::uint32_t bindingRangeIndex) const
{
    auto it = bindingByRangeIndex_.find(bindingRangeIndex);
    if (it == bindingByRangeIndex_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

[[nodiscard]] ShaderBindingPool ShaderBindingPool::create(const vk::raii::Device &device,
                                                          const ShaderDescriptorLayout &descriptorLayout,
                                                          std::uint32_t maxSets)
{
    nrAssert(*device != nullptr, "ShaderBindingPool::create requires a valid Vulkan device.");
    nrAssert(descriptorLayout.valid(), "ShaderBindingPool::create requires a valid descriptor layout.");

    ShaderBindingPool pool;
    pool.device_ = std::cref(device);

    nrAssert(maxSets > 0u, "ShaderBindingPool::create requires maxSets > 0.");

    auto descriptorCounts = std::map<vk::DescriptorType, std::uint32_t>{};
    std::uint32_t inlineUniformBindingCount = 0;
    std::ranges::for_each(descriptorLayout.descriptorSets(), [&](const DescriptorSetLayoutInfo &setInfo) {
        std::ranges::for_each(setInfo.bindings, [&](const DescriptorBindingInfo &bindingInfo) {
            const bool isVariableCount = bindingInfo.supportsVariableDescriptorCount();
            auto const [_, inserted] =
                pool.bindings_.emplace(std::tuple{bindingInfo.set, bindingInfo.binding}, bindingInfo);
            nrAssert(inserted, "Duplicate descriptor pool binding at set={}, binding={}.", bindingInfo.set,
                     bindingInfo.binding);
            const std::uint32_t effectiveDescriptorCount = bindingInfo.descriptorCount;
            nrAssert(effectiveDescriptorCount > 0u,
                     "ShaderBindingPool::create requires a non-zero descriptor count. set={}, binding={}",
                     bindingInfo.set, bindingInfo.binding);
            auto scaledDescriptorCount = static_cast<std::uint64_t>(effectiveDescriptorCount) * maxSets;
            auto &descriptorCount = descriptorCounts[bindingInfo.descriptorType];
            nrAssert(scaledDescriptorCount <= std::numeric_limits<std::uint32_t>::max() - descriptorCount,
                     "Descriptor pool size overflows uint32 for type {}. accumulated={}, perSet={}, maxSets={}",
                     vk::to_string(bindingInfo.descriptorType), descriptorCount, effectiveDescriptorCount, maxSets);
            descriptorCount += static_cast<std::uint32_t>(scaledDescriptorCount);
            if (isVariableCount)
            {
                auto &bindingCaps = pool.variableDescriptorCapBySetAndBinding_[setInfo.set];
                auto &cap = bindingCaps[bindingInfo.binding];
                cap = std::max(cap, effectiveDescriptorCount);
            }
            if (bindingInfo.descriptorType == vk::DescriptorType::eInlineUniformBlock)
            {
                nrAssert(maxSets <= std::numeric_limits<std::uint32_t>::max() - inlineUniformBindingCount,
                         "Inline-uniform descriptor pool binding count overflows uint32.");
                inlineUniformBindingCount += maxSets;
            }
        });
    });

    auto poolSizes =
        descriptorCounts |
        std::views::transform([](const auto &pair) { return vk::DescriptorPoolSize{pair.first, pair.second}; }) |
        std::ranges::to<std::vector>();

    if (poolSizes.empty())
    {
        poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, maxSets});
    }

    auto flags = vk::DescriptorPoolCreateFlags{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = flags;
    poolInfo.maxSets = maxSets;
    nrAssert(std::in_range<std::uint32_t>(poolSizes.size()),
             "Descriptor pool type count exceeds the Vulkan uint32 range.");
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
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

[[nodiscard]] ShaderBindingSet ShaderBindingPool::allocate(vk::DescriptorSetLayout descriptorSetLayout,
                                                           std::uint32_t setIndex,
                                                           std::optional<std::uint32_t> variableDescriptorCount)
{
    nrAssert(device_.has_value(), "ShaderBindingPool::allocate requires an owning device reference.");
    nrAssert(*pool_ != nullptr, "ShaderBindingPool::allocate requires a valid descriptor pool.");

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
    auto resolvedVariableDescriptorCount = std::optional<std::uint32_t>{};
    if (auto setIt = variableDescriptorCapBySetAndBinding_.find(setIndex);
        setIt != variableDescriptorCapBySetAndBinding_.end())
    {
        nrAssert(setIt->second.size() == 1u,
                 "ShaderBindingPool::allocate currently supports at most one variable descriptor-count binding per "
                 "set. set={}, count={}",
                 setIndex, setIt->second.size());
        auto const [bindingIndex, cap] = *setIt->second.begin();
        const auto requestedCount = variableDescriptorCount.value_or(cap);
        nrAssert(requestedCount > 0u && requestedCount <= cap,
                 "Variable descriptor count is outside the reflected layout capacity. set={}, requested={}, "
                 "capacity={}",
                 setIndex, requestedCount, cap);
        resolvedVariableDescriptorCount = requestedCount;
        variableCountInfo.descriptorSetCount = 1;
        variableCountInfo.pDescriptorCounts = std::addressof(*resolvedVariableDescriptorCount);
        allocateInfo.pNext = &variableCountInfo;
        set.allocatedDescriptorCountByBinding_.insert_or_assign(bindingIndex, *resolvedVariableDescriptorCount);
    }
    else
    {
        nrAssert(!variableDescriptorCount.has_value(), "Variable descriptor count supplied for non-variable set {}.",
                 setIndex);
    }

    auto allocatedSets = device_->get().allocateDescriptorSets(allocateInfo);
    nrAssert(allocatedSets.size() == 1u, "ShaderBindingPool::allocate expected exactly one descriptor set.");
    set.set_ = std::move(allocatedSets.front());
    set.descriptorPool_ = *pool_;
    return set;
}

void ShaderBindingPool::update(const ShaderBindingSet &set, std::span<const DescriptorWriteRequest> writeRequests)
{
    nrAssert(device_.has_value(), "ShaderBindingPool::update requires an owning device reference.");
    if (!set.valid() || writeRequests.empty())
    {
        return;
    }
    nrAssert(set.descriptorPool_ == *pool_,
             "ShaderBindingPool::update received a descriptor set allocated from another pool.");

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
        nrAssert(request.binding.set == set.setIndex(),
                 "Descriptor write set mismatch. request set={}, target set={}", request.binding.set, set.setIndex());

        auto expectedBindingIt = bindings_.find(std::tuple{request.binding.set, request.binding.binding});
        nrAssert(expectedBindingIt != bindings_.end(),
                 "Descriptor write targets an unknown pool binding. set={}, binding={}.", request.binding.set,
                 request.binding.binding);
        auto const &expectedBinding = expectedBindingIt->second;
        nrAssert(request.binding.descriptorType == expectedBinding.descriptorType &&
                     request.binding.descriptorCount == expectedBinding.descriptorCount &&
                     request.binding.isRuntimeSized == expectedBinding.isRuntimeSized &&
                     request.binding.bindingFlags == expectedBinding.bindingFlags &&
                     request.binding.usesImmutableSampler == expectedBinding.usesImmutableSampler,
                 "Descriptor write metadata does not match its pool layout. set={}, binding={}.", request.binding.set,
                 request.binding.binding);

        auto const descriptorCapacity = set.descriptorCapacity(expectedBinding);
        nrAssert(request.arrayElement < descriptorCapacity,
                 "Descriptor write array index out of range. set={}, binding={}, arrayElement={}, descriptorCount={}",
                 request.binding.set, request.binding.binding, request.arrayElement, descriptorCapacity);
        nrAssert(detail::descriptorPayloadMatchesType(request.payload, expectedBinding.descriptorType),
                 "Descriptor payload/type mismatch. set={}, binding={}, descriptorType={}", request.binding.set,
                 request.binding.binding, vk::to_string(request.binding.descriptorType));

        vk::WriteDescriptorSet write{};
        write.dstSet = set.raw();
        write.dstBinding = request.binding.binding;
        write.dstArrayElement = request.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = expectedBinding.descriptorType;

        std::visit(
            [&](const auto &payload) {
                using PayloadT = std::remove_cvref_t<decltype(payload)>;

                if constexpr (std::same_as<PayloadT, BufferDescriptorWrite>)
                {
                    nrAssert(payload.buffer != vk::Buffer{},
                             "Buffer descriptor write requires a non-null buffer handle.");
                    nrAssert(payload.range == vk::WholeSize || payload.range > 0u,
                             "Buffer descriptor write requires a non-zero range or vk::WholeSize.");
                    vk::DescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = payload.buffer;
                    bufferInfo.offset = payload.offset;
                    bufferInfo.range = payload.range;
                    bufferInfos.push_back(bufferInfo);
                    write.pBufferInfo = &bufferInfos.back();
                }
                else if constexpr (std::same_as<PayloadT, TexelBufferDescriptorWrite>)
                {
                    nrAssert(payload.view != vk::BufferView{},
                             "Texel-buffer descriptor write requires a non-null buffer view.");
                    texelBufferViews.push_back(payload.view);
                    write.pTexelBufferView = &texelBufferViews.back();
                }
                else if constexpr (std::same_as<PayloadT, ImageDescriptorWrite>)
                {
                    if (write.descriptorType == vk::DescriptorType::eSampler)
                    {
                        nrAssert(expectedBinding.usesImmutableSampler || payload.sampler != vk::Sampler{},
                                 "Non-immutable sampler descriptor write requires a non-null sampler.");
                    }
                    else
                    {
                        nrAssert(payload.imageView != vk::ImageView{},
                                 "Image descriptor write requires a non-null image view.");
                        nrAssert(payload.imageLayout != vk::ImageLayout::eUndefined,
                                 "Image descriptor write requires a defined image layout.");
                        if (write.descriptorType == vk::DescriptorType::eCombinedImageSampler)
                        {
                            nrAssert(expectedBinding.usesImmutableSampler || payload.sampler != vk::Sampler{},
                                     "Non-immutable combined-image descriptor write requires a non-null sampler.");
                        }
                    }
                    vk::DescriptorImageInfo imageInfo{};
                    imageInfo.sampler = payload.sampler;
                    imageInfo.imageView = payload.imageView;
                    imageInfo.imageLayout = payload.imageLayout;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                }
                else if constexpr (std::same_as<PayloadT, AccelerationStructureDescriptorWrite>)
                {
                    nrAssert(payload.accelerationStructure != vk::AccelerationStructureKHR{},
                             "Acceleration-structure descriptor write requires a non-null handle.");
                    accelerationHandles.push_back(payload.accelerationStructure);
                    vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{};
                    accelerationInfo.accelerationStructureCount = 1;
                    accelerationInfo.pAccelerationStructures = &accelerationHandles.back();
                    accelerationInfos.push_back(accelerationInfo);
                    write.pNext = &accelerationInfos.back();
                }
                else if constexpr (std::same_as<PayloadT, InlineUniformDescriptorWrite>)
                {
                    nrAssert(write.descriptorType == vk::DescriptorType::eInlineUniformBlock,
                             "Inline uniform payload requires eInlineUniformBlock descriptor type. set={}, binding={}, "
                             "type={}",
                             request.binding.set, request.binding.binding, vk::to_string(write.descriptorType));

                    nrAssert(std::in_range<std::uint32_t>(payload.data.size()),
                             "Inline uniform write size exceeds the Vulkan uint32 range.");
                    auto byteCount = static_cast<std::uint32_t>(payload.data.size());
                    nrAssert(detail::isInlineUniformByteCountValid(byteCount),
                             "Inline uniform write size must be > 0 and multiple of 4. set={}, binding={}, size={}",
                             request.binding.set, request.binding.binding, byteCount);
                    nrAssert((write.dstArrayElement % 4u) == 0u,
                             "Inline uniform dstArrayElement must be multiple of 4. set={}, binding={}, "
                             "dstArrayElement={}",
                             request.binding.set, request.binding.binding, write.dstArrayElement);
                    nrAssert(byteCount <= descriptorCapacity - write.dstArrayElement,
                             "Inline uniform write out of range. set={}, binding={}, dstArrayElement={}, "
                             "size={}, bindingByteCapacity={}",
                             request.binding.set, request.binding.binding, write.dstArrayElement, byteCount,
                             descriptorCapacity);

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

[[nodiscard]] std::vector<DescriptorWriteRequest> resolveDescriptorWriteRequests(
    const ShaderBindingSnapshot &snapshot, LogicalDescriptorResolver logicalResolver)
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

        nrAssert(resolved,
                 "resolveDescriptorWriteRequests failed to resolve descriptor record at set={}, binding={}, path='{}'.",
                 record.binding.set, record.binding.binding, record.binding.debugPath);

        if (!resolved)
        {
            return;
        }

        requests.push_back(DescriptorWriteRequest{
            .binding = record.binding,
            .arrayElement = record.arrayElement,
            .payload = std::move(resolvedPayload),
            .forceWrite = record.forceWrite,
        });
    });

    return requests;
}
} // namespace nr::rhi
