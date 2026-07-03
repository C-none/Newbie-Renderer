#include <cstddef>

import std;
import dependency;
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
    "rtobject display and path tracing shaders compile and expose expected bindings",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto displayProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/displayConvert"},
        });
        nr::test::require(displayProgram.valid(), "rtobject display conversion shader should compile");

        auto displayLayout = nr::rhi::ShaderDescriptorLayout::create(displayProgram);
        nr::test::require(displayLayout.valid(), "rtobject display conversion layout should be valid");

        auto displayRoot = displayLayout.rootCursor();
        auto sourceColor = displayRoot["gSourceColor"];
        auto convertedColor = displayRoot["gConvertedColor"];
        auto displayPushConstants = displayRoot["gDisplayConvert"];

        nr::test::require(sourceColor.valid(), "display source color cursor should resolve");
        nr::test::require(convertedColor.valid(), "display converted color cursor should resolve");
        nr::test::require(displayPushConstants.valid(), "display push constant cursor should resolve");
        nr::test::require(sourceColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::require(convertedColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(displayPushConstants.pushConstantRange().has_value());

        auto rtProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing"},
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
