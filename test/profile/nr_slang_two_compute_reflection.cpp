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

    auto rangeCount = std::max<SlangInt>(0, typeLayout->getBindingRangeCount());
    std::println("[info] {} bindingRangeCount={}", label, rangeCount);
    for (SlangInt i = 0; i < rangeCount; ++i)
    {
        std::println(
            "  {} bindingRange[{}]: set={}, binding={}, count={}, type={}",
            label,
            i,
            typeLayout->getBindingRangeDescriptorSetIndex(i),
            typeLayout->getBindingRangeFirstDescriptorRangeIndex(i),
            typeLayout->getBindingRangeBindingCount(i),
            static_cast<int32_t>(typeLayout->getBindingRangeType(i)));
    }
}

template <typename TLayout>
void printParameters(std::string_view label, TLayout *layout)
{
    if (!layout)
    {
        std::println("[warn] {} layout is null.", label);
        return;
    }

    auto parameterCount = layout->getParameterCount();
    std::println("[info] {} parameterCount={}", label, parameterCount);

    for (unsigned i = 0; i < parameterCount; ++i)
    {
        auto *var = layout->getParameterByIndex(i);
        if (!var)
        {
            continue;
        }

        auto *type = var->getType();
        auto typeName = (type && type->getName()) ? type->getName() : "<unnamed-type>";
        auto typeKind = type ? static_cast<int32_t>(type->getKind()) : -1;
        std::println(
            "  {} parameter[{}]: name='{}', type='{}', kind={}",
            label,
            i,
            var->getName() ? var->getName() : "<unnamed>",
            typeName,
            typeKind);
    }
}

void dumpProgramReflection(const nr::rhi::SlangProgram &program, std::string_view entryPoint)
{
    std::println("\n================ {} ================", entryPoint);

    auto const *entryPointBinary = program.entryPointData(entryPoint);
    if (entryPointBinary)
    {
        auto codeBytes = entryPointBinary->codeBlob ? entryPointBinary->codeBlob->getBufferSize() : 0;
        std::println(
            "[info] compiled entry='{}', stage={}, codeBytes={}",
            entryPointBinary->entryPointName,
            static_cast<int32_t>(entryPointBinary->stage),
            codeBytes);
    }

    auto *programLayout = program.programLayout();
    printParameters("program", programLayout);
    if (programLayout)
    {
        auto *globalScopeVarLayout = programLayout->getGlobalParamsVarLayout();
        printBindingRanges("program", globalScopeVarLayout ? globalScopeVarLayout->getTypeLayout() : nullptr);
    }

    auto *entryLayout = program.entryPointLayout(entryPoint);
    printParameters("entrypoint", entryLayout);
    if (entryLayout)
    {
        auto *entryScopeVarLayout = entryLayout->getVarLayout();
        printBindingRanges("entrypoint", entryScopeVarLayout ? entryScopeVarLayout->getTypeLayout() : nullptr);
    }
}
} // namespace

int main()
{
    try
    {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        constexpr std::string_view sourcePath = "test/twoComputeReflection";
        auto program = compileProgram(sourcePath);
        if (!program.valid())
        {
            std::println("[error] compile failed for module='{}'.", sourcePath);
            return 1;
        }

        dumpProgramReflection(program, "csShader1");
        dumpProgramReflection(program, "csShader2");

        if (!program.entryPointData("csShader1") || !program.entryPointData("csShader2"))
        {
            std::println("[error] expected both compute entrypoints are present after module compile.");
            return 2;
        }

        std::println("\n[ok] reflection dump finished for two compute entrypoints.");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::println("[error] exception: {}", e.what());
        return 1;
    }
}
