import std;
import nr.rhi;

namespace
{
void printDescriptorLayout(const nr::rhi::ShaderDescriptorLayout &layout)
{
    std::println("[info] global-scope descriptor sets={}", layout.descriptorSets().size());
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

[[nodiscard]] nr::rhi::SlangProgram compileProgram(std::string_view sourcePath)
{
    nr::rhi::SlangProgramCompileFileRequest request{
        .sourcePath = std::filesystem::path(sourcePath),
    };
    return nr::rhi::ShaderService::instance().compileProgramByFile(request);
}

[[nodiscard]] int runMergeChecks()
{
    auto program = compileProgram("test/descriptor/descriptorMerge");
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
    auto globalOutByIndex = root["globalOut"];
    auto globalIn = root.field("globalIn");
    auto entryOut = root.field("entryOut");

    if (!globalOut.valid() || !globalOutByIndex.valid() || !globalIn.valid())
    {
        std::println("[error] root cursor missing expected global-scope resources.");
        return 3;
    }

    auto globalFromPath = root.getPath("globalOut");
    if (!globalFromPath.valid())
    {
        std::println("[error] global cursor getPath lookup failed for root fields.");
        return 4;
    }

    if (entryOut.valid() || root.getPath("entryOut").valid())
    {
        std::println("[error] entrypoint resource unexpectedly appeared in descriptor root cursor.");
        return 6;
    }

    if (layout.descriptorSets().empty())
    {
        std::println("[error] descriptor sets are empty for merge case.");
        return 5;
    }

    printDescriptorLayout(layout);

    std::println("[ok] global-only reflection verified: descriptor root cursor exposes program-scope resources only.");
    return 0;
}

[[nodiscard]] int runConflictTrigger()
{
    auto program = compileProgram("test/descriptor/descriptorConflict");
    if (!program.valid())
    {
        std::println("[error] conflict case compile failed unexpectedly.");
        return 21;
    }

    [[maybe_unused]] auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    std::println("[error] entrypoint-resource forbidden case unexpectedly succeeded; expected nrAssert abort.");
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

    std::println("[ok] entrypoint-resource forbidden case verified via subprocess non-zero exit (code={}).", exitCode);
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
