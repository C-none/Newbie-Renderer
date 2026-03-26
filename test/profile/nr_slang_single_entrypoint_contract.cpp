import std;
import nr.rhi;

namespace
{
[[nodiscard]] nr::rhi::SlangProgram compileProgram(std::string_view sourcePath)
{
    nr::rhi::SlangProgramCompileFileRequest request{
        .sourcePath = std::filesystem::path(sourcePath),
    };
    return nr::rhi::ShaderService::instance().compileProgramByFile(request);
}

template <typename TTypeLayout>
void printBindingRanges(std::string_view label, TTypeLayout *typeLayout)
{
    if (!typeLayout)
    {
        std::println("[warn] {} type layout is null.", label);
        return;
    }

    auto rangeCount = typeLayout->getBindingRangeCount();
    if (rangeCount < 0)
    {
        rangeCount = 0;
    }
    std::println("[info] {} bindingRangeCount={}", label, rangeCount);
    for (int i = 0; i < rangeCount; ++i)
    {
        std::println(
            "  {} bindingRange[{}]: set={}, binding={}, count={}, type={}",
            label,
            i,
            typeLayout->getBindingRangeDescriptorSetIndex(i),
            typeLayout->getBindingRangeFirstDescriptorRangeIndex(i),
            typeLayout->getBindingRangeBindingCount(i),
            static_cast<int>(typeLayout->getBindingRangeType(i)));
    }
}

void printDescriptorLayout(const nr::rhi::ShaderDescriptorLayout &layout)
{
    std::println("[info] merged descriptor sets={}", layout.descriptorSets().size());
    for (auto const &setInfo : layout.descriptorSets())
    {
        std::println("  set {} has {} bindings", setInfo.set, setInfo.bindings.size());
        for (auto const &binding : setInfo.bindings)
        {
            std::println(
                "    binding {}: type={}, count={}, stageFlags=0x{:x}, rangeIndex={}, path={}",
                binding.binding,
                vk::to_string(binding.descriptorType),
                binding.descriptorCount,
                static_cast<uint32_t>(binding.stageFlags),
                binding.bindingRangeIndex,
                binding.debugPath);
        }
    }

    std::println("[info] push-constant ranges={}", layout.pushConstantRanges().size());
    for (auto const &range : layout.pushConstantRanges())
    {
        std::println(
            "  pushConstant: offset={}, size={}, stageFlags=0x{:x}, rangeIndex={}, path={}",
            range.offset,
            range.size,
            static_cast<uint32_t>(range.stageFlags),
            range.bindingRangeIndex,
            range.debugPath);
    }
}

[[nodiscard]] bool hasBindingType(slang::TypeLayoutReflection *typeLayout, slang::BindingType expected)
{
    if (!typeLayout)
    {
        return false;
    }

    auto bindingRangeCount = std::max<SlangInt>(0, typeLayout->getBindingRangeCount());
    for (SlangInt rangeIndex = 0; rangeIndex < bindingRangeCount; ++rangeIndex)
    {
        if (typeLayout->getBindingRangeType(rangeIndex) == expected)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] int checkFieldBinding(
    const nr::rhi::ShaderCursor &root,
    std::string_view fieldName,
    vk::DescriptorType expectedType)
{
    auto field = root[fieldName];
    if (!field.valid())
    {
        std::println("[error] cursor field '{}' is invalid.", fieldName);
        return 1;
    }

    auto binding = field.descriptorBinding();
    if (!binding.has_value())
    {
        std::println("[error] field '{}' has no descriptor binding.", fieldName);
        return 2;
    }

    if (binding->descriptorType != expectedType)
    {
        std::println(
            "[error] field '{}' maps to {}, expected {}.",
            fieldName,
            vk::to_string(binding->descriptorType),
            vk::to_string(expectedType));
        return 3;
    }

    std::println(
        "[ok] field '{}' => {}, set={}, binding={}, rangeIndex={}",
        fieldName,
        vk::to_string(binding->descriptorType),
        binding->set,
        binding->binding,
        binding->bindingRangeIndex);
    return 0;
}

[[nodiscard]] int runBindingReflectionChecks()
{
    auto program = compileProgram("test/main/resourceBindingReflection");
    if (!program.valid())
    {
        std::println("[error] failed to compile resourceBindingReflection shader.");
        return 101;
    }

    auto *globalScopeVarLayout = program.programLayout() ? program.programLayout()->getGlobalParamsVarLayout() : nullptr;
    auto *globalTypeLayout = globalScopeVarLayout ? globalScopeVarLayout->getTypeLayout() : nullptr;
    if (!globalTypeLayout)
    {
            std::println("[error] missing global params var/type layout.");
        return 102;
    }

    auto requiredBindingTypes = std::array{
        slang::BindingType::Sampler,
        slang::BindingType::Texture,
        slang::BindingType::MutableTexture,
        slang::BindingType::ConstantBuffer,
        slang::BindingType::TypedBuffer,
        slang::BindingType::MutableTypedBuffer,
        slang::BindingType::RawBuffer,
        slang::BindingType::MutableRawBuffer,
        slang::BindingType::PushConstant,
    };

    for (auto bindingType : requiredBindingTypes)
    {
        if (!hasBindingType(globalTypeLayout, bindingType))
        {
            std::println("[error] missing reflected Slang binding type {} in global layout.", static_cast<int32_t>(bindingType));
            return 103;
        }
    }

    auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    if (!layout.valid())
    {
        std::println("[error] descriptor layout creation failed for binding reflection test.");
        return 104;
    }

    auto root = layout.rootCursor();
    if (!root.valid())
    {
        std::println("[error] root cursor invalid for binding reflection test.");
        return 105;
    }

    auto mappingChecks = std::array{
        std::pair{"linearSampler", vk::DescriptorType::eSampler},
        std::pair{"tex2d", vk::DescriptorType::eSampledImage},
        std::pair{"rwTex2d", vk::DescriptorType::eStorageImage},
        std::pair{"cbData", vk::DescriptorType::eUniformBuffer},
        std::pair{"typedBuffer", vk::DescriptorType::eUniformTexelBuffer},
        std::pair{"rwTypedBuffer", vk::DescriptorType::eStorageTexelBuffer},
        std::pair{"rawBuffer", vk::DescriptorType::eStorageBuffer},
        std::pair{"rwRawBuffer", vk::DescriptorType::eStorageBuffer},
    };

    for (auto const &[fieldName, expectedType] : mappingChecks)
    {
        auto result = checkFieldBinding(root, fieldName, expectedType);
        if (result != 0)
        {
            return 110 + result;
        }
    }

    auto typedBufferCursor = root["typedBuffer"];
    if (!typedBufferCursor.valid())
    {
        std::println("[error] typedBuffer cursor is invalid for type-layout introspection.");
        return 123;
    }

    if (typedBufferCursor.kind() != slang::TypeReflection::Kind::Resource)
    {
        std::println("[error] typedBuffer kind mismatch: expected Resource, actual={}", static_cast<int32_t>(typedBufferCursor.kind()));
        return 124;
    }

    auto *resourceResultType = typedBufferCursor.resourceResultType();
    if (!resourceResultType || resourceResultType->getKind() != slang::TypeReflection::Kind::Vector)
    {
        std::println("[error] typedBuffer resource result type is not Vector.");
        return 125;
    }

    auto resultElementCount = typedBufferCursor.resourceResultElementCount();
    if (!resultElementCount.has_value() || *resultElementCount != 4u)
    {
        std::println("[error] typedBuffer vector element count mismatch: expected 4, actual={}", resultElementCount.value_or(0u));
        return 126;
    }

    auto typedBufferShape = typedBufferCursor.resourceShape();
    auto typedBufferAccess = typedBufferCursor.resourceAccess();
    if (!typedBufferShape.has_value() || !typedBufferAccess.has_value())
    {
        std::println("[error] typedBuffer resource shape/access metadata is unavailable from cursor.");
        return 127;
    }

    if (layout.pushConstantRanges().empty())
    {
        std::println("[error] push constant range was not reflected in binding reflection test.");
        return 120;
    }

    auto pushField = root["pushData"];
    if (!pushField.valid())
    {
        std::println("[error] pushData cursor is invalid.");
        return 121;
    }

    if (pushField.descriptorBinding().has_value())
    {
        std::println("[error] pushData should not map to descriptor set binding.");
        return 122;
    }

    if (layout.pushConstantRange(root).has_value())
    {
        std::println("[error] root cursor should not resolve to a push-constant range.");
        return 128;
    }

    auto pushDataRange = layout.pushConstantRange(pushField);
    if (!pushDataRange.has_value())
    {
        std::println("[error] pushData cursor failed to resolve push-constant range from descriptor layout.");
        return 129;
    }

    auto pushBias = root.getPath("pushData.bias");
    auto pushScale = root.getPath("pushData.scale");
    if (!pushBias.valid() || !pushScale.valid())
    {
        std::println("[error] pushData field cursors are invalid.");
        return 130;
    }

    auto pushBiasRange = pushBias.pushConstantRange();
    auto pushScaleRange = layout.pushConstantRange(pushScale);
    if (!pushBiasRange.has_value() || !pushScaleRange.has_value())
    {
        std::println("[error] pushData field cursor failed to resolve push-constant range.");
        return 131;
    }

    if (pushBiasRange->bindingRangeIndex != pushScaleRange->bindingRangeIndex)
    {
        std::println("[error] pushData fields resolved to inconsistent push-constant binding ranges.");
        return 132;
    }

    if (pushScale.address().uniformOffset <= pushBias.address().uniformOffset)
    {
        std::println(
            "[error] pushData field offsets are invalid: biasOffset={}, scaleOffset={}",
            pushBias.address().uniformOffset,
            pushScale.address().uniformOffset);
        return 133;
    }

    auto pushRangeEnd = static_cast<size_t>(pushScaleRange->offset) + static_cast<size_t>(pushScaleRange->size);
    auto scaleFieldWriteEnd = pushScale.address().uniformOffset + sizeof(float);
    if (scaleFieldWriteEnd > pushRangeEnd)
    {
        std::println(
            "[error] pushData.scale write range overflow: writeEnd={}, pushRangeEnd={}",
            scaleFieldWriteEnd,
            pushRangeEnd);
        return 134;
    }

    using CursorPushConstantOverload = void (nr::rhi::CursorPipelineLayout::*)(
        vk::CommandBuffer,
        const nr::rhi::ShaderCursor &,
        std::span<const uint8_t>) const;
    [[maybe_unused]] CursorPushConstantOverload pushConstantOverload = &nr::rhi::CursorPipelineLayout::pushConstants;

    printDescriptorLayout(layout);
    std::println("[ok] resource binding reflection and Vulkan mapping checks passed.");
    return 0;
}
} // namespace

int main()
{
    try
    {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = compileProgram("test/main/singleEntrypointContract");
        if (!program.valid())
        {
            std::println("[error] compileProgramByFile returned invalid program.");
            return 1;
        }

        if (program.entryPointCount() != 1)
        {
            std::println("[error] expected exactly one entrypoint, actual={}", program.entryPointCount());
            return 2;
        }

        auto const *entryPointBinary = program.entryPointData("csContract");
        if (!entryPointBinary)
        {
            std::println("[error] missing single entrypoint binary.");
            return 3;
        }

        if (entryPointBinary->entryPointName != "csContract")
        {
            std::println("[error] entrypoint name mismatch: expected='{}' actual='{}'", "csContract", entryPointBinary->entryPointName);
            return 4;
        }

        if (entryPointBinary->stage != SLANG_STAGE_COMPUTE)
        {
            std::println("[error] entrypoint stage mismatch: expected compute, actual={}", static_cast<int32_t>(entryPointBinary->stage));
            return 5;
        }

        if (!entryPointBinary->codeBlob || entryPointBinary->codeBlob->getBufferSize() == 0)
        {
            std::println("[error] entrypoint code blob is empty.");
            return 6;
        }

        auto *entryPointLayout = program.entryPointLayout("csContract");
        if (!entryPointLayout)
        {
            std::println("[error] missing single entrypoint layout.");
            return 7;
        }
        auto count = entryPointLayout->getParameterCount();
        for (unsigned i = 0; i < count; i++)
        {
            auto var = entryPointLayout->getParameterByIndex(static_cast<unsigned int>(i));
            std::println("  parameter[{}]: name='{}', type='{}'", i, var->getName(), var->getType()->getName());
        }
        auto *entryScopeVarLayout = entryPointLayout->getVarLayout();
        printBindingRanges("entrypoint", entryScopeVarLayout ? entryScopeVarLayout->getTypeLayout() : nullptr);

        count = program.programLayout()->getParameterCount();
        for (unsigned i = 0; i < count; i++)
        {
            auto var = program.programLayout()->getParameterByIndex(static_cast<unsigned int>(i));
            std::println("  program parameter[{}]: name='{}', type='{}'", i, var->getName(), var->getType()->getName());
        }
        auto *programScopeVarLayout = program.programLayout()->getGlobalParamsVarLayout();
        printBindingRanges("program", programScopeVarLayout ? programScopeVarLayout->getTypeLayout() : nullptr);

        auto descriptorLayout = nr::rhi::ShaderDescriptorLayout::create(program);
        if (!descriptorLayout.valid())
        {
            std::println("[error] descriptor layout invalid.");
            return 8;
        }

        if (descriptorLayout.pushConstantRanges().empty())
        {
            std::println("[error] expected reflected push-constant ranges, but found none.");
            return 9;
        }

        printDescriptorLayout(descriptorLayout);

        auto reflectionResult = runBindingReflectionChecks();
        if (reflectionResult != 0)
        {
            return reflectionResult;
        }

        std::println(
            "[ok] single-entrypoint contract verified: entry='{}', codeBytes={}, stage={}",
            entryPointBinary->entryPointName,
            entryPointBinary->codeBlob ? entryPointBinary->codeBlob->getBufferSize() : 0,
            static_cast<int32_t>(entryPointBinary->stage));
        return 0;
    }
    catch (const std::exception &e)
    {
        std::println("[error] exception: {}", e.what());
        return 10;
    }
}
