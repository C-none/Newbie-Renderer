import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingClosestHitVariant(bool enableFilterAfterShading)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign("kEnableFilterAfterShading", "bool", enableFilterAfterShading)
        .assign("CHS", "ICHS", std::string{"MaterialCHS<RtMaterialLayerFlag(31u)>"});
    return variant;
}

const nr::test::CaseRegistrar pathTracingFasVariantCase{
    "path tracing FAS closest-hit variants compile with stable layout ABI", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto offVariant = makePathTracingClosestHitVariant(false);
        auto onVariant = makePathTracingClosestHitVariant(true);
        nr::test::require(offVariant.hashValue() != onVariant.hashValue(),
                          "FAS must be a distinct closest-hit link-time variant");

        auto offProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
            .variant = offVariant,
        });
        auto onProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
            .variant = onVariant,
        });
        nr::test::require(offProgram.valid() && onProgram.valid(),
                          "fully layered path tracing FAS variants should compile");

        auto descriptorPolicy = nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 1024u,
        };
        auto offLayout = nr::rhi::ShaderDescriptorLayout::create(offProgram, descriptorPolicy);
        auto onLayout = nr::rhi::ShaderDescriptorLayout::create(onProgram, descriptorPolicy);
        nr::test::require(offLayout.valid() && onLayout.valid(),
                          "fully layered path tracing FAS layouts should be valid");
        nr::test::require(nr::rhi::shaderLayoutAbiEquivalent(offLayout.abiSignature(), onLayout.abiSignature()),
                          "FAS-off and FAS-on path tracing variants must preserve descriptor and push ABI");
    }};
} // namespace
