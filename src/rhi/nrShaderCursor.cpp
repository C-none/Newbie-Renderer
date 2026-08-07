module nr.rhi;
import :descriptor;
import dependency.slang;
import dependency.vulkan;
import :slang;
import :resource;
import std;

namespace nr::rhi
{
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

void ShaderBindingSnapshot::forceDescriptorWrites() noexcept
{
    std::ranges::for_each(descriptorWrites_, [](ShaderBindingRecord &record) { record.forceWrite = true; });
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
    if (auto *inlineWrite = std::get_if<InlineUniformDescriptorWrite>(&record.payload))
    {
        auto writeBegin = static_cast<std::uint64_t>(record.arrayElement);
        auto writeEnd = writeBegin + inlineWrite->data.size();
        std::ranges::for_each(descriptorWritesByBinding, [&](const auto &entry) {
            auto const &[set, binding, arrayElement] = entry.first;
            auto *existingInline = std::get_if<InlineUniformDescriptorWrite>(&entry.second.payload);
            if (set != record.binding.set || binding != record.binding.binding || !existingInline ||
                arrayElement == record.arrayElement)
            {
                return;
            }

            auto existingBegin = static_cast<std::uint64_t>(arrayElement);
            auto existingEnd = existingBegin + existingInline->data.size();
            nrAssert(writeEnd <= existingBegin || existingEnd <= writeBegin,
                     "Overlapping inline-uniform writes are ambiguous. set={}, binding={}, existing=[{}, {}), "
                     "incoming=[{}, {}).",
                     set, binding, existingBegin, existingEnd, writeBegin, writeEnd);
        });
    }
    descriptorWritesByBinding.insert_or_assign(key, std::move(record));
}

void ShaderCursor::SharedBindingState::writePushConstant(PushConstantWriteRecord record)
{
    auto key = std::tuple{record.range.bindingRangeIndex, record.offset};
    auto writeBegin = static_cast<std::uint64_t>(record.offset);
    auto writeEnd = writeBegin + record.data.size();
    std::ranges::for_each(pushConstantWritesByRangeAndOffset, [&](const auto &entry) {
        auto const &[bindingRangeIndex, offset] = entry.first;
        if (bindingRangeIndex != record.range.bindingRangeIndex || offset == record.offset)
        {
            return;
        }

        auto existingBegin = static_cast<std::uint64_t>(offset);
        auto existingEnd = existingBegin + entry.second.data.size();
        nrAssert(writeEnd <= existingBegin || existingEnd <= writeBegin,
                 "Overlapping push-constant writes are ambiguous. range={}, existing=[{}, {}), incoming=[{}, {}).",
                 bindingRangeIndex, existingBegin, existingEnd, writeBegin, writeEnd);
    });
    pushConstantWritesByRangeAndOffset.insert_or_assign(key, std::move(record));
}

[[nodiscard]] ShaderBindingSnapshot ShaderCursor::SharedBindingState::snapshot() const
{
    auto snapshot = ShaderBindingSnapshot{};
    snapshot.descriptorWrites_.reserve(descriptorWritesByBinding.size());
    snapshot.pushConstantWrites_.reserve(pushConstantWritesByRangeAndOffset.size());

    std::ranges::for_each(descriptorWritesByBinding,
                          [&](const auto &entry) { snapshot.descriptorWrites_.push_back(entry.second); });

    std::ranges::for_each(pushConstantWritesByRangeAndOffset,
                          [&](const auto &entry) { snapshot.pushConstantWrites_.push_back(entry.second); });

    return snapshot;
}

void ShaderCursor::SharedBindingState::clear()
{
    descriptorWritesByBinding.clear();
    pushConstantWritesByRangeAndOffset.clear();
}

ShaderCursor::ShaderCursor(const ShaderDescriptorLayout &layout, RootField field,
                           std::shared_ptr<SharedBindingState> bindingState)
    : layout_(std::cref(layout)), typeLayout_(field.typeLayout), address_(field.address), isRoot_(false),
      debugPath_(std::move(field.debugPath)), bindingState_(std::move(bindingState))
{
}

ShaderCursor::ShaderCursor(const ShaderDescriptorLayout &layout)
    : layout_(std::cref(layout)), typeLayout_(nullptr), address_({}), isRoot_(true), debugPath_("$root"),
      bindingState_(std::make_shared<SharedBindingState>())
{
}

[[nodiscard]] vk::DeviceSize ShaderCursor::normalizeBufferRange(const Buffer &buffer, vk::DeviceSize offset,
                                                                vk::DeviceSize range)
{
    nrAssert(offset < buffer.size(), "Buffer write offset out of range: offset={}, size={}", offset, buffer.size());
    if (range == vk::WholeSize)
    {
        return buffer.size() - offset;
    }
    nrAssert(range > 0u && range <= buffer.size() - offset,
             "Buffer write range out of bounds: offset={}, range={}, size={}", offset, range, buffer.size());
    return range;
}

[[nodiscard]] bool ShaderCursor::acceptsDescriptorType(vk::DescriptorType descriptorType,
                                                       std::initializer_list<vk::DescriptorType> allowed)
{
    return std::ranges::find(allowed, descriptorType) != allowed.end();
}

[[nodiscard]] const ShaderDescriptorLayout &ShaderCursor::layoutRef() const
{
    nrAssert(layout_.has_value(), "ShaderCursor requires a valid layout reference.");
    return layout_->get();
}

[[nodiscard]] std::string ShaderCursor::describeDescriptorBinding(const DescriptorBindingInfo &bindingInfo)
{
    return std::format("set={}, binding={}, type={}, count={}, runtime={}, range={}, path='{}'", bindingInfo.set,
                       bindingInfo.binding, vk::to_string(bindingInfo.descriptorType), bindingInfo.descriptorCount,
                       bindingInfo.isRuntimeSized, bindingInfo.bindingRangeIndex, bindingInfo.debugPath);
}

[[nodiscard]] std::string ShaderCursor::describeDescriptorTypes(
    std::initializer_list<vk::DescriptorType> descriptorTypes)
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
    return std::format("path='{}', valid={}, root={}, typeKind={}, offset={}, range={}, array={}",
                       debugPath_.empty() ? std::string{"<empty>"} : debugPath_, valid(), isRoot_,
                       typeLayout_ ? static_cast<int>(typeLayout_->getKind()) : -1, address_.uniformOffset,
                       address_.bindingRangeIndex, address_.bindingArrayIndex);
}

void ShaderCursor::assertValidCursor(std::string_view operation) const
{
    nrAssert(valid(), "{} requires a valid ShaderCursor. cursor={}", operation, debugSummary());
}

void ShaderCursor::assertWritableCursor(std::string_view operation) const
{
    assertValidCursor(operation);
    nrAssert(!isRoot_, "{} requires a non-root ShaderCursor. cursor={}", operation, debugSummary());
    nrAssert(static_cast<bool>(bindingState_), "{} requires shared binding state. cursor={}", operation,
             debugSummary());
}

[[nodiscard]] ShaderCursor ShaderCursor::field(std::string_view fieldName) const
{
    assertValidCursor("ShaderCursor::field");
    nrAssert(!fieldName.empty(), "ShaderCursor::field requires a non-empty field name. cursor={}", debugSummary());

    if (isRoot_)
    {
        const auto &layout = layoutRef();
        auto rootField = layout.findRootField(fieldName);
        nrAssert(rootField.has_value(),
                 "ShaderCursor::field failed to resolve root field '{}'. availableRootFields=[{}]. cursor={}",
                 fieldName, describeRootFields(layout), debugSummary());
        return ShaderCursor(layout, std::move(*rootField), bindingState_);
    }

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        auto *elementType = typeLayout_->getElementTypeLayout();
        nrAssert(elementType != nullptr,
                 "ShaderCursor::field could not dereference constant-buffer/parameter-block cursor for field '{}'. "
                 "cursor={}",
                 fieldName, debugSummary());

        ShaderCursor dereferenced = *this;
        dereferenced.typeLayout_ = elementType;
        return dereferenced.field(fieldName);
    }

    nrAssert(kind == slang::TypeReflection::Kind::Struct,
             "ShaderCursor::field requires a root, struct, constant-buffer, or parameter-block cursor. "
             "requestedField='{}', cursor={}",
             fieldName, debugSummary());

    auto fieldIndex = detail::sanitizeFieldIndex(
        typeLayout_->findFieldIndexByName(fieldName.data(), fieldName.data() + fieldName.size()));
    nrAssert(fieldIndex != std::numeric_limits<std::uint32_t>::max(),
             "ShaderCursor::field could not find field '{}'. availableFields=[{}]. cursor={}", fieldName,
             describeStructFields(typeLayout_), debugSummary());

    auto *fieldLayout = typeLayout_->getFieldByIndex(fieldIndex);
    nrAssert(fieldLayout != nullptr,
             "ShaderCursor::field found field index {} for '{}', but Slang returned a null field layout. cursor={}",
             fieldIndex, fieldName, debugSummary());

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
        auto *elementType = typeLayout_->getElementTypeLayout();
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
    nrAssert(!isRoot_, "ShaderCursor::element cannot index the root cursor. index={}, cursor={}", index,
             debugSummary());

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::Struct)
    {
        auto rawStructFieldCount = std::max<SlangInt>(0, typeLayout_->getFieldCount());
        nrAssert(std::in_range<std::uint32_t>(rawStructFieldCount),
                 "ShaderCursor::element struct field count exceeds uint32. fieldCount={}, cursor={}",
                 rawStructFieldCount, debugSummary());
        auto structFieldCount = static_cast<std::uint32_t>(rawStructFieldCount);
        nrAssert(index < structFieldCount,
                 "ShaderCursor::element struct field index out of range. index={}, fieldCount={}, "
                 "availableFields=[{}], cursor={}",
                 index, structFieldCount, describeStructFields(typeLayout_), debugSummary());
        auto *fieldLayout = typeLayout_->getFieldByIndex(index);
        nrAssert(fieldLayout != nullptr,
                 "ShaderCursor::element received a null Slang field layout. index={}, fieldCount={}, "
                 "availableFields=[{}], cursor={}",
                 index, structFieldCount, describeStructFields(typeLayout_), debugSummary());

        return fieldCursorFromLayout(*fieldLayout, index, std::format("{}[{}]", debugPath_, index));
    }

    const auto &layout = layoutRef();
    if (kind == slang::TypeReflection::Kind::Resource)
    {
        auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
        nrAssert(bindingInfo.has_value(),
                 "ShaderCursor::element requires a descriptor binding for resource indexing. index={}, cursor={}", index,
                 debugSummary());

        nrAssert(index < bindingInfo->descriptorCount,
                 "ShaderCursor::element descriptor array index out of range. index={}, binding={}, cursor={}", index,
                 describeDescriptorBinding(*bindingInfo), debugSummary());

        ShaderCursor next = *this;
        nrAssert(index <= std::numeric_limits<std::uint32_t>::max() - next.address_.bindingArrayIndex,
                 "ShaderCursor::element resource array index overflows uint32. index={}, cursor={}", index,
                 debugSummary());
        next.address_.bindingArrayIndex += index;
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    nrAssert(kind == slang::TypeReflection::Kind::Array || kind == slang::TypeReflection::Kind::Vector ||
                 kind == slang::TypeReflection::Kind::Matrix,
             "ShaderCursor::element requires a struct, resource, array, vector, or matrix cursor. index={}, cursor={}",
             index, debugSummary());

    auto *elementTypeLayout = typeLayout_->getElementTypeLayout();
    if (!elementTypeLayout)
    {
        auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
        nrAssert(bindingInfo.has_value(),
                 "ShaderCursor::element could not resolve an element type layout or descriptor binding. "
                 "index={}, cursor={}",
                 index, debugSummary());

        auto elementCount =
            detail::tryElementCount(typeLayout_->getElementCount()).value_or(bindingInfo->descriptorCount);
        nrAssert(index < elementCount && index < bindingInfo->descriptorCount,
                 "ShaderCursor::element descriptor-backed array index out of range. index={}, elementCount={}, "
                 "binding={}, cursor={}",
                 index, elementCount, describeDescriptorBinding(*bindingInfo), debugSummary());

        ShaderCursor next = *this;
        auto flattenedIndex = static_cast<std::uint64_t>(next.address_.bindingArrayIndex) *
                                  static_cast<std::uint64_t>(std::max(elementCount, 1u)) +
                              index;
        nrAssert(flattenedIndex <= std::numeric_limits<std::uint32_t>::max(),
                 "ShaderCursor::element descriptor array index overflows uint32. index={}, elementCount={}, "
                 "cursor={}",
                 index, elementCount, debugSummary());
        next.address_.bindingArrayIndex = static_cast<std::uint32_t>(flattenedIndex);
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    auto elementCount = detail::tryElementCount(typeLayout_->getElementCount());
    nrAssert(!elementCount.has_value() || index < *elementCount,
             "ShaderCursor::element array/vector/matrix index out of range. index={}, elementCount={}, cursor={}", index,
             elementCount.has_value() ? std::to_string(*elementCount) : std::string{"<runtime/unknown>"},
             debugSummary());

    ShaderCursor next = *this;
    auto elementStride = typeLayout_->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
    nrAssert(elementStride != std::numeric_limits<std::size_t>::max() &&
                 (index == 0u || elementStride <= std::numeric_limits<std::size_t>::max() / index),
             "ShaderCursor::element uniform stride multiplication overflows size_t. index={}, stride={}, cursor={}",
             index, elementStride, debugSummary());
    auto uniformOffsetDelta = static_cast<std::size_t>(index) * elementStride;
    nrAssert(uniformOffsetDelta <= std::numeric_limits<std::size_t>::max() - next.address_.uniformOffset,
             "ShaderCursor::element uniform offset overflows size_t. index={}, delta={}, cursor={}", index,
             uniformOffsetDelta, debugSummary());
    next.typeLayout_ = elementTypeLayout;
    next.address_.uniformOffset += uniformOffsetDelta;

    auto flattenedIndex = static_cast<std::uint64_t>(next.address_.bindingArrayIndex) *
                              detail::sanitizeElementCount(typeLayout_->getElementCount()) +
                          index;
    nrAssert(flattenedIndex <= std::numeric_limits<std::uint32_t>::max(),
             "ShaderCursor::element binding array index overflows uint32. index={}, cursor={}", index, debugSummary());
    next.address_.bindingArrayIndex = static_cast<std::uint32_t>(flattenedIndex);
    next.debugPath_ = std::format("{}[{}]", debugPath_, index);
    return next;
}

[[nodiscard]] ShaderCursor ShaderCursor::getPath(std::string_view path) const
{
    assertValidCursor("ShaderCursor::getPath");
    nrAssert(!path.empty(), "ShaderCursor::getPath requires a non-empty path. cursor={}", debugSummary());

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
            nrAssert(tokenEnd != std::string_view::npos,
                     "ShaderCursor::getPath found an unterminated array index. path='{}', tokenBegin={}, cursor={}",
                     path, tokenBegin, cursor.debugSummary());

            auto indexText = path.substr(tokenBegin + 1, tokenEnd - tokenBegin - 1);
            std::uint32_t index = 0;
            auto indexParse = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
            nrAssert(indexParse.ec == std::errc{} && indexParse.ptr == indexText.data() + indexText.size(),
                     "ShaderCursor::getPath failed to parse array index '{}'. path='{}', tokenBegin={}, cursor={}",
                     indexText, path, tokenBegin, cursor.debugSummary());

            cursor = cursor.element(index);
            tokenBegin = tokenEnd + 1;
            continue;
        }

        auto tokenEnd = path.find_first_of(".[", tokenBegin);
        auto token = path.substr(tokenBegin,
                                 tokenEnd == std::string_view::npos ? path.size() - tokenBegin : tokenEnd - tokenBegin);
        nrAssert(!token.empty(),
                 "ShaderCursor::getPath found an empty field token. path='{}', tokenBegin={}, cursor={}", path,
                 tokenBegin, cursor.debugSummary());
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

[[nodiscard]] ShaderBindingKind ShaderCursor::bindingKind() const
{
    if (descriptorBinding().has_value())
    {
        return ShaderBindingKind::Descriptor;
    }
    if (pushConstantRange().has_value())
    {
        return ShaderBindingKind::PushConstant;
    }
    return ShaderBindingKind::None;
}

[[nodiscard]] std::optional<ShaderDescriptorSemantic> ShaderCursor::descriptorSemantic() const
{
    auto bindingInfo = descriptorBinding();
    return bindingInfo.has_value() ? std::optional{bindingInfo->semantic()} : std::nullopt;
}

[[nodiscard]] std::optional<SlangImmutableSamplerBinding> ShaderCursor::makeImmutableSamplerBinding(
    SlangSamplerDesc samplerDesc) const
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

[[nodiscard]] bool ShaderCursor::writeDescriptorRecord(ShaderBindingRecordPayload payload,
                                                       std::initializer_list<vk::DescriptorType> allowedTypes,
                                                       std::optional<std::uint32_t> explicitArrayElement) const
{
    assertWritableCursor("ShaderCursor::writeDescriptorRecord");

    auto bindingInfo = layoutRef().findBindingByRangeIndex(address_.bindingRangeIndex);
    nrAssert(bindingInfo.has_value(),
             "ShaderCursor::writeDescriptorRecord requires a descriptor binding. allowedTypes=[{}], cursor={}",
             describeDescriptorTypes(allowedTypes), debugSummary());
    nrAssert(acceptsDescriptorType(bindingInfo->descriptorType, allowedTypes),
             "ShaderCursor::writeDescriptorRecord descriptor type mismatch. allowedTypes=[{}], actualBinding={}, "
             "cursor={}",
             describeDescriptorTypes(allowedTypes), describeDescriptorBinding(*bindingInfo), debugSummary());

    auto arrayElement = explicitArrayElement.value_or(address_.bindingArrayIndex);
    nrAssert(arrayElement < bindingInfo->descriptorCount,
             "ShaderCursor::writeDescriptorRecord descriptor array element out of range. arrayElement={}, binding={}, "
             "cursor={}",
             arrayElement, describeDescriptorBinding(*bindingInfo), debugSummary());
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
    nrAssert(!bytes.empty(), "ShaderCursor::setData requires a non-empty byte payload. cursor={}", debugSummary());
    nrAssert(bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
             "ShaderCursor::setData payload too large. size={}, cursor={}", bytes.size(), debugSummary());

    auto byteCount = static_cast<std::uint32_t>(bytes.size());
    auto *dataTypeLayout = typeLayout_;
    if (auto kind = dataTypeLayout->getKind();
        kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        if (auto *elementTypeLayout = dataTypeLayout->getElementTypeLayout())
        {
            dataTypeLayout = elementTypeLayout;
        }
    }
    auto reflectedByteSize = detail::tryLayoutSize(dataTypeLayout->getSize(slang::ParameterCategory::Uniform));
    nrAssert(reflectedByteSize.has_value() && *reflectedByteSize == bytes.size(),
             "ShaderCursor::setData requires an exact reflected field size. payloadSize={}, reflectedSize={}, "
             "cursor={}",
             bytes.size(),
             reflectedByteSize.has_value() ? std::to_string(*reflectedByteSize) : std::string{"<unknown>"},
             debugSummary());
    auto copiedBytes = std::vector<std::uint8_t>{};
    copiedBytes.assign(bytes.begin(), bytes.end());

    const auto &layout = layoutRef();
    auto pushRangeIt = layout.pushConstantByRangeIndex_.find(address_.bindingRangeIndex);
    if (pushRangeIt != layout.pushConstantByRangeIndex_.end())
    {
        const auto &pushRange = pushRangeIt->second;
        nrAssert(address_.uniformOffset <= std::numeric_limits<std::uint32_t>::max(),
                 "Push-constant cursor offset exceeds uint32 range (offset={}).", address_.uniformOffset);

        auto offset = static_cast<std::uint32_t>(address_.uniformOffset);
        auto rangeBegin = static_cast<std::uint64_t>(pushRange.offset);
        auto rangeEnd = rangeBegin + static_cast<std::uint64_t>(pushRange.size);
        auto writeBegin = static_cast<std::uint64_t>(offset);
        auto writeEnd = writeBegin + static_cast<std::uint64_t>(byteCount);

        nrAssert(writeBegin >= rangeBegin && writeEnd <= rangeEnd,
                 "Push-constant write outside range. path={}, offset={}, size={}, rangeBegin={}, rangeEnd={}",
                 debugPath_, offset, byteCount, pushRange.offset, pushRange.offset + pushRange.size);

        bindingState_->writePushConstant(PushConstantWriteRecord{
            .range = pushRange,
            .offset = offset,
            .data = std::move(copiedBytes),
        });
        return true;
    }

    auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
    nrAssert(bindingInfo.has_value(),
             "ShaderCursor::setData requires a push-constant range or inline-uniform descriptor binding. cursor={}",
             debugSummary());
    nrAssert(bindingInfo->descriptorType == vk::DescriptorType::eInlineUniformBlock,
             "ShaderCursor::setData can only write push constants or inline uniform blocks. actualBinding={}, "
             "cursor={}",
             describeDescriptorBinding(*bindingInfo), debugSummary());

    nrAssert(detail::isInlineUniformByteCountValid(byteCount),
             "Inline uniform write size must be > 0 and multiple of 4 (size={}).", byteCount);

    nrAssert(address_.uniformOffset <= std::numeric_limits<std::uint32_t>::max(),
             "Inline uniform offset exceeds uint32 range (offset={}).", address_.uniformOffset);

    auto arrayElement = static_cast<std::uint32_t>(address_.uniformOffset);
    nrAssert((arrayElement % 4u) == 0u,
             "Inline uniform dstArrayElement must be multiple of 4. path={}, dstArrayElement={}", debugPath_,
             arrayElement);

    return writeDescriptorRecord(InlineUniformDescriptorWrite{.data = std::move(copiedBytes)},
                                 {vk::DescriptorType::eInlineUniformBlock}, arrayElement);
}

[[nodiscard]] bool ShaderCursor::setObject(const Buffer &buffer, vk::DeviceSize offset, vk::DeviceSize range) const
{
    nrAssert(buffer.valid(), "ShaderCursor::setObject(Buffer) requires a valid Buffer. offset={}, range={}", offset,
             range == vk::WholeSize ? std::string{"vk::WholeSize"} : std::to_string(range));
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    return writeDescriptorRecord(
        BufferDescriptorWrite{.buffer = buffer.handle(), .offset = offset, .range = finalRange},
        {vk::DescriptorType::eUniformBuffer, vk::DescriptorType::eUniformBufferDynamic,
         vk::DescriptorType::eStorageBuffer, vk::DescriptorType::eStorageBufferDynamic});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::BufferView view) const
{
    nrAssert(view != vk::BufferView{}, "ShaderCursor::setObject(BufferView) requires a non-null buffer view.");
    return writeDescriptorRecord(TexelBufferDescriptorWrite{.view = view},
                                 {vk::DescriptorType::eUniformTexelBuffer, vk::DescriptorType::eStorageTexelBuffer});
}

[[nodiscard]] bool ShaderCursor::setObject(Buffer &buffer, vk::Format format, vk::DeviceSize offset,
                                           vk::DeviceSize range, std::string_view viewName) const
{
    nrAssert(buffer.valid(),
             "ShaderCursor::setObject(Buffer, Format) requires a valid Buffer. format={}, offset={}, range={}, "
             "viewName='{}'",
             vk::to_string(format), offset, range == vk::WholeSize ? std::string{"vk::WholeSize"} : std::to_string(range),
             viewName);
    auto finalRange = normalizeBufferRange(buffer, offset, range);
    auto view = buffer.addView(format, offset, finalRange, viewName);
    return setObject(*view.get());
}

[[nodiscard]] bool ShaderCursor::setObject(const Image &image, vk::ImageLayout imageLayout) const
{
    nrAssert(image.valid(), "ShaderCursor::setObject(Image) requires a valid Image. imageLayout={}",
             vk::to_string(imageLayout));
    nrAssert(*image.view() != vk::ImageView{},
             "ShaderCursor::setObject(Image) requires a valid default image view. imageLayout={}",
             vk::to_string(imageLayout));
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = {}},
        {vk::DescriptorType::eSampledImage, vk::DescriptorType::eInputAttachment, vk::DescriptorType::eStorageImage,
         vk::DescriptorType::eCombinedImageSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::Sampler sampler) const
{
    nrAssert(sampler != vk::Sampler{}, "ShaderCursor::setObject(Sampler) requires a non-null sampler.");
    return writeDescriptorRecord(ImageDescriptorWrite{.imageLayout = vk::ImageLayout::eUndefined, .sampler = sampler},
                                 {vk::DescriptorType::eSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(const Image &image, vk::Sampler sampler, vk::ImageLayout imageLayout) const
{
    nrAssert(image.valid(), "ShaderCursor::setObject(Image, Sampler) requires a valid Image. imageLayout={}",
             vk::to_string(imageLayout));
    nrAssert(*image.view() != vk::ImageView{},
             "ShaderCursor::setObject(Image, Sampler) requires a valid default image view. imageLayout={}",
             vk::to_string(imageLayout));
    nrAssert(sampler != vk::Sampler{},
             "ShaderCursor::setObject(Image, Sampler) requires a non-null sampler. imageLayout={}",
             vk::to_string(imageLayout));
    return writeDescriptorRecord(
        ImageDescriptorWrite{.imageView = *image.view(), .imageLayout = imageLayout, .sampler = sampler},
        {vk::DescriptorType::eCombinedImageSampler});
}

[[nodiscard]] bool ShaderCursor::setObject(vk::AccelerationStructureKHR accelerationStructure) const
{
    nrAssert(accelerationStructure != vk::AccelerationStructureKHR{},
             "ShaderCursor::setObject(AccelerationStructure) requires a non-null acceleration structure.");
    return writeDescriptorRecord(AccelerationStructureDescriptorWrite{.accelerationStructure = accelerationStructure},
                                 {vk::DescriptorType::eAccelerationStructureKHR});
}

[[nodiscard]] bool ShaderCursor::setObject(const LogicalResourceDescriptorWrite &logicalResource) const
{
    return writeDescriptorRecord(logicalResource, {
                                                      vk::DescriptorType::eUniformBuffer,
                                                      vk::DescriptorType::eUniformBufferDynamic,
                                                      vk::DescriptorType::eStorageBuffer,
                                                      vk::DescriptorType::eStorageBufferDynamic,
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

} // namespace nr::rhi
