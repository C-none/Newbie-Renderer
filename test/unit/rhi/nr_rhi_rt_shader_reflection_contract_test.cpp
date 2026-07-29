#include <cstddef>

import std;
import dependency.math;
import dependency.vulkan;
import nr.resource;
import nr.rhi;
import nr.test;

namespace
{
struct CameraData
{
    glm::vec4 origin{};
    glm::vec4 right{};
    glm::vec4 up{};
    glm::vec4 forward{};
};

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingTestVariant(
    std::uint32_t maxSurfaceBounces = 16u,
    bool enableRussianRoulette = true)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant
        .assign(
            "kMaxSurfaceBounces",
            "uint",
            maxSurfaceBounces)
        .assign(
            "RussianRoulettePolicy",
            "IRussianRoulettePolicy",
            std::string{
                enableRussianRoulette ? "RussianRouletteEnabledPolicy"
                                      : "RussianRouletteDisabledPolicy"});
    return variant;
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingTestChsVariant(
    std::uint32_t layerMask = 0u)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign(
        "CHS",
        "ICHS",
        std::format(
            "MaterialCHS<RtMaterialLayerFlag({}u)>",
            layerMask));
    return variant;
}

[[nodiscard]] std::vector<std::string> effectiveShaderLines(std::string_view source)
{
    auto result = std::vector<std::string>{};
    auto stream = std::istringstream{std::string{source}};
    auto line = std::string{};
    while (std::getline(stream, line))
    {
        if (line.empty() || line.starts_with("//"))
        {
            continue;
        }
        result.push_back(line);
    }
    return result;
}

const nr::test::CaseRegistrar variantAssignmentSourceTextCase{
    "rhi shader variants generate deterministic assignment source",
    [] {
        auto constants = nr::rhi::SlangProgramVariantDesc{};
        constants
            .assign("kUIntValue", "uint", 7u)
            .assign("kBoolValue", "bool", true)
            .assign("kFloatValue", "float", 1.5f)
            .assign("kIntValue", "int", std::int32_t{-3});

        auto constantLines = effectiveShaderLines(constants.sourceText());
        nr::test::requireEqual(constantLines.size(), std::size_t{4u});
        nr::test::requireEqual(constantLines[0], std::string{"export static const bool kBoolValue = true;"});
        nr::test::requireEqual(constantLines[1], std::string{"export static const float kFloatValue = 1.5f;"});
        nr::test::requireEqual(constantLines[2], std::string{"export static const int kIntValue = -3;"});
        nr::test::requireEqual(constantLines[3], std::string{"export static const uint kUIntValue = 7u;"});

        auto alias = nr::rhi::SlangProgramVariantDesc{};
        alias.assign(
            "Policy",
            "IPolicy",
            std::string{"ConcretePolicy<1u, SomeEnum.value>"});

        auto aliasLines = effectiveShaderLines(alias.sourceText());
        nr::test::requireEqual(aliasLines.size(), std::size_t{2u});
        nr::test::requireEqual(aliasLines[0], std::string{"import common;"});
        nr::test::requireEqual(
            aliasLines[1],
            std::string{"export struct Policy : IPolicy = ConcretePolicy<1u, SomeEnum.value>;"});

        auto constantsReordered = nr::rhi::SlangProgramVariantDesc{};
        constantsReordered
            .assign("kIntValue", "int", std::int32_t{-3})
            .assign("kUIntValue", "uint", 7u)
            .assign("kBoolValue", "bool", true)
            .assign("kFloatValue", "float", 1.5f);

        nr::test::requireEqual(constantsReordered.hashValue(), constants.hashValue());
        nr::test::requireEqual(constantsReordered.sourceText(), constants.sourceText());
    }};

const nr::test::CaseRegistrar syntheticSourceCompileCase{
    "rhi shader service compiles synthetic source roots with link-time constants",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto variant = nr::rhi::SlangProgramVariantDesc{};
        variant.assign("kSyntheticSourceValue", "uint", 7u);

        auto program = shaderService.compileProgramFromSource(nr::rhi::SlangProgramCompileSourceRequest{
            .moduleName = "test.syntheticSourceRoot",
            .sourceText = R"(
public extern static const uint kSyntheticSourceValue;

[shader("compute")]
[numthreads(1, 1, 1)]
void csMain()
{
    uint value = kSyntheticSourceValue;
}
)",
            .variant = variant,
        });
        nr::test::require(program.valid(), "synthetic source shader should compile");
        nr::test::require(program.entryPointData("csMain") != nullptr, "synthetic source shader should expose csMain");
    }};

const nr::test::CaseRegistrar pathTracingChsLinkTimeTypeCase{
    "rhi shader service compiles path tracing CHS link-time generic type aliases",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto chsVariant = makePathTracingTestChsVariant();
        auto chsSource = chsVariant.sourceText();
        auto effectiveLines = effectiveShaderLines(chsSource);
        nr::test::requireEqual(effectiveLines.size(), std::size_t{2u});
        nr::test::requireEqual(effectiveLines[0], std::string{"import common;"});
        nr::test::requireEqual(
            effectiveLines[1],
            std::string{"export struct CHS : ICHS = MaterialCHS<RtMaterialLayerFlag(0u)>;"});

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = makePathTracingTestVariant(),
            .linkVariants = {chsVariant},
        });
        nr::test::require(program.valid(), "path tracing shader should compile with a CHS link-time type");
        nr::test::require(program.entryPointData("chMain") != nullptr, "path tracing shader should expose fixed chMain");
    }};

const nr::test::CaseRegistrar rtPipelineStageSelectionCase{
    "rhi rt stage selections can give specialized chMain stages distinct logical names",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto opaqueProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = makePathTracingTestVariant(),
            .linkVariants = {makePathTracingTestChsVariant()},
        });
        nr::test::require(opaqueProgram.valid(), "opaque CHS path tracing shader should compile");

        auto alphaMaskProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = makePathTracingTestVariant(),
            .linkVariants = {makePathTracingTestChsVariant()},
        });
        nr::test::require(alphaMaskProgram.valid(), "reused alpha-mask CHS path tracing shader should compile");

        auto selectedStages = std::array{
            nr::rhi::RayTracingPipelineStageSelection{
                .program = std::cref(opaqueProgram),
                .entryPointName = "chMain",
                .logicalEntryPointName = "ch_opaque",
            },
            nr::rhi::RayTracingPipelineStageSelection{
                .program = std::cref(alphaMaskProgram),
                .entryPointName = "chMain",
                .logicalEntryPointName = "ch_alphaMask",
            },
        };

        nr::test::require(selectedStages[0].program.get().entryPointData(selectedStages[0].entryPointName) != nullptr);
        nr::test::require(selectedStages[1].program.get().entryPointData(selectedStages[1].entryPointName) != nullptr);
        nr::test::requireEqual(selectedStages[0].entryPointName, selectedStages[1].entryPointName);
        nr::test::require(selectedStages[0].logicalEntryPointName != selectedStages[1].logicalEntryPointName);

        auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{
            .stages = selectedStages | std::ranges::to<std::vector>(),
            .groups = {
                nr::rhi::RayTracingShaderGroupDesc{
                    .name = "opaque",
                    .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
                    .closestHitEntryPoint = "ch_opaque",
                },
                nr::rhi::RayTracingShaderGroupDesc{
                    .name = "alphaMask",
                    .type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
                    .closestHitEntryPoint = "ch_alphaMask",
                },
            },
        };
        nr::test::require(
            !nr::rhi::validateRayTracingProgramAssemblyDesc(assembly).has_value(),
            "named RT program assembly should accept distinct logical CHS stages");

        auto duplicateGroupAssembly = assembly;
        duplicateGroupAssembly.groups[1].name = duplicateGroupAssembly.groups[0].name;
        nr::test::require(
            nr::rhi::validateRayTracingProgramAssemblyDesc(duplicateGroupAssembly).has_value(),
            "RT program assembly should reject duplicate group names");

        auto unknownEntryAssembly = assembly;
        unknownEntryAssembly.groups[0].closestHitEntryPoint = "ch_missing";
        nr::test::require(
            nr::rhi::validateRayTracingProgramAssemblyDesc(unknownEntryAssembly).has_value(),
            "RT program assembly should reject unknown logical entry points");
    }};

const nr::test::CaseRegistrar rtShaderReflectionCase{
    "rhi rt shader reflection exposes AS image and camera bindings",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/minimalRtTriangle"},
        });
        nr::test::require(program.valid(), "minimal RT shader should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "minimal RT descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto scene = root["scene"];
        auto outputImage = root["outputImage"];
        auto camera = root["camera"];

        nr::test::require(scene.valid(), "RT scene cursor should resolve");
        nr::test::require(outputImage.valid(), "RT output image cursor should resolve");
        nr::test::require(camera.valid(), "RT camera cursor should resolve");

        nr::test::require(scene.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(outputImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(camera.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);

        nr::test::require(scene.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 100,
            .debugName = "logical-tlas",
        }));
        nr::test::require(outputImage.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 101,
            .debugName = "logical-output",
        }));
        nr::test::require(camera.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 102,
            .debugName = "logical-camera",
        }));

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{3});
        auto semantics = snapshot.descriptorWrites() |
                         std::views::transform([](const nr::rhi::ShaderBindingRecord &record) {
                             return record.binding.semantic();
                         }) |
                         std::ranges::to<std::vector>();
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::AccelerationStructure));
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::StorageImage));
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::UniformBuffer));
    }};

const nr::test::CaseRegistrar rtObjectShaderReflectionCase{
    "rtobject present and path tracing shaders compile and expose expected bindings",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto presentProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/presentConvert"},
        });
        nr::test::require(presentProgram.valid(), "rtobject present conversion shader should compile");

        auto presentLayout = nr::rhi::ShaderDescriptorLayout::create(presentProgram);
        nr::test::require(presentLayout.valid(), "rtobject present conversion layout should be valid");

        auto presentRoot = presentLayout.rootCursor();
        auto sourceColor = presentRoot["gSourceColor"];
        auto uiColor = presentRoot["gUiColor"];
        auto convertedColor = presentRoot["gConvertedColor"];
        auto presentPushConstants = presentRoot["gPresentConvert"];

        nr::test::require(sourceColor.valid(), "present source color cursor should resolve");
        nr::test::require(uiColor.valid(), "present UI color cursor should resolve");
        nr::test::require(convertedColor.valid(), "present converted color cursor should resolve");
        nr::test::require(presentPushConstants.valid(), "present push constant cursor should resolve");
        nr::test::require(sourceColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::require(uiColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::require(convertedColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(presentPushConstants.pushConstantRange().has_value());

        auto rtProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = makePathTracingTestVariant(),
            .linkVariants = {makePathTracingTestChsVariant()},
        });
        nr::test::require(rtProgram.valid(), "rtobject path tracing shader should compile");

        auto rtLayout = nr::rhi::ShaderDescriptorLayout::create(rtProgram);
        nr::test::require(rtLayout.valid(), "rtobject path tracing layout should be valid");

        auto rtRoot = rtLayout.rootCursor();
        auto scene = rtRoot["scene"];
        auto outputImage = rtRoot["outputImage"];
        auto frameUniform = rtRoot["gFrame"];
        auto instanceMetadata = rtRoot["rtInstanceMetadata"];
        auto geometryMetadata = rtRoot["rtGeometryMetadata"];
        auto materialHeaders = rtRoot["rtMaterialHeaders"];
        auto materialLayers = rtRoot["rtMaterialLayers"];
        auto materialTextureRefs = rtRoot["rtMaterialTextureRefs"];
        auto vertexData = rtRoot["rtVertexData"];
        auto indexData = rtRoot["rtIndexData"];
        auto sceneTextures = rtRoot["gSceneTextures"];
        auto sceneLightHeader = rtRoot["gSceneLightHeader"];
        auto sceneLights = rtRoot["gSceneLights"];
        auto sceneLightAliasTable = rtRoot["gSceneLightAliasTable"];
        auto environmentMap = rtRoot["gEnvironmentMap"];
        auto environmentParameters = rtRoot["gEnvironment"];

        nr::test::require(scene.valid(), "path tracing TLAS cursor should resolve");
        nr::test::require(outputImage.valid(), "path tracing output cursor should resolve");
        nr::test::require(frameUniform.valid(), "path tracing global frame uniform cursor should resolve");
        nr::test::require(instanceMetadata.valid(), "RT instance metadata cursor should resolve");
        nr::test::require(geometryMetadata.valid(), "RT geometry metadata cursor should resolve");
        nr::test::require(materialHeaders.valid(), "RT material headers cursor should resolve");
        nr::test::require(materialLayers.valid(), "RT material layers cursor should resolve");
        nr::test::require(materialTextureRefs.valid(), "RT material texture refs cursor should resolve");
        nr::test::require(vertexData.valid(), "RT vertex atlas cursor should resolve");
        nr::test::require(indexData.valid(), "RT index atlas cursor should resolve");
        nr::test::require(sceneTextures.valid(), "path tracing scene texture table cursor should resolve");
        nr::test::require(sceneLightHeader.valid(), "path tracing scene light header cursor should resolve");
        nr::test::require(sceneLights.valid(), "path tracing scene light list cursor should resolve");
        nr::test::require(sceneLightAliasTable.valid(), "path tracing scene light alias table cursor should resolve");
        nr::test::require(environmentMap.valid(), "path tracing environment map cursor should resolve");
        nr::test::require(environmentParameters.valid(), "path tracing environment parameters should resolve");
        nr::test::require(scene.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(outputImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(frameUniform.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(instanceMetadata.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(geometryMetadata.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(materialHeaders.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(materialLayers.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(materialTextureRefs.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(vertexData.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(indexData.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(sceneTextures.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::require(sceneLightHeader.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(sceneLights.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(sceneLightAliasTable.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(
            environmentMap.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler,
            "path tracing environment map should use a combined image sampler");
        auto environmentPushRange = environmentParameters.pushConstantRange();
        nr::test::require(environmentPushRange.has_value(), "path tracing environment parameters should use push constants");
        nr::test::requireEqual(environmentPushRange->size, 16u);
        nr::test::require(
            environmentMap.makeImmutableSamplerBinding(nr::rhi::SlangSamplerDesc{}).has_value(),
            "path tracing environment map should support an immutable sampler");

        auto sceneLightHeaderBinding = sceneLightHeader.descriptorBinding();
        auto sceneLightsBinding = sceneLights.descriptorBinding();
        auto sceneLightAliasTableBinding = sceneLightAliasTable.descriptorBinding();
        nr::test::require(sceneLightHeaderBinding.has_value(), "path tracing scene light header should expose binding");
        nr::test::require(sceneLightsBinding.has_value(), "path tracing scene lights should expose binding");
        nr::test::require(sceneLightAliasTableBinding.has_value(), "path tracing scene light alias table should expose binding");
        nr::test::requireEqual(sceneLightHeaderBinding->set, 6u);
        nr::test::requireEqual(sceneLightHeaderBinding->binding, 0u);
        nr::test::requireEqual(sceneLightsBinding->set, 6u);
        nr::test::requireEqual(sceneLightsBinding->binding, 1u);
        nr::test::requireEqual(sceneLightAliasTableBinding->set, 6u);
        nr::test::requireEqual(sceneLightAliasTableBinding->binding, 2u);
    }};

const nr::test::CaseRegistrar pathTracingLinkTimeVariantCase{
    "path tracing link-time variants compile and keep shader layout ABI",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto baselineVariant = makePathTracingTestVariant();
        auto baselineProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = baselineVariant,
            .linkVariants = {makePathTracingTestChsVariant()},
        });
        nr::test::require(baselineProgram.valid(), "path tracing baseline shader should compile");

        auto constantVariant = makePathTracingTestVariant(2u);

        auto constantProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = constantVariant,
            .linkVariants = {makePathTracingTestChsVariant()},
        });
        nr::test::require(constantProgram.valid(), "path tracing constant variant should compile");

        auto typeVariant = makePathTracingTestVariant(16u, false);

        auto typeProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = typeVariant,
            .linkVariants = {makePathTracingTestChsVariant()},
        });
        nr::test::require(typeProgram.valid(), "path tracing type alias variant should compile");

        auto descriptorPolicy = nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 1024u,
        };
        auto baselineLayout = nr::rhi::ShaderDescriptorLayout::create(baselineProgram, descriptorPolicy);
        auto constantLayout = nr::rhi::ShaderDescriptorLayout::create(constantProgram, descriptorPolicy);
        auto typeLayout = nr::rhi::ShaderDescriptorLayout::create(typeProgram, descriptorPolicy);
        nr::test::require(baselineLayout.valid(), "path tracing baseline layout should be valid");
        nr::test::require(constantLayout.valid(), "path tracing constant variant layout should be valid");
        nr::test::require(typeLayout.valid(), "path tracing type variant layout should be valid");
        nr::test::require(
            nr::rhi::shaderLayoutAbiEquivalent(baselineLayout.abiSignature(), constantLayout.abiSignature()),
            "path tracing constant variant should keep descriptor/push ABI");
        nr::test::require(
            nr::rhi::shaderLayoutAbiEquivalent(baselineLayout.abiSignature(), typeLayout.abiSignature()),
            "path tracing type variant should keep descriptor/push ABI");

        auto presentProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/presentConvert"},
        });
        nr::test::require(presentProgram.valid(), "present shader should compile for ABI difference check");
        auto presentLayout = nr::rhi::ShaderDescriptorLayout::create(presentProgram, descriptorPolicy);
        nr::test::require(presentLayout.valid(), "present shader layout should be valid for ABI difference check");
        auto abiDiff = nr::rhi::describeShaderLayoutAbiDifference(
            baselineLayout.abiSignature(),
            presentLayout.abiSignature());
        nr::test::require(!abiDiff.empty(), "ABI difference diagnostics should describe a mismatched layout");

        auto const generationBeforeReload = shaderService.sessionGeneration();
        shaderService.reloadSession();
        nr::test::require(
            shaderService.sessionGeneration() > generationBeforeReload,
            "shader service reload should advance session generation for variant cache invalidation");

        auto reloadedVariantProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
            .variant = constantVariant,
            .linkVariants = {makePathTracingTestChsVariant(4u)},
        });
        nr::test::require(reloadedVariantProgram.valid(), "path tracing variant should compile after session reload");
    }};

const nr::test::CaseRegistrar rtVertexAtlasLayoutCase{
    "path tracing shader vertex atlas offsets match resource vertex layout",
    [] {
        nr::test::requireEqual(offsetof(nr::resource::Vertex, position), std::size_t{0u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, normal), std::size_t{12u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, tangent), std::size_t{24u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, texCoord0), std::size_t{40u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, texCoord1), std::size_t{48u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, color0), std::size_t{56u});
    }};

const nr::test::CaseRegistrar rtMaterialTextureIdsReflectionCase{
    "rt shader common material texture id helper exposes scene texture table",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/materialTextureIdsRt"},
        });
        nr::test::require(program.valid(), "material texture id RT shader should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program, nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 1024,
        });
        nr::test::require(layout.valid(), "material texture id RT descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto sceneTextures = root["gSceneTextures"];
        nr::test::require(sceneTextures.valid(), "RT gSceneTextures cursor should resolve");
        nr::test::require(sceneTextures.referencesRuntimeDescriptorArray(), "RT gSceneTextures should be runtime-sized");
        nr::test::require(sceneTextures.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::requireEqual(*sceneTextures.bindingDescriptorCount(), 1024u);

        auto sceneTextureBinding = sceneTextures.descriptorBinding();
        nr::test::require(sceneTextureBinding.has_value(), "RT gSceneTextures should expose descriptor binding reflection");
        nr::test::requireEqual(sceneTextureBinding->set, 1u);
        nr::test::requireEqual(sceneTextureBinding->binding, 0u);
        nr::test::require(sceneTextureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler);
        nr::test::require(sceneTextureBinding->supportsVariableDescriptorCount());

        auto sceneTextureImmutableSampler = sceneTextures.makeImmutableSamplerBinding(nr::rhi::SlangSamplerDesc{});
        nr::test::require(sceneTextureImmutableSampler.has_value(), "RT gSceneTextures should allow immutable sampler binding");
        nr::test::requireEqual(sceneTextureImmutableSampler->set, 1u);
        nr::test::requireEqual(sceneTextureImmutableSampler->binding, 0u);
        nr::test::requireEqual(sceneTextureImmutableSampler->descriptorCount, 1024u);

        auto sceneTextureElement = sceneTextures[7u];
        nr::test::require(sceneTextureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 77,
            .debugName = "RT.SceneTexture[7]",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));

        auto outputImage = root["outputImage"];
        nr::test::require(outputImage.valid(), "RT material helper output image cursor should resolve");
        nr::test::require(outputImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);

        auto materialTextureIds = root["gMaterialTextureIds"];
        nr::test::require(materialTextureIds.valid(), "RT material helper push constants should resolve");
        nr::test::require(materialTextureIds.pushConstantRange().has_value());

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{1});
        auto const& write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.binding.set, 1u);
        nr::test::requireEqual(write.binding.binding, 0u);
        nr::test::requireEqual(write.arrayElement, 7u);
        nr::test::require(write.binding.descriptorType == vk::DescriptorType::eCombinedImageSampler);
    }};
} // namespace
