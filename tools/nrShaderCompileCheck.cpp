import nr.rhi;
import std;

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::println(std::cerr, "Usage: nr_shader_compile_check <shader-module-path>");
        return 2;
    }

    auto const sourcePath = std::filesystem::path{argv[1]};
    if (sourcePath.empty() || sourcePath.is_absolute())
    {
        std::println(std::cerr, "Shader module path must be relative to the configured shader root.");
        return 2;
    }

    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();

    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = sourcePath,
    });
    shaderService.waitForPendingModuleCacheWrites();
    if (!program.valid())
    {
        std::println(std::cerr, "Shader compilation failed for '{}'.", sourcePath.generic_string());
        return 1;
    }

    std::println("Shader '{}' compiled successfully with {} entry point(s).", sourcePath.generic_string(), program.entryPointCount());
    return 0;
}
