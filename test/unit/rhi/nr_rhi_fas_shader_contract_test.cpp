import std;
import dependency.slang;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
const nr::test::CaseRegistrar materialFilterContractCase{
    "material FAS contract compiles both root policies with stable layout ABI",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto compileContract = [&](bool enableFilterAfterShading) {
            auto variant = nr::rhi::SlangProgramVariantDesc{};
            variant.assign(
                "kEnableFilterAfterShading",
                "bool",
                enableFilterAfterShading);
            return shaderService.compileProgramByFile(
                nr::rhi::SlangProgramCompileFileRequest{
                    .sourcePath = std::filesystem::path{
                        "test/rt/materialFilterAfterShadingContract"},
                    .variant = variant,
                });
        };

        auto offProgram = compileContract(false);
        auto onProgram = compileContract(true);
        nr::test::require(
            offProgram.valid() && onProgram.valid(),
            "material FAS contract should compile both root link-time policies");
        nr::test::require(
            offProgram.entryPointBlob("computeMain") != nullptr &&
                onProgram.entryPointBlob("computeMain") != nullptr,
            "material FAS contract should expose off/on compute SPIR-V");

        auto* programLayout = offProgram.programLayout();
        nr::test::require(
            programLayout != nullptr,
            "material FAS contract should expose reflection");
        auto* rayPayloadType =
            programLayout->findTypeByName("ResolvedMaterialRayPayload");
        auto* rayPayloadLayout = rayPayloadType != nullptr
            ? programLayout->getTypeLayout(rayPayloadType)
            : nullptr;
        nr::test::require(
            rayPayloadLayout != nullptr,
            "material FAS contract should reflect ResolvedMaterialRayPayload");
        auto const rayPayloadSize = static_cast<std::size_t>(
            rayPayloadLayout->getSize());
        auto const rayPayloadStride = static_cast<std::size_t>(
            rayPayloadLayout->getStride());
        nr::test::require(
            rayPayloadSize == 188u,
            std::format(
                "lossless ResolvedMaterialRayPayload should remain 47 uint32 lanes: size={}, stride={}",
                rayPayloadSize,
                rayPayloadStride));
        auto* rayPayloadWrapperType =
            programLayout->findTypeByName("RayPayload");
        auto* rayPayloadWrapperLayout = rayPayloadWrapperType != nullptr
            ? programLayout->getTypeLayout(rayPayloadWrapperType)
            : nullptr;
        nr::test::require(
            rayPayloadWrapperLayout != nullptr &&
                rayPayloadWrapperLayout->getSize() == rayPayloadSize,
            "the stage-facing RayPayload wrapper must add no lanes to its resolved transport record");

        auto offLayout = nr::rhi::ShaderDescriptorLayout::create(offProgram);
        auto onLayout = nr::rhi::ShaderDescriptorLayout::create(onProgram);
        nr::test::require(
            offLayout.valid() && onLayout.valid(),
            "material FAS contract layouts should be valid");
        nr::test::require(
            nr::rhi::shaderLayoutAbiEquivalent(
                offLayout.abiSignature(),
                onLayout.abiSignature()),
            "material FAS root policies must preserve contract layout ABI");
    }};
} // namespace
