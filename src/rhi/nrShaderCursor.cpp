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
        std::ranges::for_each(descriptorWrites_, [](ShaderBindingRecord &record) {
            record.forceWrite = true;
        });
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
        [&] { return std::format("{} requires a valid ShaderCursor. cursor={}", operation, debugSummary()); });
}

void ShaderCursor::assertWritableCursor(std::string_view operation) const
{
    assertValidCursor(operation);
    nrAssert(
        !isRoot_,
        [&] { return std::format("{} requires a non-root ShaderCursor. cursor={}", operation, debugSummary()); });
    nrAssert(
        static_cast<bool>(bindingState_),
        [&] { return std::format("{} requires shared binding state. cursor={}", operation, debugSummary()); });
}

[[nodiscard]] ShaderCursor ShaderCursor::field(std::string_view fieldName) const
{
    assertValidCursor("ShaderCursor::field");
    nrAssert(
        !fieldName.empty(),
        [&] { return std::format("ShaderCursor::field requires a non-empty field name. cursor={}", debugSummary()); });

    if (isRoot_)
    {
        const auto &layout = layoutRef();
        auto rootField = layout.findRootField(fieldName);
        nrAssert(
            rootField.has_value(),
            [&] {
                return std::format(
                    "ShaderCursor::field failed to resolve root field '{}'. availableRootFields=[{}]. cursor={}",
                    fieldName,
                    describeRootFields(layout),
                    debugSummary());
            });
        return ShaderCursor(layout, std::move(*rootField), bindingState_);
    }

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer || kind == slang::TypeReflection::Kind::ParameterBlock)
    {
        auto *elementType = typeLayout_->getElementTypeLayout();
        nrAssert(
            elementType != nullptr,
            [&] {
                return std::format(
                    "ShaderCursor::field could not dereference constant-buffer/parameter-block cursor for field '{}'. cursor={}",
                    fieldName,
                    debugSummary());
            });

        ShaderCursor dereferenced = *this;
        dereferenced.typeLayout_ = elementType;
        return dereferenced.field(fieldName);
    }

    nrAssert(
        kind == slang::TypeReflection::Kind::Struct,
        [&] {
            return std::format(
                "ShaderCursor::field requires a root, struct, constant-buffer, or parameter-block cursor. requestedField='{}', cursor={}",
                fieldName,
                debugSummary());
        });

    auto fieldIndex = detail::sanitizeFieldIndex(typeLayout_->findFieldIndexByName(fieldName.data(), fieldName.data() + fieldName.size()));
    nrAssert(
        fieldIndex != std::numeric_limits<std::uint32_t>::max(),
        [&] {
            return std::format(
                "ShaderCursor::field could not find field '{}'. availableFields=[{}]. cursor={}",
                fieldName,
                describeStructFields(typeLayout_),
                debugSummary());
        });

    auto *fieldLayout = typeLayout_->getFieldByIndex(fieldIndex);
    nrAssert(
        fieldLayout != nullptr,
        [&] {
            return std::format(
                "ShaderCursor::field found field index {} for '{}', but Slang returned a null field layout. cursor={}",
                fieldIndex,
                fieldName,
                debugSummary());
        });

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
        [&] { return std::format("ShaderCursor::element cannot index the root cursor. index={}, cursor={}", index, debugSummary()); });

    auto kind = typeLayout_->getKind();
    if (kind == slang::TypeReflection::Kind::Struct)
    {
        auto structFieldCount = static_cast<std::uint32_t>(std::max<SlangInt>(0, typeLayout_->getFieldCount()));
        auto *fieldLayout = typeLayout_->getFieldByIndex(index);
        nrAssert(
            fieldLayout != nullptr,
            [&] {
                return std::format(
                    "ShaderCursor::element struct field index out of range. index={}, fieldCount={}, availableFields=[{}], cursor={}",
                    index,
                    structFieldCount,
                    describeStructFields(typeLayout_),
                    debugSummary());
            });

        return fieldCursorFromLayout(*fieldLayout, index, std::format("{}[{}]", debugPath_, index));
    }

    const auto &layout = layoutRef();
    if (kind == slang::TypeReflection::Kind::Resource)
    {
        auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
        nrAssert(
            bindingInfo.has_value(),
            [&] {
                return std::format(
                    "ShaderCursor::element requires a descriptor binding for resource indexing. index={}, cursor={}",
                    index,
                    debugSummary());
            });

        nrAssert(
            bindingInfo->isRuntimeSized || index < bindingInfo->descriptorCount,
            [&] {
                return std::format(
                    "ShaderCursor::element descriptor array index out of range. index={}, binding={}, cursor={}",
                    index,
                    describeDescriptorBinding(*bindingInfo),
                    debugSummary());
            });

        ShaderCursor next = *this;
        next.address_.bindingArrayIndex += index;
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    nrAssert(
        kind == slang::TypeReflection::Kind::Array || kind == slang::TypeReflection::Kind::Vector || kind == slang::TypeReflection::Kind::Matrix,
        [&] {
            return std::format(
                "ShaderCursor::element requires a struct, resource, array, vector, or matrix cursor. index={}, cursor={}",
                index,
                debugSummary());
        });

    auto *elementTypeLayout = typeLayout_->getElementTypeLayout();
    if (!elementTypeLayout)
    {
        auto bindingInfo = layout.findBindingByRangeIndex(address_.bindingRangeIndex);
        nrAssert(
            bindingInfo.has_value(),
            [&] {
                return std::format(
                    "ShaderCursor::element could not resolve an element type layout or descriptor binding. index={}, cursor={}",
                    index,
                    debugSummary());
            });

        auto elementCount = detail::tryElementCount(typeLayout_->getElementCount()).value_or(bindingInfo->descriptorCount);
        nrAssert(
            bindingInfo->isRuntimeSized || index < elementCount,
            [&] {
                return std::format(
                    "ShaderCursor::element descriptor-backed array index out of range. index={}, elementCount={}, binding={}, cursor={}",
                    index,
                    elementCount,
                    describeDescriptorBinding(*bindingInfo),
                    debugSummary());
            });

        ShaderCursor next = *this;
        next.address_.bindingArrayIndex = next.address_.bindingArrayIndex * std::max(elementCount, 1u) + index;
        next.debugPath_ = std::format("{}[{}]", debugPath_, index);
        return next;
    }

    auto elementCount = detail::tryElementCount(typeLayout_->getElementCount());
    nrAssert(
        !elementCount.has_value() || index < *elementCount,
        [&] {
            return std::format(
                "ShaderCursor::element array/vector/matrix index out of range. index={}, elementCount={}, cursor={}",
                index,
                elementCount.has_value() ? std::to_string(*elementCount) : std::string{"<runtime/unknown>"},
                debugSummary());
        });

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
        [&] { return std::format("ShaderCursor::getPath requires a non-empty path. cursor={}", debugSummary()); });

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
                [&] {
                    return std::format(
                        "ShaderCursor::getPath found an unterminated array index. path='{}', tokenBegin={}, cursor={}",
                        path,
                        tokenBegin,
                        cursor.debugSummary());
                });

            auto indexText = path.substr(tokenBegin + 1, tokenEnd - tokenBegin - 1);
            std::uint32_t index = 0;
            auto indexParse = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
            nrAssert(
                indexParse.ec == std::errc{} && indexParse.ptr == indexText.data() + indexText.size(),
                [&] {
                    return std::format(
                        "ShaderCursor::getPath failed to parse array index '{}'. path='{}', tokenBegin={}, cursor={}",
                        indexText,
                        path,
                        tokenBegin,
                        cursor.debugSummary());
                });

            cursor = cursor.element(index);
            tokenBegin = tokenEnd + 1;
            continue;
        }

        auto tokenEnd = path.find_first_of(".[", tokenBegin);
        auto token = path.substr(tokenBegin, tokenEnd == std::string_view::npos ? path.size() - tokenBegin : tokenEnd - tokenBegin);
        nrAssert(
            !token.empty(),
            [&] {
                return std::format(
                    "ShaderCursor::getPath found an empty field token. path='{}', tokenBegin={}, cursor={}",
                    path,
                    tokenBegin,
                    cursor.debugSummary());
            });
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
        [&] {
            return std::format(
                "ShaderCursor::writeDescriptorRecord requires a descriptor binding. allowedTypes=[{}], cursor={}",
                describeDescriptorTypes(allowedTypes),
                debugSummary());
        });
    nrAssert(
        acceptsDescriptorType(bindingInfo->descriptorType, allowedTypes),
        [&] {
            return std::format(
                "ShaderCursor::writeDescriptorRecord descriptor type mismatch. allowedTypes=[{}], actualBinding={}, cursor={}",
                describeDescriptorTypes(allowedTypes),
                describeDescriptorBinding(*bindingInfo),
                debugSummary());
        });

    auto arrayElement = explicitArrayElement.value_or(address_.bindingArrayIndex);
    nrAssert(
        bindingInfo->isRuntimeSized || arrayElement < bindingInfo->descriptorCount,
        [&] {
            return std::format(
                "ShaderCursor::writeDescriptorRecord descriptor array element out of range. arrayElement={}, binding={}, cursor={}",
                arrayElement,
                describeDescriptorBinding(*bindingInfo),
                debugSummary());
        });
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
        [&] { return std::format("ShaderCursor::setData requires a non-empty byte payload. cursor={}", debugSummary()); });
    nrAssert(
        bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
        [&] { return std::format("ShaderCursor::setData payload too large. size={}, cursor={}", bytes.size(), debugSummary()); });

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
            [&] { return std::format("Push-constant cursor offset exceeds uint32 range (offset={}).", address_.uniformOffset); });

        auto offset = static_cast<std::uint32_t>(address_.uniformOffset);
        auto rangeBegin = static_cast<std::uint64_t>(pushRange.offset);
        auto rangeEnd = rangeBegin + static_cast<std::uint64_t>(pushRange.size);
        auto writeBegin = static_cast<std::uint64_t>(offset);
        auto writeEnd = writeBegin + static_cast<std::uint64_t>(byteCount);

        nrAssert(
            writeBegin >= rangeBegin && writeEnd <= rangeEnd,
            [&] {
                return std::format(
                    "Push-constant write outside range. path={}, offset={}, size={}, rangeBegin={}, rangeEnd={}",
                    debugPath_,
                    offset,
                    byteCount,
                    pushRange.offset,
                    pushRange.offset + pushRange.size);
            });

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
        [&] {
            return std::format(
                "ShaderCursor::setData requires a push-constant range or inline-uniform descriptor binding. cursor={}",
                debugSummary());
        });
    nrAssert(
        bindingInfo->descriptorType == vk::DescriptorType::eInlineUniformBlock,
        [&] {
            return std::format(
                "ShaderCursor::setData can only write push constants or inline uniform blocks. actualBinding={}, cursor={}",
                describeDescriptorBinding(*bindingInfo),
                debugSummary());
        });

    nrAssert(
        detail::isInlineUniformByteCountValid(byteCount),
        [&] { return std::format("Inline uniform write size must be > 0 and multiple of 4 (size={}).", byteCount); });

    nrAssert(
        address_.uniformOffset <= std::numeric_limits<std::uint32_t>::max(),
        [&] { return std::format("Inline uniform offset exceeds uint32 range (offset={}).", address_.uniformOffset); });

    auto arrayElement = static_cast<std::uint32_t>(address_.uniformOffset);
    nrAssert(
        (arrayElement % 4u) == 0u,
        [&] {
            return std::format(
                "Inline uniform dstArrayElement must be multiple of 4. path={}, dstArrayElement={}",
                debugPath_,
                arrayElement);
        });

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
        [&] {
            return std::format(
                "ShaderCursor::setObject(Buffer) requires a valid Buffer. offset={}, range={}",
                offset,
                range == vk::WholeSize ? std::string{"vk::WholeSize"} : std::to_string(range));
        });
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
        [&] {
            return std::format(
                "ShaderCursor::setObject(Buffer, Format) requires a valid Buffer. format={}, offset={}, range={}, viewName='{}'",
                vk::to_string(format),
                offset,
                range == vk::WholeSize ? std::string{"vk::WholeSize"} : std::to_string(range),
                viewName);
        });
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
        [&] {
            return std::format(
                "ShaderCursor::setObject(Image) requires a valid Image. imageLayout={}",
                vk::to_string(imageLayout));
        });
    nrAssert(
        *image.view() != vk::ImageView{},
        [&] {
            return std::format(
                "ShaderCursor::setObject(Image) requires a valid default image view. imageLayout={}",
                vk::to_string(imageLayout));
        });
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
        [&] {
            return std::format(
                "ShaderCursor::setObject(Image, Sampler) requires a valid Image. imageLayout={}",
                vk::to_string(imageLayout));
        });
    nrAssert(
        *image.view() != vk::ImageView{},
        [&] {
            return std::format(
                "ShaderCursor::setObject(Image, Sampler) requires a valid default image view. imageLayout={}",
                vk::to_string(imageLayout));
        });
    nrAssert(
        sampler != vk::Sampler{},
        [&] {
            return std::format(
                "ShaderCursor::setObject(Image, Sampler) requires a non-null sampler. imageLayout={}",
                vk::to_string(imageLayout));
        });
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
} // namespace nr::rhi
