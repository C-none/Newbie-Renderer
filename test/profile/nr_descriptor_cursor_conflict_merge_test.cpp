import std;
import nr.rhi;

namespace
{
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

[[nodiscard]] nr::rhi::SlangProgram compileProgram(std::string_view sourcePath, std::string_view entryPoint)
{
    nr::rhi::SlangProgramCompileRequest request{
        .sourcePath = std::filesystem::path(sourcePath),
        .entryPoint = std::string(entryPoint),
    };
    return nr::rhi::ShaderService::instance().compileProgramByFileAndEntry(request);
}

[[nodiscard]] int runMergeChecks()
{
    auto program = compileProgram("test/descriptor/descriptorMerge", "csMerge");
    if (!program.valid())
    {
        std::println("[error] merge case compile failed.");
        return 1;
    }

    auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    if (!layout.valid())
    {
        std::println("[error] descriptor layout is invalid for merge case.");
        return 2;
    }

    auto root = layout.rootCursor();
    auto globalOut = root.field("globalOut");
    auto entryOut = root.field("entryOut");

    if (!globalOut.valid() || !entryOut.valid())
    {
        std::println("[error] root cursor did not merge program-level and entrypoint-level resources.");
        return 3;
    }

    auto globalFromPath = root.getPath("globalOut");
    auto entryFromPath = root.getPath("entryOut");
    if (!globalFromPath.valid() || !entryFromPath.valid())
    {
        std::println("[error] merged cursor getPath lookup failed for root fields.");
        return 4;
    }

    if (layout.descriptorSets().empty())
    {
        std::println("[error] descriptor sets are empty for merge case.");
        return 5;
    }

    printDescriptorLayout(layout);

    std::println("[ok] merge case verified: globalOut and entryOut coexist in one root cursor.");
    return 0;
}

[[nodiscard]] int runConflictTrigger()
{
    auto program = compileProgram("test/descriptor/descriptorConflict", "csConflict");
    if (!program.valid())
    {
        std::println("[error] conflict case compile failed unexpectedly.");
        return 21;
    }

    [[maybe_unused]] auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    std::println("[error] conflict case unexpectedly succeeded; expected nrAssert abort.");
    return 22;
}

[[nodiscard]] int runConflictParent(const std::filesystem::path &selfPath)
{
    auto command = std::format("\"{}\" --trigger-conflict", selfPath.string());
    auto exitCode = std::system(command.c_str());
    if (exitCode == 0)
    {
        std::println("[error] conflict subprocess exited with success, expected failure.");
        return 31;
    }

    std::println("[ok] conflict case verified via subprocess non-zero exit (code={}).", exitCode);
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        if (argc > 1 && std::string_view(argv[1]) == "--trigger-conflict")
        {
            return runConflictTrigger();
        }

        auto mergeResult = runMergeChecks();
        if (mergeResult != 0)
        {
            return mergeResult;
        }

        auto selfPath = std::filesystem::absolute(std::filesystem::path(argv[0]));
        return runConflictParent(selfPath);
    }
    catch (const std::exception &e)
    {
        std::println("[error] exception: {}", e.what());
        return 100;
    }
}
