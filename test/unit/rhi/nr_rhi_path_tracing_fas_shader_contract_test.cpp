import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingVariant(
    bool enableFilterAfterShading)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant
        .assign("kMaxSurfaceBounces", "uint", 16u)
        .assign(
            "kEnableFilterAfterShading",
            "bool",
            enableFilterAfterShading)
        .assign(
            "RussianRoulettePolicy",
            "IRussianRoulettePolicy",
            std::string{"RussianRouletteEnabledPolicy"});
    return variant;
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makeFullyLayeredChsVariant()
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign(
        "CHS",
        "ICHS",
        std::string{"MaterialCHS<RtMaterialLayerFlag(31u)>"});
    return variant;
}

const nr::test::CaseRegistrar pathTracingFasVariantCase{
    "path tracing FAS root variants compile with stable layout ABI",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto offVariant = makePathTracingVariant(false);
        auto onVariant = makePathTracingVariant(true);
        nr::test::require(
            offVariant.hashValue() != onVariant.hashValue(),
            "FAS must be a distinct root link-time variant");

        auto chsVariant = makeFullyLayeredChsVariant();
        auto offProgram = shaderService.compileProgramByFile(
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing"},
                .variant = offVariant,
                .linkVariants = {chsVariant},
            });
        auto onProgram = shaderService.compileProgramByFile(
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing"},
                .variant = onVariant,
                .linkVariants = {chsVariant},
            });
        nr::test::require(
            offProgram.valid() && onProgram.valid(),
            "fully layered path tracing FAS variants should compile");

        auto descriptorPolicy = nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 1024u,
        };
        auto offLayout =
            nr::rhi::ShaderDescriptorLayout::create(
                offProgram,
                descriptorPolicy);
        auto onLayout =
            nr::rhi::ShaderDescriptorLayout::create(
                onProgram,
                descriptorPolicy);
        nr::test::require(
            offLayout.valid() && onLayout.valid(),
            "fully layered path tracing FAS layouts should be valid");
        nr::test::require(
            nr::rhi::shaderLayoutAbiEquivalent(
                offLayout.abiSignature(),
                onLayout.abiSignature()),
            "FAS-off and FAS-on path tracing variants must preserve descriptor and push ABI");
    }};
} // namespace
