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

[[nodiscard]] bool isInlineUniformByteCountValid(std::uint32_t byteCount)
{
    return byteCount > 0u && (byteCount % 4u) == 0u;
}

template <typename Derived, typename PayloadT>
struct DescriptorPayloadKeyPolicyBase
{
    [[nodiscard]] static DescriptorWritePayloadKey make(const PayloadT &payload)
    {
        return Derived::make(payload);
    }
};

struct BufferDescriptorPayloadKeyPolicy final : DescriptorPayloadKeyPolicyBase<BufferDescriptorPayloadKeyPolicy, BufferDescriptorWrite>
{
    [[nodiscard]] static DescriptorWritePayloadKey make(const BufferDescriptorWrite &payload)
    {
        return BufferDescriptorPayloadKey{
            .buffer = payload.buffer,
            .offset = payload.offset,
            .range = payload.range,
        };
    }
};

struct TexelBufferDescriptorPayloadKeyPolicy final : DescriptorPayloadKeyPolicyBase<TexelBufferDescriptorPayloadKeyPolicy, TexelBufferDescriptorWrite>
{
    [[nodiscard]] static DescriptorWritePayloadKey make(const TexelBufferDescriptorWrite &payload)
    {
        return TexelBufferDescriptorPayloadKey{
            .view = payload.view,
        };
    }
};

struct ImageDescriptorPayloadKeyPolicy final : DescriptorPayloadKeyPolicyBase<ImageDescriptorPayloadKeyPolicy, ImageDescriptorWrite>
{
    [[nodiscard]] static DescriptorWritePayloadKey make(const ImageDescriptorWrite &payload)
    {
        return ImageDescriptorPayloadKey{
            .imageView = payload.imageView,
            .imageLayout = payload.imageLayout,
            .sampler = payload.sampler,
        };
    }
};

struct AccelerationStructureDescriptorPayloadKeyPolicy final
    : DescriptorPayloadKeyPolicyBase<AccelerationStructureDescriptorPayloadKeyPolicy, AccelerationStructureDescriptorWrite>
{
    [[nodiscard]] static DescriptorWritePayloadKey make(const AccelerationStructureDescriptorWrite &payload)
    {
        return AccelerationStructureDescriptorPayloadKey{
            .accelerationStructure = payload.accelerationStructure,
        };
    }
};

struct InlineUniformDescriptorPayloadKeyPolicy final : DescriptorPayloadKeyPolicyBase<InlineUniformDescriptorPayloadKeyPolicy, InlineUniformDescriptorWrite>
{
    [[nodiscard]] static DescriptorWritePayloadKey make(const InlineUniformDescriptorWrite &payload)
    {
        return InlineUniformDescriptorPayloadKey{
            .data = payload.data,
        };
    }
};

template <typename PayloadT>
struct DescriptorPayloadKeyPolicy;

template <>
struct DescriptorPayloadKeyPolicy<BufferDescriptorWrite>
{
    using Type = BufferDescriptorPayloadKeyPolicy;
};

template <>
struct DescriptorPayloadKeyPolicy<TexelBufferDescriptorWrite>
{
    using Type = TexelBufferDescriptorPayloadKeyPolicy;
};

template <>
struct DescriptorPayloadKeyPolicy<ImageDescriptorWrite>
{
    using Type = ImageDescriptorPayloadKeyPolicy;
};

template <>
struct DescriptorPayloadKeyPolicy<AccelerationStructureDescriptorWrite>
{
    using Type = AccelerationStructureDescriptorPayloadKeyPolicy;
};

template <>
struct DescriptorPayloadKeyPolicy<InlineUniformDescriptorWrite>
{
    using Type = InlineUniformDescriptorPayloadKeyPolicy;
};

template <typename PayloadT>
[[nodiscard]] DescriptorWritePayloadKey makeDescriptorPayloadKey(const PayloadT &payload)
{
    return DescriptorPayloadKeyPolicy<PayloadT>::Type::make(payload);
}

[[nodiscard]] DescriptorWritePayloadKey makeDescriptorPayloadKey(const DescriptorWritePayload &payload)
{
    return std::visit(
        [](const auto &typedPayload) {
            return makeDescriptorPayloadKey(typedPayload);
        },
        payload);
}

[[nodiscard]] DescriptorWriteSlotKey makeDescriptorWriteSlotKey(const DescriptorWriteRequest &request) noexcept
{
    return DescriptorWriteSlotKey{
        .set = request.binding.set,
        .binding = request.binding.binding,
        .arrayElement = request.arrayElement,
        .descriptorType = request.binding.descriptorType,
    };
}

[[nodiscard]] std::uint32_t sanitizePushConstantSize(std::size_t byteSize)
{
    return sanitizeCountOrSize<std::size_t, 0u>(byteSize);
}

[[nodiscard]] std::uint32_t sanitizeDescriptorCount(SlangInt descriptorCount)
{
    return sanitizeCountOrSize<SlangInt, 1u>(descriptorCount, true);
}

[[nodiscard]] std::uint32_t sanitizeRangeOffset(SlangInt rangeOffset)
{
    return sanitizeCountOrSize<SlangInt, 0u>(rangeOffset, true);
}

[[nodiscard]] std::uint32_t sanitizeFieldIndex(SlangInt fieldIndex)
{
    if (fieldIndex < 0) return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(fieldIndex);
}

[[nodiscard]] std::uint32_t sanitizeElementCount(std::size_t elementCount)
{
    return sanitizeCountOrSize<std::size_t, 1u>(elementCount);
}

[[nodiscard]] std::optional<std::uint32_t> tryElementCount(std::size_t elementCount)
{
    if (elementCount == 0 || elementCount == std::numeric_limits<std::size_t>::max() || elementCount > std::numeric_limits<std::uint32_t>::max())
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
    ++version_;
}

[[nodiscard]] std::uint64_t DescriptorWriteCache::version() const noexcept
{
    return version_;
}

[[nodiscard]] std::vector<DescriptorWriteRequest> DescriptorWriteCache::filterChanged(std::span<const DescriptorWriteRequest> writeRequests)
{
    auto changedWrites = std::vector<DescriptorWriteRequest>{};
    changedWrites.reserve(writeRequests.size());

    std::ranges::for_each(writeRequests, [&](const DescriptorWriteRequest &request) {
        auto slotKey = detail::makeDescriptorWriteSlotKey(request);
        auto payloadKey = detail::makeDescriptorPayloadKey(request.payload);

        auto cachedPayload = payloadsBySlot_.find(slotKey);
        if (cachedPayload != payloadsBySlot_.end() && cachedPayload->second == payloadKey)
        {
            return;
        }

        payloadsBySlot_.insert_or_assign(std::move(slotKey), std::move(payloadKey));
        changedWrites.push_back(request);
    });

    if (!changedWrites.empty())
    {
        ++version_;
    }

    return changedWrites;
}

[[nodiscard]] std::vector<DescriptorWriteRequest> filterChangedDescriptorWrites(
    DescriptorWriteCache &cache,
    std::span<const DescriptorWriteRequest> writeRequests)
{
    return cache.filterChanged(writeRequests);
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

[[nodiscard]] bool usesDynamicDescriptorOffset(vk::DescriptorType descriptorType) noexcept
{
    return descriptorType == vk::DescriptorType::eUniformBufferDynamic ||
           descriptorType == vk::DescriptorType::eStorageBufferDynamic;
}

[[nodiscard]] std::optional<std::uint32_t> runtimeDescriptorArraySetFor(
    ShaderDescriptorSemantic semantic,
    const RuntimeDescriptorArraySetConvention &convention) noexcept
{
    switch (semantic)
    {
    case ShaderDescriptorSemantic::Sampler:
        return convention.samplerSet;
    case ShaderDescriptorSemantic::CombinedImageSampler:
    case ShaderDescriptorSemantic::SampledImage:
        return convention.sampledImageSet;
    case ShaderDescriptorSemantic::StorageImage:
        return convention.storageImageSet;
    case ShaderDescriptorSemantic::UniformTexelBuffer:
    case ShaderDescriptorSemantic::StorageTexelBuffer:
    case ShaderDescriptorSemantic::UniformBuffer:
    case ShaderDescriptorSemantic::StorageBuffer:
    case ShaderDescriptorSemantic::DynamicUniformBuffer:
    case ShaderDescriptorSemantic::DynamicStorageBuffer:
        return convention.bufferSet;
    case ShaderDescriptorSemantic::AccelerationStructure:
        return convention.accelerationStructureSet;
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
        return (bindingFlags & vk::DescriptorBindingFlagBits::eVariableDescriptorCount) == vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
    }

[[nodiscard]] bool DescriptorBindingInfo::isPartiallyBound() const noexcept
{
        return (bindingFlags & vk::DescriptorBindingFlagBits::ePartiallyBound) == vk::DescriptorBindingFlagBits::ePartiallyBound;
    }

[[nodiscard]] bool DescriptorBindingInfo::isUpdateAfterBind() const noexcept
{
        return (bindingFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) == vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    }

[[nodiscard]] ShaderDescriptorSemantic DescriptorBindingInfo::semantic() const noexcept
{
        return descriptorSemantic(descriptorType);
    }

[[nodiscard]] bool DescriptorBindingInfo::supportsImmutableSampler() const noexcept
{
        return nr::rhi::supportsImmutableSampler(descriptorType);
    }

[[nodiscard]] bool DescriptorBindingInfo::usesDynamicDescriptorOffset() const noexcept
{
        return nr::rhi::usesDynamicDescriptorOffset(descriptorType);
    }

[[nodiscard]] bool DescriptorBindingInfo::followsExpectedRuntimeSet() const noexcept
{
        return !expectedRuntimeSet.has_value() || set == *expectedRuntimeSet;
    }

[[nodiscard]] bool DescriptorBindingInfo::hasPhase(ShaderBindingPhase phase) const noexcept
{
        switch (phase)
        {
        case ShaderBindingPhase::Layout:
        case ShaderBindingPhase::DescriptorWrite:
        case ShaderBindingPhase::CommandRecord:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool PushConstantRangeInfo::hasPhase(ShaderBindingPhase phase) const noexcept
{
        switch (phase)
        {
        case ShaderBindingPhase::Layout:
        case ShaderBindingPhase::CommandRecord:
            return true;
        case ShaderBindingPhase::DescriptorWrite:
            return false;
        default:
            return false;
        }
    }

[[nodiscard]] bool ShaderBindingReflection::hasPhase(ShaderBindingPhase phase) const noexcept
{
        switch (kind)
        {
        case ShaderBindingKind::Descriptor:
            return descriptorBinding.has_value() && descriptorBinding->hasPhase(phase);
        case ShaderBindingKind::PushConstant:
            return pushConstantRange.has_value() && pushConstantRange->hasPhase(phase);
        case ShaderBindingKind::None:
            return false;
        default:
            return false;
        }
    }

[[nodiscard]] std::optional<ShaderDescriptorSemantic> ShaderBindingReflection::descriptorSemantic() const noexcept
{
        if (!descriptorBinding.has_value())
        {
            return std::nullopt;
        }
        return descriptorBinding->semantic();
    }

[[nodiscard]] bool ShaderBindingReflection::supportsImmutableSampler() const noexcept
{
        return descriptorBinding.has_value() && descriptorBinding->supportsImmutableSampler();
    }

[[nodiscard]] bool ShaderBindingReflection::usesDynamicDescriptorOffset() const noexcept
{
        return descriptorBinding.has_value() && descriptorBinding->usesDynamicDescriptorOffset();
    }

[[nodiscard]] bool ShaderBindingSet::valid() const noexcept
{ return static_cast<bool>(set_); }

[[nodiscard]] vk::DescriptorSet ShaderBindingSet::raw() const noexcept
{ return set_; }

[[nodiscard]] std::uint32_t ShaderBindingSet::setIndex() const noexcept
{ return setIndex_; }

[[nodiscard]] std::uint32_t ShaderBindingSet::descriptorCapacity(const DescriptorBindingInfo &bindingInfo) const noexcept
{
        auto it = allocatedDescriptorCountByBinding_.find(bindingInfo.binding);
        if (it != allocatedDescriptorCountByBinding_.end())
        {
            return it->second;
        }
        return bindingInfo.descriptorCount;
    }

[[nodiscard]] bool ShaderBindingSnapshot::empty() const noexcept
{
        return descriptorWrites_.empty() && pushConstantWrites_.empty();
    }

[[nodiscard]] std::size_t ShaderBindingSnapshot::descriptorWriteCount() const noexcept
{
        return descriptorWrites_.size();
    }

[[nodiscard]] std::size_t ShaderBindingSnapshot::pushConstantWriteCount() const noexcept
{
        return pushConstantWrites_.size();
    }

[[nodiscard]] std::span<const ShaderBindingRecord> ShaderBindingSnapshot::descriptorWrites() const noexcept
{
        return descriptorWrites_;
    }

[[nodiscard]] std::span<const PushConstantWriteRecord> ShaderBindingSnapshot::pushConstantWrites() const noexcept
{
        return pushConstantWrites_;
    }

[[nodiscard]] bool ShaderCursor::valid() const noexcept
{
        return layout_.has_value() && (isRoot_ || typeLayout_ != nullptr);
    }

[[nodiscard]] CursorAddress ShaderCursor::address() const noexcept
{
        return address_;
    }

[[nodiscard]] slang::TypeLayoutReflection *ShaderCursor::typeLayout() const noexcept
{
        return typeLayout_;
    }

void ShaderCursor::SharedBindingState::writeDescriptor(ShaderBindingRecord record)
{
            auto key = std::tuple{record.binding.set, record.binding.binding, record.arrayElement};
            descriptorWritesByBinding.insert_or_assign(key, std::move(record));
        }

void ShaderCursor::SharedBindingState::writePushConstant(PushConstantWriteRecord record)
{
            auto key = std::tuple{record.range.bindingRangeIndex, record.offset};
            pushConstantWritesByRangeAndOffset.insert_or_assign(key, std::move(record));
        }

[[nodiscard]] ShaderBindingSnapshot ShaderCursor::SharedBindingState::snapshot() const
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

void ShaderCursor::SharedBindingState::clear()
{
            descriptorWritesByBinding.clear();
            pushConstantWritesByRangeAndOffset.clear();
        }

ShaderCursor::ShaderCursor(const ShaderDescriptorLayout &layout, RootField field, std::shared_ptr<SharedBindingState> bindingState)
        : layout_(std::cref(layout)),
          typeLayout_(field.typeLayout),
          address_(field.address),
          isRoot_(false),
          debugPath_(std::move(field.debugPath)),
          bindingState_(std::move(bindingState))
{
    }

ShaderCursor::ShaderCursor(const ShaderDescriptorLayout &layout)
        : layout_(std::cref(layout)),
          typeLayout_(nullptr),
          address_({}),
          isRoot_(true),
          debugPath_("$root"),
          bindingState_(std::make_shared<SharedBindingState>())
{
    }

[[nodiscard]] vk::DeviceSize ShaderCursor::normalizeBufferRange(const Buffer &buffer, vk::DeviceSize offset, vk::DeviceSize range)
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

[[nodiscard]] bool ShaderCursor::acceptsDescriptorType(vk::DescriptorType descriptorType, std::initializer_list<vk::DescriptorType> allowed)
{
        return std::ranges::find(allowed, descriptorType) != allowed.end();
    }

[[nodiscard]] const ShaderDescriptorLayout &ShaderCursor::layoutRef() const
{
        nrAssert(layout_.has_value(), "ShaderCursor requires a valid layout reference.");
        return layout_->get();
    }

[[nodiscard]] ShaderDescriptorLayout ShaderDescriptorLayout::create(const SlangProgram &program, DescriptorBindingPolicy policy)
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

        std::uint32_t baseBindingRangeIndex = 0;

        auto collectFromTypeLayout = [&](slang::TypeLayoutReflection *typeLayout, std::optional<SlangStage> stage, std::string_view scopeName) {
            if (!typeLayout)
            {
                return;
            }

            auto stageFlags = detail::toVkShaderStageFlags(stage);
            auto bindingRangeCount = std::max<SlangInt>(0, typeLayout->getBindingRangeCount());
            auto fieldCount = std::max<SlangInt>(0, typeLayout->getFieldCount());
            auto fieldLayoutByBindingRangeOffset = std::map<std::uint32_t, std::reference_wrapper<slang::VariableLayoutReflection>>{};

            std::ranges::for_each(std::views::iota(SlangInt{0}, fieldCount), [&](SlangInt fieldIndex) {
                auto *fieldLayout = typeLayout->getFieldByIndex(static_cast<unsigned int>(fieldIndex));
                auto fieldBindingRangeOffset = typeLayout->getFieldBindingRangeOffset(fieldIndex);
                if (!fieldLayout || fieldBindingRangeOffset < 0 || fieldBindingRangeOffset >= bindingRangeCount)
                {
                    return;
                }
                fieldLayoutByBindingRangeOffset.try_emplace(static_cast<std::uint32_t>(fieldBindingRangeOffset), std::ref(*fieldLayout));
            });

            for (SlangInt rangeIndex = 0; rangeIndex < bindingRangeCount; ++rangeIndex)
            {
                auto *leafVariable = typeLayout->getBindingRangeLeafVariable(rangeIndex);
                auto bindingRangeDebugPath = leafVariable && leafVariable->getName()
                                                 ? std::format("{}::{}::bindingRange[{}]", scopeName, leafVariable->getName(), rangeIndex)
                                                 : std::format("{}::bindingRange[{}]", scopeName, rangeIndex);
                auto bindingType = typeLayout->getBindingRangeType(rangeIndex);
                auto bindingRangeIndex = baseBindingRangeIndex + static_cast<std::uint32_t>(rangeIndex);

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
                            "Invalid push constant size in '{}' (size={})",
                            bindingRangeDebugPath,
                            pushConstantBufferTypeLayout ? pushConstantBufferTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) : std::size_t(0)));
                    nrAssert(
                        pushConstantSize <= kMaxPushConstantBytes,
                        std::format(
                            "Push constant range in '{}' is {} bytes, but Newbie-Renderer allows at most {} bytes. "
                            "Move larger payload fields to frame uniforms or buffer/texture upload paths.",
                            bindingRangeDebugPath,
                            pushConstantSize,
                            kMaxPushConstantBytes));

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
                if (descriptorSetIndex < 0 || firstDescriptorRangeIndex < 0 || descriptorRangeCount <= 0)
                {
                    continue;
                }

                auto descriptorRangeIndex = firstDescriptorRangeIndex;
                auto setIndex = typeLayout->getDescriptorSetSpaceOffset(descriptorSetIndex);
                auto bindingIndex = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(descriptorSetIndex, descriptorRangeIndex);
                auto descriptorBindingType = typeLayout->getDescriptorSetDescriptorRangeType(descriptorSetIndex, descriptorRangeIndex);
                auto descriptorCategory = detail::bindingLayoutCategory(descriptorBindingType)
                                              .value_or(typeLayout->getDescriptorSetDescriptorRangeCategory(descriptorSetIndex, descriptorRangeIndex));
                if (auto fieldLayoutIt = fieldLayoutByBindingRangeOffset.find(static_cast<std::uint32_t>(rangeIndex));
                    fieldLayoutIt != fieldLayoutByBindingRangeOffset.end() && descriptorCategory != slang::ParameterCategory::None)
                {
                    auto &fieldLayout = fieldLayoutIt->second.get();
                    auto fieldSetIndex = detail::trySlangLayoutIndex(fieldLayout.getBindingSpace(descriptorCategory));
                    auto fieldBindingIndex = detail::trySlangLayoutIndex(fieldLayout.getOffset(descriptorCategory));
                    if (fieldSetIndex.has_value() && fieldBindingIndex.has_value())
                    {
                        if (setIndex != static_cast<SlangInt>(*fieldSetIndex))
                        {
                            // Slang descriptor-set ranges can report fallback set0/binding0 for globals
                            // from imported modules; the field layout preserves the source set.
                            setIndex = static_cast<SlangInt>(*fieldSetIndex);
                            bindingIndex = static_cast<SlangInt>(*fieldBindingIndex);
                        }
                    }
                }
                if (setIndex < 0 || bindingIndex < 0)
                {
                    continue;
                }

                auto descriptorType = detail::toVkDescriptorType(descriptorBindingType);
                nrAssert(
                    descriptorType.has_value(),
                    std::format(
                        "Unsupported Slang descriptor binding type {} in '{}'.",
                        static_cast<std::int32_t>(descriptorBindingType),
                        bindingRangeDebugPath));

                auto descriptorCountRaw = typeLayout->getDescriptorSetDescriptorRangeDescriptorCount(descriptorSetIndex, descriptorRangeIndex);

                DescriptorBindingInfo info{};
                info.set = static_cast<std::uint32_t>(setIndex);
                info.binding = static_cast<std::uint32_t>(bindingIndex);
                info.descriptorCount = detail::sanitizeDescriptorCount(descriptorCountRaw);
                info.isRuntimeSized = detail::isUnboundedDescriptorCount(descriptorCountRaw);
                info.descriptorType = *descriptorType;
                info.stageFlags = stageFlags;
                info.bindingRangeIndex = bindingRangeIndex;
                info.debugPath = bindingRangeDebugPath;
                if (info.isRuntimeSized && policy.runtimeArraySetPolicy == RuntimeDescriptorArraySetPolicy::RequireSemanticMultiSet)
                {
                    auto expectedSet = runtimeDescriptorArraySetFor(info.semantic(), policy.runtimeArraySetConvention);
                    nrAssert(
                        expectedSet.has_value(),
                        std::format(
                            "Runtime descriptor array '{}' has unsupported descriptor semantic {}.",
                            info.debugPath,
                            shaderDescriptorSemanticName(info.semantic())));
                    info.expectedRuntimeSet = expectedSet;
                    nrAssert(
                        info.followsExpectedRuntimeSet(),
                        std::format(
                            "Runtime descriptor array '{}' uses set {}, but the semantic multi-set ABI requires set {} for {} descriptors. "
                            "Update the shader [[vk::binding(binding, set)]] declaration instead of remapping it in RHI.",
                            info.debugPath,
                            info.set,
                            *expectedSet,
                            shaderDescriptorSemanticName(info.semantic())));
                }
                if (info.isRuntimeSized && policy.enableVariableDescriptorCount)
                {
                    info.descriptorCount = std::max(policy.defaultRuntimeDescriptorCount, 1u);
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

                auto key = std::tuple<std::uint32_t, std::uint32_t>{info.set, info.binding};
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
                            mergedIt->second.bindingFlags == info.bindingFlags &&
                            mergedIt->second.isRuntimeSized == info.isRuntimeSized,
                        std::format(
                            "Descriptor layout mismatch at set={}, binding={} when merging '{}' (type={}, count={}, runtime={}) with existing '{}' (type={}, count={}, runtime={}).",
                            info.set,
                            info.binding,
                            info.debugPath,
                            vk::to_string(info.descriptorType),
                            info.descriptorCount,
                            info.isRuntimeSized,
                            mergedIt->second.debugPath,
                            vk::to_string(mergedIt->second.descriptorType),
                            mergedIt->second.descriptorCount,
                            mergedIt->second.isRuntimeSized));
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
                    name,
                    ShaderCursor::RootField{
                        .typeLayout = fieldTypeLayout,
                        .address = CursorAddress{
                            .uniformOffset = fieldLayout->getOffset(),
                            .bindingRangeIndex = baseBindingRangeIndex + detail::sanitizeRangeOffset(typeLayout->getFieldBindingRangeOffset(fieldIndex)),
                        },
                        .debugPath = std::format("{}.{}", scopeName, name),
                    });

                nrAssert(
                    inserted.second,
                    std::format("Shader parameter name conflict detected for '{}'. Program-level and entrypoint-level resources must not share names.", name));
            }

            baseBindingRangeIndex += static_cast<std::uint32_t>(bindingRangeCount);
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

        std::ranges::for_each(layout.descriptorSets_, [](const DescriptorSetLayoutInfo &setInfo) {
            auto variableBindings = setInfo.bindings |
                                    std::views::filter([](const DescriptorBindingInfo &bindingInfo) {
                                        return bindingInfo.supportsVariableDescriptorCount();
                                    }) |
                                    std::ranges::to<std::vector>();
            nrAssert(
                variableBindings.size() <= 1u,
                std::format(
                    "ShaderDescriptorLayout::create supports at most one variable descriptor-count binding per set. set={}, count={}",
                    setInfo.set,
                    variableBindings.size()));
            if (variableBindings.empty())
            {
                return;
            }

            auto maxBinding = std::ranges::max(
                setInfo.bindings |
                std::views::transform([](const DescriptorBindingInfo &bindingInfo) { return bindingInfo.binding; }));
            nrAssert(
                variableBindings.front().binding == maxBinding,
                std::format(
                    "Variable descriptor-count binding must be the largest binding number in the set. set={}, binding={}, maxBinding={}",
                    setInfo.set,
                    variableBindings.front().binding,
                    maxBinding));
        });

        std::ranges::sort(layout.pushConstantRanges_, [](const PushConstantRangeInfo &lhs, const PushConstantRangeInfo &rhs) {
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

[[nodiscard]] std::vector<vk::DescriptorSetLayoutBinding> ShaderDescriptorLayout::makeVkSetLayoutBindings(std::uint32_t setIndex) const
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

[[nodiscard]] std::vector<vk::DescriptorBindingFlags> ShaderDescriptorLayout::makeVkSetLayoutBindingFlags(std::uint32_t setIndex) const
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

[[nodiscard]] bool ShaderDescriptorLayout::requiresUpdateAfterBindPool() const
{
        return std::ranges::any_of(descriptorSets_, [](const DescriptorSetLayoutInfo &setInfo) {
            return std::ranges::any_of(setInfo.bindings, [](const DescriptorBindingInfo &bindingInfo) {
                return (bindingInfo.bindingFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) == vk::DescriptorBindingFlagBits::eUpdateAfterBind;
            });
        });
    }

[[nodiscard]] std::span<const PushConstantRangeInfo> ShaderDescriptorLayout::pushConstantRanges() const noexcept
{
        return pushConstantRanges_;
    }

[[nodiscard]] std::optional<PushConstantRangeInfo> ShaderDescriptorLayout::pushConstantRange(const ShaderCursor &cursor) const
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

        std::ranges::sort(signature.descriptorBindings, [](const ShaderDescriptorAbiBinding &lhs, const ShaderDescriptorAbiBinding &rhs) {
            return lhs < rhs;
        });
        std::ranges::sort(signature.pushConstantRanges, [](const ShaderPushConstantAbiRange &lhs, const ShaderPushConstantAbiRange &rhs) {
            return lhs < rhs;
        });
        return signature;
    }

[[nodiscard]] std::vector<vk::PushConstantRange> ShaderDescriptorLayout::makeVkPushConstantRanges() const
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

[[nodiscard]] ShaderCursor ShaderDescriptorLayout::rootCursor() const
{
        nrAssert(valid(), "ShaderDescriptorLayout::rootCursor requires a valid descriptor layout.");
        return ShaderCursor(*this);
    }

[[nodiscard]] bool shaderLayoutAbiEquivalent(const ShaderLayoutAbiSignature &lhs, const ShaderLayoutAbiSignature &rhs) noexcept
{
        return lhs == rhs;
    }

namespace
{
[[nodiscard]] std::string describeAbiBinding(const nr::rhi::ShaderDescriptorAbiBinding &binding)
{
    return std::format(
        "set={}, binding={}, type={}, count={}, runtime={}, bindingFlags=0x{:x}, stageFlags=0x{:x}",
        binding.set,
        binding.binding,
        vk::to_string(binding.descriptorType),
        binding.descriptorCount,
        binding.isRuntimeSized,
        static_cast<std::uint32_t>(binding.bindingFlags),
        static_cast<std::uint32_t>(binding.stageFlags));
}

[[nodiscard]] std::string describeAbiPushConstantRange(const nr::rhi::ShaderPushConstantAbiRange &range)
{
    return std::format(
        "offset={}, size={}, stageFlags=0x{:x}",
        range.offset,
        range.size,
        static_cast<std::uint32_t>(range.stageFlags));
}
} // namespace

[[nodiscard]] std::string describeShaderLayoutAbiDifference(const ShaderLayoutAbiSignature &baseline, const ShaderLayoutAbiSignature &variant)
{
        if (baseline.descriptorBindings.size() != variant.descriptorBindings.size())
        {
            return std::format(
                "descriptor binding count differs: baseline={}, variant={}",
                baseline.descriptorBindings.size(),
                variant.descriptorBindings.size());
        }

        auto bindingMismatch = std::ranges::mismatch(baseline.descriptorBindings, variant.descriptorBindings);
        if (bindingMismatch.in1 != baseline.descriptorBindings.end() && bindingMismatch.in2 != variant.descriptorBindings.end())
        {
            return std::format(
                "descriptor binding differs: baseline=[{}], variant=[{}]",
                describeAbiBinding(*bindingMismatch.in1),
                describeAbiBinding(*bindingMismatch.in2));
        }

        if (baseline.pushConstantRanges.size() != variant.pushConstantRanges.size())
        {
            return std::format(
                "push constant range count differs: baseline={}, variant={}",
                baseline.pushConstantRanges.size(),
                variant.pushConstantRanges.size());
        }

        auto pushConstantMismatch = std::ranges::mismatch(baseline.pushConstantRanges, variant.pushConstantRanges);
        if (pushConstantMismatch.in1 != baseline.pushConstantRanges.end() && pushConstantMismatch.in2 != variant.pushConstantRanges.end())
        {
            return std::format(
                "push constant range differs: baseline=[{}], variant=[{}]",
                describeAbiPushConstantRange(*pushConstantMismatch.in1),
                describeAbiPushConstantRange(*pushConstantMismatch.in2));
        }

        return {};
    }

void assertShaderLayoutAbiStable(
    const SlangProgram &baselineProgram,
    const SlangProgram &variantProgram,
    DescriptorBindingPolicy policy,
    std::string_view debugName)
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
        auto message = std::format(
            "Shader variant changed descriptor/push-constant ABI. debugName='{}', difference='{}'",
            debugName,
            diff.empty() ? "unknown" : diff);
        nrLog(LogLevel::error, "RHI", message, std::source_location::current(), false);
        nrAssert(false, message);
    }

[[nodiscard]] std::optional<ShaderCursor::RootField> ShaderDescriptorLayout::findRootField(std::string_view fieldName) const
{
        auto it = rootFields_.find(std::string(fieldName));
        if (it == rootFields_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

[[nodiscard]] std::optional<DescriptorBindingInfo> ShaderDescriptorLayout::findBindingByRangeIndex(std::uint32_t bindingRangeIndex) const
{
        auto it = bindingByRangeIndex_.find(bindingRangeIndex);
        if (it == bindingByRangeIndex_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

[[nodiscard]] std::string ShaderCursor::describeDescriptorBinding(const DescriptorBindingInfo &bindingInfo)
{
    return std::format(
        "set={}, binding={}, type={}, count={}, runtime={}, range={}, path='{}'",
        bindingInfo.set,
        bindingInfo.binding,
        vk::to_string(bindingInfo.descriptorType),
        bindingInfo.descriptorCount,
        bindingInfo.isRuntimeSized,
        bindingInfo.bindingRangeIndex,
        bindingInfo.debugPath);
}

[[nodiscard]] std::string ShaderCursor::describeDescriptorTypes(std::initializer_list<vk::DescriptorType> descriptorTypes)
{
    auto result = std::string{};
    std::ranges::for_each(descriptorTypes, [&](vk::DescriptorType descriptorType) {
        if (!result.empty())
        {
            result += ", ";
        }
        result += vk::to_string(descriptorType);
    });
    return result.empty() ? std::string{"<none>"} : result;
}

[[nodiscard]] std::string ShaderCursor::describeRootFields(const ShaderDescriptorLayout &layout)
{
    auto result = std::string{};
    std::ranges::for_each(layout.rootFields_, [&](const auto &entry) {
        if (!result.empty())
        {
            result += ", ";
        }
        result += entry.first;
    });
    return result.empty() ? std::string{"<none>"} : result;
}

[[nodiscard]] std::string ShaderCursor::describeStructFields(slang::TypeLayoutReflection *typeLayout)
{
    if (!typeLayout)
    {
        return "<null type layout>";
    }

    auto result = std::string{};
    auto fieldCountValue = std::max<SlangInt>(0, typeLayout->getFieldCount());
    auto fieldIndices = std::views::iota(SlangInt{0}, fieldCountValue);
    std::ranges::for_each(fieldIndices, [&](SlangInt fieldIndex) {
        auto *fieldLayout = typeLayout->getFieldByIndex(static_cast<unsigned int>(fieldIndex));
        auto *fieldVariable = fieldLayout ? fieldLayout->getVariable() : nullptr;
        auto *fieldName = fieldVariable ? fieldVariable->getName() : nullptr;
        if (!fieldName)
        {
            return;
        }

        if (!result.empty())
        {
            result += ", ";
        }
        result += fieldName;
    });
    return result.empty() ? std::string{"<none>"} : result;
}

[[nodiscard]] std::string ShaderCursor::debugSummary() const
{
    return std::format(
        "path='{}', valid={}, root={}, typeKind={}, offset={}, range={}, array={}",
        debugPath_.empty() ? std::string{"<empty>"} : debugPath_,
        valid(),
        isRoot_,
        typeLayout_ ? static_cast<int>(typeLayout_->getKind()) : -1,
        address_.uniformOffset,
        address_.bindingRangeIndex,
        address_.bindingArrayIndex);
}

void ShaderCursor::assertValidCursor(std::string_view operation) const
{
    nrAssert(
        valid(),
        std::format("{} requires a valid ShaderCursor. cursor={}", operation, debugSummary()));
}

void ShaderCursor::assertWritableCursor(std::string_view operation) const
{
    assertValidCursor(operation);
    nrAssert(
        !isRoot_,
        std::format("{} requires a non-root ShaderCursor. cursor={}", operation, debugSummary()));
    nrAssert(
        static_cast<bool>(bindingState_),
        std::format("{} requires shared binding state. cursor={}", operation, debugSummary()));
}

[[nodiscard]] ShaderCursor ShaderCursor::field(std::string_view fieldName) const
{
    assertValidCursor("ShaderCursor::field");
    nrAssert(
        !fieldName.empty(),
        std::format("ShaderCursor::field requires a non-empty field name. cursor={}", debugSummary()));

    if (isRoot_)
    {
        const auto &layout = layoutRef();
        auto rootField = layout.findRootField(fieldName);
        nrAssert(
            rootField.has_value(),
            std::format(
                "ShaderCursor::field failed to resolve root field '{}'. availableRootFields=[{}]. cursor={}",
                fieldName,
                describeRootFields(layout),
                debugSummary()));
        return ShaderCursor(layout, std::move(*rootField), bindingState_);
    }

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        auto *elementType = typeLayout_->getElementTypeLayout();
        nrAssert(
            elementType != nullptr,
            std::format("ShaderCursor::field could not dereference constant-buffer/parameter-block cursor for field '{}'. cursor={}", fieldName, debugSummary()));

        ShaderCursor dereferenced = *this;
        dereferenced.typeLayout_ = elementType;
        return dereferenced.field(fieldName);
    }

    nrAssert(
        kind == slang::TypeReflection::Kind::Struct,
        std::format(
            "ShaderCursor::field requires a root, struct, constant-buffer, or parameter-block cursor. requestedField='{}', cursor={}",
            fieldName,
            debugSummary()));

    auto fieldIndex = detail::sanitizeFieldIndex(typeLayout_->findFieldIndexByName(fieldName.data(), fieldName.data() + fieldName.size()));
    nrAssert(
        fieldIndex != std::numeric_limits<std::uint32_t>::max(),
        std::format(
            "ShaderCursor::field could not find field '{}'. availableFields=[{}]. cursor={}",
            fieldName,
            describeStructFields(typeLayout_),
            debugSummary()));

    auto *fieldLayout = typeLayout_->getFieldByIndex(fieldIndex);
    nrAssert(
        fieldLayout != nullptr,
        std::format(
            "ShaderCursor::field found field index {} for '{}', but Slang returned a null field layout. cursor={}",
            fieldIndex,
            fieldName,
            debugSummary()));

    return fieldCursorFromLayout(*fieldLayout, fieldIndex, std::format("{}.{}", debugPath_, fieldName));
}

[[nodiscard]] bool ShaderCursor::hasField(std::string_view fieldName) const
{
    if (!valid() || fieldName.empty())
    {
        return false;
    }

    if (isRoot_)
    {
        return layoutRef().findRootField(fieldName).has_value();
    }

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        auto* elementType = typeLayout_->getElementTypeLayout();
        if (elementType == nullptr)
        {
            return false;
        }

        kind = elementType->getKind();
        if (kind != slang::TypeReflection::Kind::Struct)
        {
            return false;
        }

        auto fieldIndex = detail::sanitizeFieldIndex(
            elementType->findFieldIndexByName(fieldName.data(), fieldName.data() + fieldName.size()));
        return fieldIndex != std::numeric_limits<std::uint32_t>::max();
    }

    if (kind != slang::TypeReflection::Kind::Struct)
    {
        return false;
    }

    auto fieldIndex = detail::sanitizeFieldIndex(
        typeLayout_->findFieldIndexByName(fieldName.data(), fieldName.data() + fieldName.size()));
    return fieldIndex != std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] ShaderCursor ShaderCursor::element(std::uint32_t index) const
{
    assertValidCursor("ShaderCursor::element");
    nrAssert(
        !isRoot_,
        std::format("ShaderCursor::element cannot index the root cursor. index={}, cursor={}", index, debugSummary()));

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::Struct)
    {
        auto structFieldCount = static_cast<std::uint32_t>(std::max<SlangInt>(0, typeLayout_->getFieldCount()));
        auto *fieldLayout = typeLayout_->getFieldByIndex(index);
        nrAssert(
            fieldLayout != nullptr,
            std::format(
                "ShaderCursor::element struct field index out of range. index={}, fieldCount={}, availableFields=[{}], cursor={}",
                index,
                structFieldCount,
                describeStructFields(typeLayout_),
                debugSummary()));

        return fieldCursorFromLayout(*fieldLayout, index, std::format("{}[{}]", debugPath_, index));
    }

    const auto &layout = layoutRef();
    if (kind == slang::TypeReflection::Kind::Resource)
    {
        auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
        nrAssert(
            bindingInfo.has_value(),
            std::format("ShaderCursor::element requires a descriptor binding for resource indexing. index={}, cursor={}", index, debugSummary()));

        nrAssert(
            bindingInfo->isRuntimeSized || index < bindingInfo->descriptorCount,
            std::format(
                "ShaderCursor::element descriptor array index out of range. index={}, binding={}, cursor={}",
                index,
                describeDescriptorBinding(*bindingInfo),
                debugSummary()));

        ShaderCursor next = *this;
        next.address_.bindingArrayIndex += index;
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    nrAssert(
        kind == slang::TypeReflection::Kind::Array || kind == slang::TypeReflection::Kind::Vector || kind == slang::TypeReflection::Kind::Matrix,
        std::format("ShaderCursor::element requires a struct, resource, array, vector, or matrix cursor. index={}, cursor={}", index, debugSummary()));

    auto *elementTypeLayout = typeLayout_->getElementTypeLayout();
    if (!elementTypeLayout)
    {
        auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
        nrAssert(
            bindingInfo.has_value(),
            std::format(
                "ShaderCursor::element could not resolve an element type layout or descriptor binding. index={}, cursor={}",
                index,
                debugSummary()));

        auto elementCount = detail::tryElementCount(typeLayout_->getElementCount()).value_or(bindingInfo->descriptorCount);
        nrAssert(
            bindingInfo->isRuntimeSized || index < elementCount,
            std::format(
                "ShaderCursor::element descriptor-backed array index out of range. index={}, elementCount={}, binding={}, cursor={}",
                index,
                elementCount,
                describeDescriptorBinding(*bindingInfo),
                debugSummary()));

        ShaderCursor next = *this;
        next.address_.bindingArrayIndex = next.address_.bindingArrayIndex * std::max(elementCount, 1u) + index;
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    auto elementCount = detail::tryElementCount(typeLayout_->getElementCount());
    nrAssert(
        !elementCount.has_value() || index < *elementCount,
        std::format(
            "ShaderCursor::element array/vector/matrix index out of range. index={}, elementCount={}, cursor={}",
            index,
            elementCount.has_value() ? std::to_string(*elementCount) : std::string{"<runtime/unknown>"},
            debugSummary()));

    ShaderCursor next = *this;
    next.typeLayout_ = elementTypeLayout;
    next.address_.uniformOffset += static_cast<std::size_t>(index) * typeLayout_->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
    next.address_.bindingArrayIndex = next.address_.bindingArrayIndex * detail::sanitizeElementCount(typeLayout_->getElementCount()) + index;
    next.debugPath_ = std::format("{}[{}]", debugPath_, index);
    return next;
}

[[nodiscard]] ShaderCursor ShaderCursor::getPath(std::string_view path) const
{
    assertValidCursor("ShaderCursor::getPath");
    nrAssert(
        !path.empty(),
        std::format("ShaderCursor::getPath requires a non-empty path. cursor={}", debugSummary()));

    ShaderCursor cursor = *this;
    std::size_t tokenBegin = 0;

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
            nrAssert(
                tokenEnd != std::string_view::npos,
                std::format(
                    "ShaderCursor::getPath found an unterminated array index. path='{}', tokenBegin={}, cursor={}",
                    path,
                    tokenBegin,
                    cursor.debugSummary()));

            auto indexText = path.substr(tokenBegin + 1, tokenEnd - tokenBegin - 1);
            std::uint32_t index = 0;
            auto indexParse = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
            nrAssert(
                indexParse.ec == std::errc{} && indexParse.ptr == indexText.data() + indexText.size(),
                std::format(
                    "ShaderCursor::getPath failed to parse array index '{}'. path='{}', tokenBegin={}, cursor={}",
                    indexText,
                    path,
                    tokenBegin,
                    cursor.debugSummary()));

            cursor = cursor.element(index);
            tokenBegin = tokenEnd + 1;
            continue;
        }

        auto tokenEnd = path.find_first_of(".[", tokenBegin);
        auto token = path.substr(tokenBegin, tokenEnd == std::string_view::npos ? path.size() - tokenBegin : tokenEnd - tokenBegin);
        nrAssert(
            !token.empty(),
            std::format(
                "ShaderCursor::getPath found an empty field token. path='{}', tokenBegin={}, cursor={}",
                path,
                tokenBegin,
                cursor.debugSummary()));
        cursor = cursor.field(token);
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

[[nodiscard]] ShaderBindingReflection ShaderCursor::bindingReflection() const
{
    auto reflection = ShaderBindingReflection{};
    if (!valid() || isRoot_)
    {
        return reflection;
    }

    if (auto bindingInfo = descriptorBinding(); bindingInfo.has_value())
    {
        reflection.kind = ShaderBindingKind::Descriptor;
        reflection.descriptorBinding = std::move(bindingInfo);
        return reflection;
    }

    if (auto pushRange = pushConstantRange(); pushRange.has_value())
    {
        reflection.kind = ShaderBindingKind::PushConstant;
        reflection.pushConstantRange = std::move(pushRange);
        return reflection;
    }

    return reflection;
}

[[nodiscard]] ShaderBindingKind ShaderCursor::bindingKind() const
{
    return bindingReflection().kind;
}

[[nodiscard]] bool ShaderCursor::hasBindingPhase(ShaderBindingPhase phase) const
{
    return bindingReflection().hasPhase(phase);
}

[[nodiscard]] std::optional<ShaderDescriptorSemantic> ShaderCursor::descriptorSemantic() const
{
    return bindingReflection().descriptorSemantic();
}

[[nodiscard]] bool ShaderCursor::supportsImmutableSampler() const
{
    return bindingReflection().supportsImmutableSampler();
}

[[nodiscard]] bool ShaderCursor::usesDynamicDescriptorOffset() const
{
    return bindingReflection().usesDynamicDescriptorOffset();
}

[[nodiscard]] std::optional<SlangImmutableSamplerBinding> ShaderCursor::makeImmutableSamplerBinding(SlangSamplerDesc samplerDesc) const
{
    auto bindingInfo = descriptorBinding();
    if (!bindingInfo.has_value() || !bindingInfo->supportsImmutableSampler())
    {
        return std::nullopt;
    }

    if (address_.bindingArrayIndex != 0u)
    {
        return std::nullopt;
    }

    return SlangImmutableSamplerBinding{
        .set = bindingInfo->set,
        .binding = bindingInfo->binding,
        .descriptorCount = bindingInfo->descriptorCount,
        .samplerDesc = samplerDesc,
    };
}

[[nodiscard]] std::optional<std::uint32_t> ShaderCursor::bindingDescriptorCount() const
{
    auto bindingInfo = descriptorBinding();
    if (!bindingInfo.has_value())
    {
        return std::nullopt;
    }
    return bindingInfo->descriptorCount;
}

[[nodiscard]] bool ShaderCursor::referencesRuntimeDescriptorArray() const
{
    auto bindingInfo = descriptorBinding();
    return bindingInfo.has_value() && bindingInfo->isRuntimeSized;
}

[[nodiscard]] bool ShaderCursor::writeDescriptorRecord(
    ShaderBindingRecordPayload payload,
    std::initializer_list<vk::DescriptorType> allowedTypes,
    std::optional<std::uint32_t> explicitArrayElement) const
{
    assertWritableCursor("ShaderCursor::writeDescriptorRecord");

    auto bindingInfo = layoutRef().findBindingByRangeIndex(address_.bindingRangeIndex);
    nrAssert(
        bindingInfo.has_value(),
        std::format(
            "ShaderCursor::writeDescriptorRecord requires a descriptor binding. allowedTypes=[{}], cursor={}",
            describeDescriptorTypes(allowedTypes),
            debugSummary()));
    nrAssert(
        acceptsDescriptorType(bindingInfo->descriptorType, allowedTypes),
        std::format(
            "ShaderCursor::writeDescriptorRecord descriptor type mismatch. allowedTypes=[{}], actualBinding={}, cursor={}",
            describeDescriptorTypes(allowedTypes),
            describeDescriptorBinding(*bindingInfo),
            debugSummary()));

    auto arrayElement = explicitArrayElement.value_or(address_.bindingArrayIndex);
    nrAssert(
        bindingInfo->isRuntimeSized || arrayElement < bindingInfo->descriptorCount,
        std::format(
            "ShaderCursor::writeDescriptorRecord descriptor array element out of range. arrayElement={}, binding={}, cursor={}",
            arrayElement,
            describeDescriptorBinding(*bindingInfo),
            debugSummary()));
    bindingState_->writeDescriptor(ShaderBindingRecord{
        .binding = *bindingInfo,
        .arrayElement = arrayElement,
        .payload = std::move(payload),
    });
    return true;
}

[[nodiscard]] bool ShaderCursor::setData(std::span<const std::uint8_t> bytes) const
{
    assertWritableCursor("ShaderCursor::setData");
    nrAssert(
        !bytes.empty(),
        std::format("ShaderCursor::setData requires a non-empty byte payload. cursor={}", debugSummary()));
    nrAssert(
        bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
        std::format("ShaderCursor::setData payload too large. size={}, cursor={}", bytes.size(), debugSummary()));

    auto byteCount = static_cast<std::uint32_t>(bytes.size());
    auto copiedBytes = std::vector<std::uint8_t>{};
    copiedBytes.assign(bytes.begin(), bytes.end());

    const auto &layout = layoutRef();
    auto pushRangeIt = layout.pushConstantByRangeIndex_.find(address_.bindingRangeIndex);
    if (pushRangeIt != layout.pushConstantByRangeIndex_.end())
    {
        const auto &pushRange = pushRangeIt->second;
        nrAssert(
            address_.uniformOffset <= std::numeric_limits<std::uint32_t>::max(),
            std::format("Push-constant cursor offset exceeds uint32 range (offset={}).", address_.uniformOffset));

        auto offset = static_cast<std::uint32_t>(address_.uniformOffset);
        auto rangeBegin = static_cast<std::uint64_t>(pushRange.offset);
        auto rangeEnd = rangeBegin + static_cast<std::uint64_t>(pushRange.size);
        auto writeBegin = static_cast<std::uint64_t>(offset);
        auto writeEnd = writeBegin + static_cast<std::uint64_t>(byteCount);

        nrAssert(
            writeBegin >= rangeBegin && writeEnd <= rangeEnd,
            std::format(
                "Push-constant write outside range. path={}, offset={}, size={}, rangeBegin={}, rangeEnd={}",
                debugPath_,
                offset,
                byteCount,
                pushRange.offset,
                pushRange.offset + pushRange.size));

        bindingState_->writePushConstant(PushConstantWriteRecord{
            .range = pushRange,
            .offset = offset,
            .data = std::move(copiedBytes),
        });
        return true;
    }

    auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
    nrAssert(
        bindingInfo.has_value(),
        std::format("ShaderCursor::setData requires a push-constant range or inline-uniform descriptor binding. cursor={}", debugSummary()));
    nrAssert(
        bindingInfo->descriptorType == vk::DescriptorType::eInlineUniformBlock,
        std::format(
            "ShaderCursor::setData can only write push constants or inline uniform blocks. actualBinding={}, cursor={}",
            describeDescriptorBinding(*bindingInfo),
            debugSummary()));

    nrAssert(
        detail::isInlineUniformByteCountValid(byteCount),
        std::format("Inline uniform write size must be > 0 and multiple of 4 (size={}).", byteCount));

    nrAssert(
        address_.uniformOffset <= std::numeric_limits<std::uint32_t>::max(),
        std::format("Inline uniform offset exceeds uint32 range (offset={}).", address_.uniformOffset));

    auto arrayElement = static_cast<std::uint32_t>(address_.uniformOffset);
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
    nrAssert(
        buffer.valid(),
        std::format(
            "ShaderCursor::setObject(Buffer) requires a valid Buffer. offset={}, range={}",
            offset,
            range == vk::WholeSize ? std::string{"vk::WholeSize"} : std::to_string(range)));
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    return writeDescriptorRecord(
        BufferDescriptorWrite{.buffer = buffer.handle(), .offset = offset, .range = finalRange},
        {vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eUniformBufferDynamic, vk::DescriptorType::eStorageBuffer});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::BufferView view) const
{
    nrAssert(
        view != vk::BufferView{},
        "ShaderCursor::setObject(BufferView) requires a non-null buffer view.");
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
    nrAssert(
        buffer.valid(),
        std::format(
            "ShaderCursor::setObject(Buffer, Format) requires a valid Buffer. format={}, offset={}, range={}, viewName='{}'",
            vk::to_string(format),
            offset,
            range == vk::WholeSize ? std::string{"vk::WholeSize"} : std::to_string(range),
            viewName));
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    auto view = buffer.addView(format, offset, finalRange, viewName);
    return setObject(*view.get());
}

[[nodiscard]] bool ShaderCursor::setObject(
    const Image &image,
    vk::ImageLayout imageLayout) const
{
    nrAssert(
        image.valid(),
        std::format("ShaderCursor::setObject(Image) requires a valid Image. imageLayout={}", vk::to_string(imageLayout)));
    nrAssert(
        *image.view() != vk::ImageView{},
        std::format("ShaderCursor::setObject(Image) requires a valid default image view. imageLayout={}", vk::to_string(imageLayout)));
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = {}},
        {vk::DescriptorType::eSampledImage, vk::DescriptorType::eInputAttachment, vk::DescriptorType::eStorageImage, vk::DescriptorType::eCombinedImageSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::Sampler sampler) const
{
    nrAssert(
        sampler != vk::Sampler{},
        "ShaderCursor::setObject(Sampler) requires a non-null sampler.");
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageLayout = vk::ImageLayout::eUndefined, .sampler = sampler},
        {vk::DescriptorType::eSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(
    const Image &image,
    vk::Sampler sampler,
    vk::ImageLayout imageLayout) const
{
    nrAssert(
        image.valid(),
        std::format("ShaderCursor::setObject(Image, Sampler) requires a valid Image. imageLayout={}", vk::to_string(imageLayout)));
    nrAssert(
        *image.view() != vk::ImageView{},
        std::format("ShaderCursor::setObject(Image, Sampler) requires a valid default image view. imageLayout={}", vk::to_string(imageLayout)));
    nrAssert(
        sampler != vk::Sampler{},
        std::format("ShaderCursor::setObject(Image, Sampler) requires a non-null sampler. imageLayout={}", vk::to_string(imageLayout)));
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = sampler},
        {vk::DescriptorType::eCombinedImageSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::AccelerationStructureKHR accelerationStructure) const
{
    nrAssert(
        accelerationStructure != vk::AccelerationStructureKHR{},
        "ShaderCursor::setObject(AccelerationStructure) requires a non-null acceleration structure.");
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

[[nodiscard]] std::uint32_t ShaderCursor::fieldCount() const noexcept
{
    if (!valid() || isRoot_)
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(std::max<SlangInt>(0, typeLayout_->getFieldCount()));
}

[[nodiscard]] std::optional<std::uint32_t> ShaderCursor::elementCount() const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }

    auto kindValue = typeLayout_->getKind();
    if (kindValue == slang::TypeReflection::Kind::Resource)
    {
        auto bindingInfo = descriptorBinding();
        if (!bindingInfo.has_value())
        {
            return std::nullopt;
        }
        return bindingInfo->descriptorCount;
    }

    if (kindValue != slang::TypeReflection::Kind::Array && kindValue != slang::TypeReflection::Kind::Vector && kindValue != slang::TypeReflection::Kind::Matrix)
    {
        return std::nullopt;
    }

    auto elementCount = detail::tryElementCount(typeLayout_->getElementCount());
    if (elementCount.has_value())
    {
        return elementCount;
    }

    auto bindingInfo = descriptorBinding();
    if (!bindingInfo.has_value())
    {
        return std::nullopt;
    }
    return bindingInfo->descriptorCount;
}

[[nodiscard]] std::optional<std::size_t> ShaderCursor::size(slang::ParameterCategory category) const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return detail::tryLayoutSize(typeLayout_->getSize(category));
}

[[nodiscard]] std::optional<std::size_t> ShaderCursor::stride(slang::ParameterCategory category) const
{
    if (!valid() || isRoot_)
    {
        return std::nullopt;
    }
    return detail::tryLayoutSize(typeLayout_->getStride(category));
}

[[nodiscard]] std::optional<std::int32_t> ShaderCursor::alignment(slang::ParameterCategory category) const
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

[[nodiscard]] std::optional<std::uint32_t> ShaderCursor::resourceResultElementCount() const
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

    auto descriptorCounts = std::map<vk::DescriptorType, std::uint32_t>{};
    std::uint32_t inlineUniformBindingCount = 0;
    std::ranges::for_each(descriptorLayout.descriptorSets(), [&](const DescriptorSetLayoutInfo &setInfo) {
        std::ranges::for_each(setInfo.bindings, [&](const DescriptorBindingInfo &bindingInfo) {
            const bool isVariableCount = bindingInfo.supportsVariableDescriptorCount();
            const std::uint32_t effectiveDescriptorCount = isVariableCount ? std::max(config.defaultVariableDescriptorCount, 1u) : bindingInfo.descriptorCount;
            descriptorCounts[bindingInfo.descriptorType] += effectiveDescriptorCount * maxSets;
            if (isVariableCount)
            {
                auto &bindingCaps = pool.variableDescriptorCapBySetAndBinding_[setInfo.set];
                auto &cap = bindingCaps[bindingInfo.binding];
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

[[nodiscard]] ShaderBindingSet ShaderBindingPool::allocate(vk::DescriptorSetLayout descriptorSetLayout, std::uint32_t setIndex, std::optional<std::uint32_t> variableDescriptorCount) const
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
    if (auto setIt = variableDescriptorCapBySetAndBinding_.find(setIndex); setIt != variableDescriptorCapBySetAndBinding_.end())
    {
        nrAssert(
            setIt->second.size() == 1u,
            std::format(
                "ShaderBindingPool::allocate currently supports at most one variable descriptor-count binding per set. set={}, count={}",
                setIndex,
                setIt->second.size()));
        auto const [bindingIndex, cap] = *setIt->second.begin();
        const auto requestedCount = variableDescriptorCount.value_or(cap);
        const auto resolvedCount = std::clamp(requestedCount, 1u, cap);
        variableCountInfo.descriptorSetCount = 1;
        variableCountInfo.pDescriptorCounts = &resolvedCount;
        allocateInfo.pNext = &variableCountInfo;
        set.allocatedDescriptorCountByBinding_.insert_or_assign(bindingIndex, resolvedCount);
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

        auto const descriptorCapacity = set.descriptorCapacity(request.binding);
        nrAssert(
            request.arrayElement < descriptorCapacity,
            std::format(
                "Descriptor write array index out of range. set={}, binding={}, arrayElement={}, descriptorCount={}",
                request.binding.set,
                request.binding.binding,
                request.arrayElement,
                descriptorCapacity));

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

                    auto byteCount = static_cast<std::uint32_t>(payload.data.size());
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
                        write.dstArrayElement + byteCount <= descriptorCapacity,
                        std::format(
                            "Inline uniform write out of range. set={}, binding={}, dstArrayElement={}, size={}, bindingByteCapacity={}",
                            request.binding.set,
                            request.binding.binding,
                            write.dstArrayElement,
                            byteCount,
                            descriptorCapacity));

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
