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

[[nodiscard]] bool hasStage(const nr::rhi::SlangProgram& program, SlangStage stage)
{
    return std::ranges::any_of(program.entryPoints(), [stage](const nr::rhi::SlangEntryPointData& entryPoint) {
        return entryPoint.stage == stage;
    });
}
}

int main()
{
    try
    {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        constexpr std::string_view sourcePath = "test/main/twoGraphicsEntrypoints";
        auto program = compileProgram(sourcePath);
        if (!program.valid())
        {
            std::println("[error] compile failed for module='{}'.", sourcePath);
            return 1;
        }

        if (program.entryPointCount() < 2)
        {
            std::println("[error] expected at least two entrypoints for module='{}', got {}.", sourcePath, program.entryPointCount());
            return 2;
        }

        auto const* vs = program.entryPointData("vsMain");
        auto const* fs = program.entryPointData("fsMain");
        if (!vs || !fs)
        {
            std::println("[error] expected vsMain and fsMain entrypoints in module='{}'.", sourcePath);
            return 3;
        }

        if (vs->stage != SLANG_STAGE_VERTEX || fs->stage != SLANG_STAGE_FRAGMENT)
        {
            std::println(
                "[error] stage mismatch: vsMain={}, fsMain={}",
                static_cast<int32_t>(vs->stage),
                static_cast<int32_t>(fs->stage));
            return 4;
        }

        if (!hasStage(program, SLANG_STAGE_VERTEX) || !hasStage(program, SLANG_STAGE_FRAGMENT))
        {
            std::println("[error] graphics stage filtering precondition failed for module='{}'.", sourcePath);
            return 5;
        }

        std::println("[ok] multi-entrypoint compile by file works for graphics module '{}'.", sourcePath);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::println("[error] exception: {}", e.what());
        return 1;
    }
}
