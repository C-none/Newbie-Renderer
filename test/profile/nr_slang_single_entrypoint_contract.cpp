import std;
import nr.rhi;

namespace
{
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
}
} // namespace

int main()
{
    try
    {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        nr::rhi::SlangProgramCompileRequest request{
            .sourcePath = std::filesystem::path("test/main/singleEntrypointContract"),
            .entryPoint = "csContract",
        };

        auto program = shaderService.compileProgramByFileAndEntry(request);
        if (!program.valid())
        {
            std::println("[error] compileProgramByFileAndEntry returned invalid program.");
            return 1;
        }

        auto const *entryPointBinary = program.entryPointBinary();
        if (!entryPointBinary)
        {
            std::println("[error] missing single entrypoint binary.");
            return 2;
        }

        if (entryPointBinary->entryPointName != request.entryPoint)
        {
            std::println("[error] entrypoint name mismatch: expected='{}' actual='{}'", request.entryPoint, entryPointBinary->entryPointName);
            return 3;
        }

        if (entryPointBinary->stage != SLANG_STAGE_COMPUTE)
        {
            std::println("[error] entrypoint stage mismatch: expected compute, actual={}", static_cast<int32_t>(entryPointBinary->stage));
            return 4;
        }

        if (entryPointBinary->spirv.empty())
        {
            std::println("[error] entrypoint spirv blob is empty.");
            return 5;
        }

        auto *entryPointLayout = program.entryPointLayout();
        if (!entryPointLayout)
        {
            std::println("[error] missing single entrypoint layout.");
            return 6;
        }
        auto count = entryPointLayout->getParameterCount();
        for (unsigned i = 0; i < count; i++)
        {
            auto var = entryPointLayout->getParameterByIndex(static_cast<unsigned int>(i));
            std::println("  parameter[{}]: name='{}', type='{}'", i, var->getName(), var->getType()->getName());
        }
        printBindingRanges("entrypoint", entryPointLayout->getTypeLayout());

        count = program.programLayout()->getParameterCount();
        for (unsigned i = 0; i < count; i++)
        {
            auto var = program.programLayout()->getParameterByIndex(static_cast<unsigned int>(i));
            std::println("  program parameter[{}]: name='{}', type='{}'", i, var->getName(), var->getType()->getName());
        }
        printBindingRanges("program", program.programLayout()->getGlobalParamsTypeLayout());

        auto descriptorLayout = nr::rhi::ShaderDescriptorLayout::create(program);
        if (!descriptorLayout.valid())
        {
            std::println("[error] descriptor layout invalid.");
            return 7;
        }
        printDescriptorLayout(descriptorLayout);

        std::println(
            "[ok] single-entrypoint contract verified: entry='{}', words={}, stage={}",
            entryPointBinary->entryPointName,
            entryPointBinary->spirv.size(),
            static_cast<int32_t>(entryPointBinary->stage));
        return 0;
    }
    catch (const std::exception &e)
    {
        std::println("[error] exception: {}", e.what());
        return 10;
    }
}
