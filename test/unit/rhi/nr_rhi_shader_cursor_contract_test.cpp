import std;
import dependency.math;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
template <typename VkHandle>
[[nodiscard]] VkHandle fakeVkHandle(std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<VkHandle>)
    {
        return reinterpret_cast<VkHandle>(value);
    }
    else
    {
        return static_cast<VkHandle>(value);
    }
}

struct UiPushConstants
{
    glm::vec2 scale{};
    glm::vec2 translate{};
    std::uint32_t textureIndex = 0;
    glm::uvec3 padding{};
};

static_assert(
    nr::rhi::ShaderServiceConfig{}.backendWorkerCount == 6u,
    "shader backend compilation must default to six workers");

const nr::test::CaseRegistrar shaderBatchCompileCase{
    "rhi shader batch compiler preserves single-entry request order",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto const requests = std::array{
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"test/sceneLightReflection"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/vertex"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/vertex"},
            },
        };
        auto programs = shaderService.compileProgramsByFile(requests);
        auto const stats = shaderService.lastCompileBatchStats();

        nr::test::requireEqual(programs.size(), requests.size());
        nr::test::require(
            std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
            "every single-entry batch request should compile");
        auto const expectedEntryPoints = std::array{
            std::pair{"main", SLANG_STAGE_COMPUTE},
            std::pair{"vertexMain", SLANG_STAGE_VERTEX},
            std::pair{"fragmentMain", SLANG_STAGE_FRAGMENT},
            std::pair{"vertexMain", SLANG_STAGE_VERTEX},
        };
        std::ranges::for_each(
            std::views::iota(std::size_t{0}, programs.size()),
            [&](std::size_t index) {
                auto const& program = programs[index];
                auto const& expected = expectedEntryPoints[index];
                auto const* entryPoint = program.entryPoint();
                nr::test::require(entryPoint != nullptr);
                nr::test::requireEqual(entryPoint->entryPointName, std::string{expected.first});
                nr::test::require(entryPoint->stage == expected.second);
            });
        nr::test::requireEqual(stats.requestCount, requests.size());
        nr::test::require(
            stats.memoryHitCount >= 1u,
            "an identical request in one batch should reuse the prepared program and fan out its result");
    }};

const nr::test::CaseRegistrar splitGraphicsReflectionRootCase{
    "rhi split graphics entrypoints preserve the canonical reflection-root ABI",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto const requests = std::array{
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/vertex"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/embeddedTriangle/vertex"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/embeddedTriangle/fragment"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/normalBuffer/vertex"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/normalBuffer/fragment"},
            },
        };
        auto programs = shaderService.compileProgramsByFile(requests);
        nr::test::requireEqual(programs.size(), requests.size());
        nr::test::require(
            std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
            "every split graphics entrypoint should compile");

        auto layouts = programs |
                       std::views::transform([](const nr::rhi::SlangProgram& program) {
                           return nr::rhi::ShaderDescriptorLayout::create(program);
                       }) |
                       std::ranges::to<std::vector>();
        nr::test::require(
            std::ranges::all_of(layouts, &nr::rhi::ShaderDescriptorLayout::valid),
            "every split graphics entrypoint should expose a valid descriptor layout");

        auto const pipelinePairs = std::array{
            std::pair{std::string_view{"appUi"}, std::size_t{0}},
            std::pair{std::string_view{"embeddedTriangle"}, std::size_t{2}},
            std::pair{std::string_view{"normalBuffer"}, std::size_t{4}},
        };
        std::ranges::for_each(pipelinePairs, [&](const auto& pipelinePair) {
            auto const& [pipelineName, vertexIndex] = pipelinePair;
            auto const& vertexLayout = layouts[vertexIndex];
            auto const& fragmentLayout = layouts[vertexIndex + 1u];
            auto const difference = nr::rhi::describeShaderLayoutAbiDifference(
                vertexLayout.abiSignature(),
                fragmentLayout.abiSignature());
            nr::test::require(
                nr::rhi::shaderLayoutAbiEquivalent(
                    vertexLayout.abiSignature(),
                    fragmentLayout.abiSignature()),
                std::format(
                    "split {} vertex/fragment programs must expose the same descriptor/push ABI: {}",
                    pipelineName,
                    difference));
        });

        auto requireRootFields = [&](std::size_t programIndex, std::span<const std::string_view> fieldNames) {
            auto root = layouts[programIndex].rootCursor();
            std::ranges::for_each(fieldNames, [&](std::string_view fieldName) {
                nr::test::require(
                    root.hasField(fieldName),
                    std::format(
                        "canonical reflection root '{}' must expose global field '{}'",
                        requests[programIndex].sourcePath.generic_string(),
                        fieldName));
            });
        };
        auto const appUiRootFields = std::array{
            std::string_view{"gUiTextures"},
            std::string_view{"gUiPush"},
        };
        auto const embeddedTriangleRootFields = std::array{
            std::string_view{"gFrame"},
        };
        auto const normalBufferRootFields = std::array{
            std::string_view{"gFrame"},
            std::string_view{"gSceneTextures"},
            std::string_view{"gPushConstants"},
        };
        requireRootFields(0u, appUiRootFields);
        requireRootFields(2u, embeddedTriangleRootFields);
        requireRootFields(4u, normalBufferRootFields);
    }};

const nr::test::CaseRegistrar persistentSpirvCacheCase{
    "rhi shader service reuses persistent SPIR-V after session reload",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();
        auto const request = nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/sceneLightReflection"},
        };

        {
            auto initialProgram = shaderService.compileProgramByFile(request);
            nr::test::require(
                initialProgram.valid(),
                "the initial single-entry shader compile should produce SPIR-V");
        }

        shaderService.reloadSession();
        auto restoredProgram = shaderService.compileProgramByFile(request);
        auto const stats = shaderService.lastCompileBatchStats();
        nr::test::require(
            restoredProgram.valid(),
            "the shader should remain valid when restored from persistent SPIR-V");
        nr::test::requireEqual(stats.requestCount, std::size_t{1});
        nr::test::require(
            stats.persistentHitCount >= 1u,
            "a fresh Slang session should reuse the persistent SPIR-V artifact");
        nr::test::requireEqual(
            stats.backendCompilationCount,
            std::size_t{0},
            "a persistent cache hit must skip Slang backend code generation");
    }};

const nr::test::CaseRegistrar globalFrameUniformCase{
    "rhi shader cursor exposes global frame uniform at set5 binding0",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/normalBuffer/fragment"},
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
        nr::test::requireEqual(normalBufferPushRange->size, 96u);
        auto const expectedPushFieldOffsets = std::array{
            std::pair{"modelRow0", std::size_t{0u}},
            std::pair{"modelRow1", std::size_t{16u}},
            std::pair{"modelRow2", std::size_t{32u}},
            std::pair{"normalUvLinear", std::size_t{48u}},
            std::pair{"normalUvOffsetScale", std::size_t{64u}},
            std::pair{"normalTextureMeta", std::size_t{80u}},
        };
        std::ranges::for_each(expectedPushFieldOffsets, [&](auto const& expected) {
            auto fieldCursor = pushConstantsCursor[expected.first];
            nr::test::require(
                fieldCursor.valid(),
                std::format("normalBuffer push field '{}' should resolve", expected.first));
            nr::test::requireEqual(
                fieldCursor.address().uniformOffset,
                expected.second,
                std::format("normalBuffer push field '{}' offset should match C++", expected.first));
        });

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
                .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
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
            .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
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
            .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
        });
        nr::test::require(program.valid(), "appUi shader program should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program, nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 16,
        });
        nr::test::require(layout.valid(), "appUi descriptor layout should be valid");

        auto root = layout.rootCursor();
        nr::test::require(!root.hasField("gSceneTextures"), "appUi shader must not inherit the scene texture table from common");
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

        auto resolvedWrites = nr::rhi::resolveDescriptorWriteRequests(
            snapshot,
            [](const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
               const nr::rhi::DescriptorBindingInfo&,
               std::uint32_t) -> std::optional<nr::rhi::DescriptorWritePayload> {
                nr::test::requireEqual(logicalResource.logicalResourceId, std::uint64_t{42});
                return nr::rhi::DescriptorWritePayload{
                    nr::rhi::ImageDescriptorWrite{
                        .imageView = vk::ImageView{fakeVkHandle<VkImageView>(0x2001u)},
                        .imageLayout = logicalResource.imageLayout,
                    }};
            });
        nr::test::requireEqual(resolvedWrites.size(), std::size_t{1});

        root.clearSnapshot();
        nr::test::require(root.snapshot().empty(), "clearSnapshot should remove descriptor and push writes");
    }};
} // namespace
