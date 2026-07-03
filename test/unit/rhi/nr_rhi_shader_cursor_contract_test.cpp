import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
struct UiPushConstants
{
    glm::vec2 scale{};
    glm::vec2 translate{};
    std::uint32_t textureIndex = 0;
    glm::uvec3 padding{};
};

const nr::test::CaseRegistrar globalFrameUniformCase{
    "rhi shader cursor exposes global frame uniform at set5 binding0",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/normalBuffer"},
        });
        nr::test::require(program.valid(), "normalBuffer shader program should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "normalBuffer descriptor layout should be valid");

        auto descriptorSets = layout.descriptorSets();
        nr::test::require(
            std::ranges::any_of(descriptorSets, [](const nr::rhi::DescriptorSetLayoutInfo &setInfo) {
                return setInfo.set == 5u;
            }),
            "normalBuffer descriptor layout should expose set 5");
        nr::test::require(
            std::ranges::any_of(descriptorSets, [](const nr::rhi::DescriptorSetLayoutInfo &setInfo) {
                return setInfo.set == 1u;
            }),
            "normalBuffer descriptor layout should expose set 1 for scene textures");

        auto root = layout.rootCursor();
        auto pushConstantsCursor = root["gPushConstants"];
        nr::test::require(pushConstantsCursor.valid(), "gPushConstants cursor should resolve");
        auto normalBufferPushRange = pushConstantsCursor.pushConstantRange();
        nr::test::require(normalBufferPushRange.has_value(), "gPushConstants should have push constant reflection");
        nr::test::requireEqual(normalBufferPushRange->size, 72u);

        auto sceneTexturesCursor = root["gSceneTextures"];
        nr::test::require(sceneTexturesCursor.valid(), "gSceneTextures cursor should resolve");
        nr::test::require(sceneTexturesCursor.referencesRuntimeDescriptorArray(), "gSceneTextures should be runtime-sized");
        nr::test::require(sceneTexturesCursor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::requireEqual(*sceneTexturesCursor.bindingDescriptorCount(), 1024u);

        auto sceneTextureBinding = sceneTexturesCursor.descriptorBinding();
        nr::test::require(sceneTextureBinding.has_value(), "gSceneTextures should have descriptor binding reflection");
        nr::test::requireEqual(sceneTextureBinding->set, 1u);
        nr::test::requireEqual(sceneTextureBinding->binding, 0u);
        nr::test::require(sceneTextureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler);

        auto sceneTextureElement = sceneTexturesCursor[0u];
        nr::test::require(sceneTextureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 11,
            .debugName = "Renderer.SceneTexture[0]",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        auto sceneTextureSnapshot = root.snapshot();
        nr::test::requireEqual(sceneTextureSnapshot.descriptorWriteCount(), std::size_t{1});
        auto const &sceneTextureWrite = sceneTextureSnapshot.descriptorWrites().front();
        nr::test::requireEqual(sceneTextureWrite.binding.set, 1u);
        nr::test::requireEqual(sceneTextureWrite.binding.binding, 0u);
        nr::test::requireEqual(sceneTextureWrite.arrayElement, 0u);
        nr::test::require(sceneTextureWrite.binding.descriptorType == vk::DescriptorType::eCombinedImageSampler);
        root.clearSnapshot();

        auto frameCursor = root["gFrame"];
        nr::test::require(frameCursor.valid(), "gFrame cursor should resolve");
        nr::test::require(frameCursor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);

        auto binding = frameCursor.descriptorBinding();
        nr::test::require(binding.has_value(), "gFrame should have descriptor binding reflection");
        nr::test::requireEqual(binding->set, 5u);
        nr::test::requireEqual(binding->binding, 0u);
        nr::test::require(binding->descriptorType == vk::DescriptorType::eUniformBuffer);

        nr::test::require(frameCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 7,
            .debugName = "Renderer.GlobalFrameUniforms",
            .offset = 256,
            .range = 416,
        }));

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{1});
        auto const &write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.binding.set, 5u);
        nr::test::requireEqual(write.binding.binding, 0u);
        nr::test::require(std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(write.payload));
        auto const &logical = std::get<nr::rhi::LogicalResourceDescriptorWrite>(write.payload);
        nr::test::requireEqual(logical.logicalResourceId, std::uint64_t{7});
        nr::test::requireEqual(logical.offset, vk::DeviceSize{256});
        nr::test::requireEqual(logical.range, vk::DeviceSize{416});
    }};

const nr::test::CaseRegistrar shaderServiceReloadCase{
    "rhi shader service reload rebuilds session and preserves shader compilation",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        {
            auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi"},
            });
            nr::test::require(program.valid(), "appUi shader program should compile before session reload");
        }

        auto const generationBeforeReload = shaderService.sessionGeneration();
        shaderService.reloadSession();
        auto const generationAfterReload = shaderService.sessionGeneration();
        nr::test::require(
            generationAfterReload > generationBeforeReload,
            "session generation should increase after reload");

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/appUi"},
        });
        nr::test::require(program.valid(), "appUi shader program should compile after session reload");
    }};

const nr::test::CaseRegistrar sceneLightReflectionCase{
    "rhi shader cursor exposes scene light set6 bindings",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/sceneLightReflection"},
        });
        nr::test::require(program.valid(), "scene light reflection shader should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "scene light descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto lightHeader = root["gSceneLightHeader"];
        auto sceneLights = root["gSceneLights"];
        auto sceneLightAliasTable = root["gSceneLightAliasTable"];
        nr::test::require(lightHeader.valid(), "gSceneLightHeader cursor should resolve");
        nr::test::require(sceneLights.valid(), "gSceneLights cursor should resolve");
        nr::test::require(sceneLightAliasTable.valid(), "gSceneLightAliasTable cursor should resolve");
        nr::test::require(lightHeader.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(sceneLights.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(sceneLightAliasTable.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);

        auto headerBinding = lightHeader.descriptorBinding();
        auto lightsBinding = sceneLights.descriptorBinding();
        auto aliasBinding = sceneLightAliasTable.descriptorBinding();
        nr::test::require(headerBinding.has_value(), "gSceneLightHeader should expose descriptor binding reflection");
        nr::test::require(lightsBinding.has_value(), "gSceneLights should expose descriptor binding reflection");
        nr::test::require(aliasBinding.has_value(), "gSceneLightAliasTable should expose descriptor binding reflection");
        nr::test::requireEqual(headerBinding->set, 6u);
        nr::test::requireEqual(headerBinding->binding, 0u);
        nr::test::requireEqual(lightsBinding->set, 6u);
        nr::test::requireEqual(lightsBinding->binding, 1u);
        nr::test::requireEqual(aliasBinding->set, 6u);
        nr::test::requireEqual(aliasBinding->binding, 2u);

        nr::test::require(lightHeader.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 60u,
            .debugName = "Renderer.SceneLightHeader",
        }));
        nr::test::require(sceneLights.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 61u,
            .debugName = "Renderer.SceneLights",
        }));
        nr::test::require(sceneLightAliasTable.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 62u,
            .debugName = "Renderer.SceneLightAliasTable",
        }));

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{3});
        auto const& headerWrite = snapshot.descriptorWrites()[0];
        auto const& lightsWrite = snapshot.descriptorWrites()[1];
        auto const& aliasWrite = snapshot.descriptorWrites()[2];
        nr::test::requireEqual(headerWrite.binding.set, 6u);
        nr::test::requireEqual(headerWrite.binding.binding, 0u);
        nr::test::requireEqual(lightsWrite.binding.set, 6u);
        nr::test::requireEqual(lightsWrite.binding.binding, 1u);
        nr::test::requireEqual(aliasWrite.binding.set, 6u);
        nr::test::requireEqual(aliasWrite.binding.binding, 2u);
    }};

const nr::test::CaseRegistrar appUiCursorCase{
    "rhi shader cursor captures runtime descriptor array and push constants",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/appUi"},
        });
        nr::test::require(program.valid(), "appUi shader program should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program, nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 16,
        });
        nr::test::require(layout.valid(), "appUi descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto texturesCursor = root["gUiTextures"];
        nr::test::require(texturesCursor.valid(), "runtime texture cursor should resolve");
        nr::test::require(texturesCursor.referencesRuntimeDescriptorArray(), "gUiTextures should be runtime-sized");
        nr::test::require(texturesCursor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::requireEqual(*texturesCursor.bindingDescriptorCount(), 16u);

        auto textureElement = texturesCursor[5u];
        nr::test::require(textureElement.valid(), "runtime descriptor array element should resolve");
        nr::test::require(textureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 42,
            .debugName = "ui-test-texture",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));

        auto pushCursor = root["gUiPush"];
        nr::test::require(pushCursor.valid(), "push constant cursor should resolve");
        nr::test::require(pushCursor.bindingKind() == nr::rhi::ShaderBindingKind::PushConstant);
        nr::test::require(pushCursor.setData(UiPushConstants{
            .scale = glm::vec2{2.0f, 2.0f},
            .translate = glm::vec2{-1.0f, -1.0f},
            .textureIndex = 5,
        }));

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{1});
        nr::test::requireEqual(snapshot.pushConstantWriteCount(), std::size_t{1});
        auto const &write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.arrayElement, 5u);
        nr::test::require(write.binding.descriptorType == vk::DescriptorType::eCombinedImageSampler);
        nr::test::require(std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(write.payload));
        nr::test::requireEqual(std::get<nr::rhi::LogicalResourceDescriptorWrite>(write.payload).logicalResourceId, std::uint64_t{42});

        root.clearSnapshot();
        nr::test::require(root.snapshot().empty(), "clearSnapshot should remove descriptor and push writes");
    }};
} // namespace
