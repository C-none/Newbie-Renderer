import nr.rhi;
import std;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::println(std::cerr, "Usage: nr_shader_compile_check <shader-module-path>...");
        return 2;
    }

    auto sourcePaths = std::span<char *>{argv + 1, static_cast<std::size_t>(argc - 1)} |
                       std::views::transform([](const char *value) { return std::filesystem::path{value}; }) |
                       std::ranges::to<std::vector>();
    if (std::ranges::any_of(sourcePaths,
                            [](const auto &sourcePath) { return sourcePath.empty() || sourcePath.is_absolute(); }))
    {
        std::println(std::cerr, "Every shader module path must be relative to the configured shader root.");
        return 2;
    }

    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();

    auto requests = sourcePaths | std::views::transform([](const auto &sourcePath) {
                        return nr::rhi::SlangProgramCompileFileRequest{
                            .sourcePath = sourcePath,
                        };
                    }) |
                    std::ranges::to<std::vector>();
    auto programs = shaderService.compileProgramsByFile(requests);
    if (programs.size() != requests.size() || !std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid))
    {
        std::println(std::cerr, "Shader batch compilation failed.");
        return 1;
    }

    std::println("Compiled {} single-entry shader(s) successfully.", programs.size());
    return 0;
}
