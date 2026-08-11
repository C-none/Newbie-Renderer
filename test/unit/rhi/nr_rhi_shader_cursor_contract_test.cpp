import std;
import dependency.math;
import dependency.slang;
import dependency.vulkan;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
static_assert(std::is_standard_layout_v<DirectX::XMFLOAT3>);
static_assert(std::is_trivially_copyable_v<DirectX::XMFLOAT3>);
static_assert(sizeof(DirectX::XMFLOAT3) == 12u);
static_assert(sizeof(DirectX::XMFLOAT4) == 16u);
static_assert(sizeof(DirectX::XMFLOAT4X3) == 48u);
static_assert(sizeof(DirectX::XMFLOAT4X4) == 64u);

template <typename VkHandle> [[nodiscard]] VkHandle fakeVkHandle(std::uintptr_t value) noexcept
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
    DirectX::XMFLOAT2 scale{};
    DirectX::XMFLOAT2 translate{};
    std::uint32_t textureIndex = 0;
    DirectX::XMUINT3 padding{};
};

struct AccumulatePushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t resetHistory = 0u;
    std::uint32_t historySampleCount = 0u;
    std::uint32_t maxHistorySampleCount = 0u;
};

static_assert(std::is_standard_layout_v<AccumulatePushConstants>);
static_assert(sizeof(AccumulatePushConstants) == 20u);

struct DlssMotionVectorDebugPushConstants
{
    std::array<std::uint32_t, 2u> outputBase{};
    std::array<std::uint32_t, 2u> outputSize{};
    std::array<std::uint32_t, 2u> motionVectorBase{};
    std::array<std::uint32_t, 2u> motionVectorSize{};
    std::array<float, 2u> motionVectorScale{};
};

static_assert(std::is_standard_layout_v<DlssMotionVectorDebugPushConstants>);
static_assert(sizeof(DlssMotionVectorDebugPushConstants) == 40u);

struct DescriptorBindingExpectation
{
    std::string_view name;
    nr::rhi::ShaderDescriptorSemantic semantic;
    vk::DescriptorType descriptorType;
    std::uint32_t set;
    std::uint32_t binding;
};

[[nodiscard]] DirectX::XMFLOAT3 normalized(DirectX::XMFLOAT3 value) noexcept
{
    auto const normalizedValue = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&value));
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result, normalizedValue);
    return result;
}

[[nodiscard]] DirectX::XMFLOAT3 transformDirection(DirectX::XMFLOAT3 direction,
                                                    DirectX::XMMATRIX transform) noexcept
{
    auto const transformed = DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&direction), transform);
    auto result = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(&result, transformed);
    return result;
}

[[nodiscard]] float dot(DirectX::XMFLOAT3 lhs, DirectX::XMFLOAT3 rhs) noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&lhs), DirectX::XMLoadFloat3(&rhs)));
}

void requireComputeEntryAbi(const nr::rhi::SlangProgram &program, std::array<SlangUInt, 3u> expectedThreadGroupSize,
                            std::string_view shaderLabel)
{
    nr::test::require(program.valid(), std::format("{} shader should compile", shaderLabel));
    nr::test::require(program.entryPoint() != nullptr,
                      std::format("{} shader should expose one entry point", shaderLabel));
    nr::test::require(program.entryPoint()->stage == SLANG_STAGE_COMPUTE,
                      std::format("{} entry point should remain compute", shaderLabel));

    auto *entryPointLayout = program.programLayout()->getEntryPointByIndex(0u);
    nr::test::require(entryPointLayout != nullptr,
                      std::format("{} entry-point reflection should resolve", shaderLabel));
    auto reflectedThreadGroupSize = std::array<SlangUInt, 3u>{};
    entryPointLayout->getComputeThreadGroupSize(reflectedThreadGroupSize.size(), reflectedThreadGroupSize.data());
    nr::test::require(reflectedThreadGroupSize == expectedThreadGroupSize,
                      std::format("{} thread-group ABI should match", shaderLabel));
}

[[nodiscard]] nr::rhi::ShaderCursor requireDescriptorBindingAbi(
    const nr::rhi::ShaderCursor &root, const DescriptorBindingExpectation &expected, std::string_view shaderLabel)
{
    auto cursor = root[expected.name];
    nr::test::require(cursor.valid(),
                      std::format("{} descriptor '{}' should resolve", shaderLabel, expected.name));
    nr::test::require(cursor.descriptorSemantic() == expected.semantic,
                      std::format("{} descriptor '{}' semantic should match", shaderLabel, expected.name));
    auto binding = cursor.descriptorBinding();
    nr::test::require(binding.has_value(),
                      std::format("{} descriptor '{}' binding should reflect", shaderLabel, expected.name));
    nr::test::requireEqual(binding->set, expected.set,
                           std::format("{} descriptor '{}' set should match", shaderLabel, expected.name));
    nr::test::requireEqual(binding->binding, expected.binding,
                           std::format("{} descriptor '{}' binding should match", shaderLabel, expected.name));
    nr::test::require(binding->descriptorType == expected.descriptorType,
                      std::format("{} descriptor '{}' type should match", shaderLabel, expected.name));
    nr::test::require(binding->stageFlags == vk::ShaderStageFlags{vk::ShaderStageFlagBits::eAll},
                      std::format("{} descriptor '{}' should retain all-stage visibility", shaderLabel,
                                  expected.name));
    return cursor;
}

[[nodiscard]] nr::rhi::ShaderCursor requirePushConstantAbi(
    const nr::rhi::ShaderCursor &root, std::string_view pushConstantName, std::size_t expectedSize,
    std::span<const std::pair<std::string_view, std::size_t>> expectedFields, std::string_view shaderLabel)
{
    auto cursor = root[pushConstantName];
    nr::test::require(cursor.valid(),
                      std::format("{} push constants '{}' should resolve", shaderLabel, pushConstantName));
    nr::test::require(cursor.bindingKind() == nr::rhi::ShaderBindingKind::PushConstant,
                      std::format("{} '{}' should remain a push constant", shaderLabel, pushConstantName));
    auto range = cursor.pushConstantRange();
    nr::test::require(range.has_value(),
                      std::format("{} push-constant range should reflect", shaderLabel));
    nr::test::requireEqual(range->offset, 0u,
                           std::format("{} push constants should start at byte zero", shaderLabel));
    nr::test::requireEqual(range->size, expectedSize,
                           std::format("{} push-constant size should match", shaderLabel));
    nr::test::require(range->stageFlags == vk::ShaderStageFlags{vk::ShaderStageFlagBits::eAll},
                      std::format("{} push constants should retain all-stage visibility", shaderLabel));
    std::ranges::for_each(expectedFields, [&](const auto &expected) {
        auto fieldCursor = cursor[expected.first];
        nr::test::require(fieldCursor.valid(),
                          std::format("{} push field '{}' should resolve", shaderLabel, expected.first));
        nr::test::requireEqual(fieldCursor.address().uniformOffset, expected.second,
                               std::format("{} push field '{}' offset should match", shaderLabel, expected.first));
    });
    return cursor;
}

const nr::test::CaseRegistrar shaderBackendWorkerCountCase{
    "rhi shader backend defaults to the device worker limit", [] {
        nr::test::requireEqual(nr::rhi::ShaderServiceConfig{}.backendWorkerCount,
                               nr::threading::resolveWorkerCount(0, nr::maxThreads));
    }};

const nr::test::CaseRegistrar rowMajorMatrixLayoutCase{
    "rhi Slang default options select row-major matrices", [] {
        auto const &compilerOptions = nr::rhi::kDefaultSlangCompileOptions.compilerOptions;
        nr::test::require(
            std::ranges::any_of(compilerOptions, [](const nr::rhi::SlangCompilerOption &option) {
                return option.name == slang::CompilerOptionName::MatrixLayoutRow && option.intValue0 == 1;
            }),
            "the default Slang compiler options must force row-major matrix layout");
        nr::test::requireEqual(
            nr::rhi::computeCompileOptionsHashValue(nr::rhi::kDefaultSlangCompileOptions),
            nr::rhi::kDefaultSlangCompileOptions.hashValue,
            "the row-major Slang option must participate in the persistent shader cache hash");
    }};

const nr::test::CaseRegistrar normalBufferRowVectorNormalCase{
    "rhi normalBuffer row-vector normal transform preserves tangent orthogonality and handedness", [] {
        auto const linear = DirectX::XMMatrixMultiply(
            DirectX::XMMatrixScaling(-2.0f, 3.0f, 4.0f),
            DirectX::XMMatrixRotationRollPitchYaw(0.0f, 0.0f, nr::math::radians(45.0f)));
        auto const sourceNormal = DirectX::XMFLOAT3{0.0f, 1.0f, 0.0f};
        auto const sourceTangent = DirectX::XMFLOAT3{1.0f, 0.0f, 0.0f};
        auto const transformedTangent = normalized(transformDirection(sourceTangent, linear));

        auto const inverseLinear = DirectX::XMMatrixInverse(nullptr, linear);
        auto const normalTransform = DirectX::XMMatrixTranspose(inverseLinear);
        auto const transformedNormal = normalized(transformDirection(sourceNormal, normalTransform));
        auto const incorrectlyTransformedNormal = normalized(transformDirection(sourceNormal, inverseLinear));

        nr::test::require(std::abs(dot(transformedNormal, transformedTangent)) <= 1.0e-5f,
                          "n * transpose(inverse(L)) must remain orthogonal to t * L");
        nr::test::require(std::abs(dot(incorrectlyTransformedNormal, transformedTangent)) >= 1.0e-2f,
                          "the row-vector normal golden must reject an untransposed inverse");

        auto const determinant = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(linear));
        auto const handedness = determinant < 0.0f ? -1.0f : 1.0f;
        auto const sourceTangentSign = -1.0f;
        nr::test::requireEqual(handedness, -1.0f,
                               "the affine reference transform must retain its mirrored scale determinant");
        nr::test::requireEqual(sourceTangentSign * handedness, 1.0f,
                               "normalBuffer tangent sign must flip for mirrored transforms");
    }};

const nr::test::CaseRegistrar shaderBatchCompileCase{
    "rhi shader batch compiler preserves single-entry request order", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
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
        nr::test::require(std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
                          "every single-entry batch request should compile");
        auto const expectedEntryPoints = std::array{
            std::pair{"main", SLANG_STAGE_COMPUTE},
            std::pair{"vertexMain", SLANG_STAGE_VERTEX},
            std::pair{"fragmentMain", SLANG_STAGE_FRAGMENT},
            std::pair{"vertexMain", SLANG_STAGE_VERTEX},
        };
        std::ranges::for_each(std::views::iota(std::size_t{0}, programs.size()), [&](std::size_t index) {
            auto const &program = programs[index];
            auto const &expected = expectedEntryPoints[index];
            auto const *entryPoint = program.entryPoint();
            nr::test::require(entryPoint != nullptr);
            nr::test::requireEqual(entryPoint->entryPointName, std::string{expected.first});
            nr::test::require(entryPoint->stage == expected.second);
        });
        nr::test::requireEqual(stats.requestCount, requests.size());
        nr::test::require(stats.memoryHitCount >= 1u,
                          "an identical request in one batch should reuse the prepared program and fan out its result");
    }};

const nr::test::CaseRegistrar splitGraphicsReflectionRootCase{
    "rhi split graphics entrypoints preserve the canonical reflection-root ABI", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
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
        nr::test::require(std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
                          "every split graphics entrypoint should compile");

        auto layouts = programs | std::views::transform([](const nr::rhi::SlangProgram &program) {
                           return nr::rhi::ShaderDescriptorLayout::create(program);
                       }) |
                       std::ranges::to<std::vector>();
        nr::test::require(std::ranges::all_of(layouts, &nr::rhi::ShaderDescriptorLayout::valid),
                          "every split graphics entrypoint should expose a valid descriptor layout");

        auto const pipelinePairs = std::array{
            std::pair{std::string_view{"appUi"}, std::size_t{0}},
            std::pair{std::string_view{"embeddedTriangle"}, std::size_t{2}},
            std::pair{std::string_view{"normalBuffer"}, std::size_t{4}},
        };
        std::ranges::for_each(pipelinePairs, [&](const auto &pipelinePair) {
            auto const &[pipelineName, vertexIndex] = pipelinePair;
            auto const &vertexLayout = layouts[vertexIndex];
            auto const &fragmentLayout = layouts[vertexIndex + 1u];
            auto const difference =
                nr::rhi::describeShaderLayoutAbiDifference(vertexLayout.abiSignature(), fragmentLayout.abiSignature());
            nr::test::require(
                nr::rhi::shaderLayoutAbiEquivalent(vertexLayout.abiSignature(), fragmentLayout.abiSignature()),
                std::format("split {} vertex/fragment programs must expose the same descriptor/push ABI: {}",
                            pipelineName, difference));
        });

        auto requireRootFields = [&](std::size_t programIndex, std::span<const std::string_view> fieldNames) {
            auto root = layouts[programIndex].rootCursor();
            std::ranges::for_each(fieldNames, [&](std::string_view fieldName) {
                nr::test::require(root.hasField(fieldName),
                                  std::format("canonical reflection root '{}' must expose global field '{}'",
                                              requests[programIndex].sourcePath.generic_string(), fieldName));
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
    "rhi shader service reuses persistent SPIR-V after session reload", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();
        auto const request = nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/sceneLightReflection"},
        };

        {
            auto initialProgram = shaderService.compileProgramByFile(request);
            nr::test::require(initialProgram.valid(), "the initial single-entry shader compile should produce SPIR-V");
        }

        shaderService.reloadSession();
        auto restoredProgram = shaderService.compileProgramByFile(request);
        auto const stats = shaderService.lastCompileBatchStats();
        nr::test::require(restoredProgram.valid(),
                          "the shader should remain valid when restored from persistent SPIR-V");
        nr::test::requireEqual(stats.requestCount, std::size_t{1});
        nr::test::require(stats.persistentHitCount >= 1u,
                          "a fresh Slang session should reuse the persistent SPIR-V artifact");
        nr::test::requireEqual(stats.backendCompilationCount, std::size_t{0},
                               "a persistent cache hit must skip Slang backend code generation");
    }};

const nr::test::CaseRegistrar globalFrameUniformCase{
    "rhi shader cursor exposes semantic descriptor-set bindings", [] {
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
            std::ranges::any_of(descriptorSets,
                                [](const nr::rhi::DescriptorSetLayoutInfo &setInfo) { return setInfo.set == 3u; }),
            "normalBuffer descriptor layout should expose set 3 for buffers");
        nr::test::require(
            std::ranges::any_of(descriptorSets,
                                [](const nr::rhi::DescriptorSetLayoutInfo &setInfo) { return setInfo.set == 1u; }),
            "normalBuffer descriptor layout should expose set 1 for scene textures");

        auto root = layout.rootCursor();
        auto pushConstantsCursor = root["gPushConstants"];
        nr::test::require(pushConstantsCursor.valid(), "gPushConstants cursor should resolve");
        auto normalBufferPushRange = pushConstantsCursor.pushConstantRange();
        nr::test::require(normalBufferPushRange.has_value(), "gPushConstants should have push constant reflection");
        nr::test::requireEqual(normalBufferPushRange->size, 96u);
        auto const expectedPushFieldOffsets = std::array{
            std::pair{"model", std::size_t{0u}},
            std::pair{"normalUvLinear", std::size_t{48u}},
            std::pair{"normalUvOffsetScale", std::size_t{64u}},
            std::pair{"normalTextureMeta", std::size_t{80u}},
        };
        std::ranges::for_each(expectedPushFieldOffsets, [&](auto const &expected) {
            auto fieldCursor = pushConstantsCursor[expected.first];
            nr::test::require(fieldCursor.valid(),
                              std::format("normalBuffer push field '{}' should resolve", expected.first));
            nr::test::requireEqual(fieldCursor.address().uniformOffset, expected.second,
                                   std::format("normalBuffer push field '{}' offset should match C++", expected.first));
        });
        auto modelCursor = pushConstantsCursor["model"];
        auto *modelLayout = modelCursor.typeLayout();
        nr::test::require(modelLayout != nullptr, "normalBuffer model matrix should expose type reflection");
        nr::test::require(modelLayout->getMatrixLayoutMode() == SLANG_MATRIX_LAYOUT_ROW_MAJOR,
                          "normalBuffer model matrix must be row-major");
        nr::test::requireEqual(modelLayout->getRowCount(), 4u,
                               "normalBuffer model matrix must preserve four affine rows");
        nr::test::requireEqual(modelLayout->getColumnCount(), 3u,
                               "normalBuffer model matrix must upload three affine columns");
        auto const modelSize =
            static_cast<std::size_t>(modelLayout->getSize(slang::ParameterCategory::Uniform));
        nr::test::requireEqual(modelSize, std::size_t{48u},
                               "normalBuffer model matrix must occupy 12 floats");
        nr::test::requireEqual(modelSize / static_cast<std::size_t>(modelLayout->getRowCount()),
                               std::size_t{12u},
                               "normalBuffer row-major matrix must keep a tightly packed 12-byte row stride");
        nr::test::requireEqual(static_cast<std::size_t>(modelLayout->getStride(slang::ParameterCategory::Uniform)),
                               std::size_t{48u}, "normalBuffer matrix value stride must remain 48 bytes");

        auto sceneTexturesCursor = root["gSceneTextures"];
        nr::test::require(sceneTexturesCursor.valid(), "gSceneTextures cursor should resolve");
        nr::test::require(sceneTexturesCursor.referencesRuntimeDescriptorArray(),
                          "gSceneTextures should be runtime-sized");
        nr::test::require(sceneTexturesCursor.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::requireEqual(*sceneTexturesCursor.bindingDescriptorCount(), 1024u);

        auto sceneTextureBinding = sceneTexturesCursor.descriptorBinding();
        nr::test::require(sceneTextureBinding.has_value(), "gSceneTextures should have descriptor binding reflection");
        nr::test::requireEqual(sceneTextureBinding->set, 1u);
        nr::test::requireEqual(sceneTextureBinding->binding, 2u);
        nr::test::require(sceneTextureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler);
        nr::test::require(sceneTextureBinding->supportsVariableDescriptorCount());
        nr::test::require(sceneTextureBinding->isPartiallyBound());
        nr::test::require((sceneTextureBinding->bindingFlags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) ==
                              vk::DescriptorBindingFlags{},
                          "prepare-before-record runtime bindings should not request update-after-bind");

        root.beginRecording();
        auto sceneTextureElement = sceneTexturesCursor[0u];
        nr::test::require(sceneTextureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 11,
            .debugName = "Renderer.SceneTexture[0]",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        auto sceneTextureSnapshot = root.takeSnapshot();
        nr::test::requireEqual(sceneTextureSnapshot.descriptorWriteCount(), std::size_t{1});
        auto const &sceneTextureWrite = sceneTextureSnapshot.descriptorWrites().front();
        nr::test::requireEqual(sceneTextureWrite.binding.set, 1u);
        nr::test::requireEqual(sceneTextureWrite.binding.binding, 2u);
        nr::test::requireEqual(sceneTextureWrite.arrayElement, 0u);
        nr::test::require(sceneTextureWrite.binding.descriptorType == vk::DescriptorType::eCombinedImageSampler);
        root.beginRecording();
        auto frameCursor = root["gFrame"];
        nr::test::require(frameCursor.valid(), "gFrame cursor should resolve");
        nr::test::require(frameCursor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);

        auto binding = frameCursor.descriptorBinding();
        nr::test::require(binding.has_value(), "gFrame should have descriptor binding reflection");
        nr::test::requireEqual(binding->set, 3u);
        nr::test::requireEqual(binding->binding, 0u);
        nr::test::require(binding->descriptorType == vk::DescriptorType::eUniformBuffer);

        nr::test::require(frameCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 7,
            .debugName = "Renderer.GlobalFrameUniforms",
            .offset = 256,
            .range = 288,
        }));

        auto snapshot = root.takeSnapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{1});
        auto const &write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.binding.set, 3u);
        nr::test::requireEqual(write.binding.binding, 0u);
        nr::test::require(std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(write.payload));
        auto const &logical = std::get<nr::rhi::LogicalResourceDescriptorWrite>(write.payload);
        nr::test::requireEqual(logical.logicalResourceId, std::uint64_t{7});
        nr::test::requireEqual(logical.offset, vk::DeviceSize{256});
        nr::test::requireEqual(logical.range, vk::DeviceSize{288});
    }};

const nr::test::CaseRegistrar accumulateShaderAbiCase{
    "rhi accumulate shader preserves descriptor push-constant and thread-group ABI", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/accumulate"},
        });
        requireComputeEntryAbi(program, std::array<SlangUInt, 3u>{16u, 16u, 1u}, "accumulate");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "accumulate descriptor layout should be valid");
        auto root = layout.rootCursor();
        auto const expectedBindings = std::array{
            DescriptorBindingExpectation{
                .name = "gCurrentColor",
                .semantic = nr::rhi::ShaderDescriptorSemantic::SampledImage,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .set = 1u,
                .binding = 0u,
            },
            DescriptorBindingExpectation{
                .name = "gHistoryColor",
                .semantic = nr::rhi::ShaderDescriptorSemantic::SampledImage,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .set = 1u,
                .binding = 1u,
            },
            DescriptorBindingExpectation{
                .name = "gAccumulatedColor",
                .semantic = nr::rhi::ShaderDescriptorSemantic::StorageImage,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .set = 2u,
                .binding = 0u,
            },
        };
        auto descriptors = expectedBindings | std::views::transform([&](const auto &expected) {
                               return requireDescriptorBindingAbi(root, expected, "accumulate");
                           }) |
                           std::ranges::to<std::vector>();

        auto const expectedPushFields = std::array{
            std::pair{std::string_view{"width"}, std::size_t{0u}},
            std::pair{std::string_view{"height"}, std::size_t{4u}},
            std::pair{std::string_view{"resetHistory"}, std::size_t{8u}},
            std::pair{std::string_view{"historySampleCount"}, std::size_t{12u}},
            std::pair{std::string_view{"maxHistorySampleCount"}, std::size_t{16u}},
        };
        auto pushConstants = requirePushConstantAbi(root, "gAccumulate", sizeof(AccumulatePushConstants),
                                                    expectedPushFields, "accumulate");

        root.beginRecording();
        nr::test::require(descriptors[0u].setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 70u,
            .debugName = "Accumulate.CurrentColor",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        nr::test::require(descriptors[1u].setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 71u,
            .debugName = "Accumulate.HistoryColor",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        nr::test::require(descriptors[2u].setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 72u,
            .debugName = "Accumulate.AccumulatedColor",
            .imageLayout = vk::ImageLayout::eGeneral,
        }));
        nr::test::require(pushConstants.setData(AccumulatePushConstants{
            .width = 1920u,
            .height = 1080u,
            .resetHistory = 1u,
            .historySampleCount = 4u,
            .maxHistorySampleCount = 64u,
        }));

        auto snapshot = root.takeSnapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{3u});
        nr::test::requireEqual(snapshot.pushConstantWriteCount(), std::size_t{1u});
    }};

const nr::test::CaseRegistrar dlssMotionVectorDebugShaderAbiCase{
    "rhi DLSS motion-vector debug shader preserves descriptor push-constant and thread-group ABI", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/dlssRayReconstructionDebug"},
        });
        requireComputeEntryAbi(program, std::array<SlangUInt, 3u>{8u, 8u, 1u}, "DLSS motion-vector debug");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "DLSS motion-vector debug descriptor layout should be valid");
        auto root = layout.rootCursor();
        auto const expectedBindings = std::array{
            DescriptorBindingExpectation{
                .name = "gMotionVectors",
                .semantic = nr::rhi::ShaderDescriptorSemantic::SampledImage,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .set = 1u,
                .binding = 0u,
            },
            DescriptorBindingExpectation{
                .name = "gMotionVectorVisualization",
                .semantic = nr::rhi::ShaderDescriptorSemantic::StorageImage,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .set = 2u,
                .binding = 0u,
            },
        };
        auto descriptors = expectedBindings | std::views::transform([&](const auto &expected) {
                               return requireDescriptorBindingAbi(root, expected, "DLSS motion-vector debug");
                           }) |
                           std::ranges::to<std::vector>();

        auto const expectedPushFields = std::array{
            std::pair{std::string_view{"outputBase"}, std::size_t{0u}},
            std::pair{std::string_view{"outputSize"}, std::size_t{8u}},
            std::pair{std::string_view{"motionVectorBase"}, std::size_t{16u}},
            std::pair{std::string_view{"motionVectorSize"}, std::size_t{24u}},
            std::pair{std::string_view{"motionVectorScale"}, std::size_t{32u}},
        };
        auto pushConstants = requirePushConstantAbi(root, "gMotionVectorDebug",
                                                    sizeof(DlssMotionVectorDebugPushConstants), expectedPushFields,
                                                    "DLSS motion-vector debug");

        root.beginRecording();
        nr::test::require(descriptors[0u].setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 80u,
            .debugName = "DLSS.RR.MotionVectors",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        nr::test::require(descriptors[1u].setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 81u,
            .debugName = "DLSS.RR.MotionVectorVisualization",
            .imageLayout = vk::ImageLayout::eGeneral,
        }));
        nr::test::require(pushConstants.setData(DlssMotionVectorDebugPushConstants{
            .outputBase = {8u, 16u},
            .outputSize = {1920u, 1080u},
            .motionVectorBase = {4u, 8u},
            .motionVectorSize = {960u, 540u},
            .motionVectorScale = {1.0f, 1.0f},
        }));

        auto snapshot = root.takeSnapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{2u});
        nr::test::requireEqual(snapshot.pushConstantWriteCount(), std::size_t{1u});
    }};

const nr::test::CaseRegistrar shaderServiceReloadCase{
    "rhi shader service reload rebuilds session and preserves shader compilation", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
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
        nr::test::require(generationAfterReload > generationBeforeReload,
                          "session generation should increase after reload");

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/appUi/fragment"},
        });
        nr::test::require(program.valid(), "appUi shader program should compile after session reload");
    }};

const nr::test::CaseRegistrar sceneLightReflectionCase{
    "rhi shader cursor exposes scene light set5 bindings", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
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
        nr::test::require(sceneLightAliasTable.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::StorageBuffer);

        auto headerBinding = lightHeader.descriptorBinding();
        auto lightsBinding = sceneLights.descriptorBinding();
        auto aliasBinding = sceneLightAliasTable.descriptorBinding();
        nr::test::require(headerBinding.has_value(), "gSceneLightHeader should expose descriptor binding reflection");
        nr::test::require(lightsBinding.has_value(), "gSceneLights should expose descriptor binding reflection");
        nr::test::require(aliasBinding.has_value(),
                          "gSceneLightAliasTable should expose descriptor binding reflection");
        nr::test::requireEqual(headerBinding->set, 5u);
        nr::test::requireEqual(headerBinding->binding, 0u);
        nr::test::requireEqual(lightsBinding->set, 5u);
        nr::test::requireEqual(lightsBinding->binding, 1u);
        nr::test::requireEqual(aliasBinding->set, 5u);
        nr::test::requireEqual(aliasBinding->binding, 2u);

        root.beginRecording();
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

        auto snapshot = root.takeSnapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{3});
        auto const &headerWrite = snapshot.descriptorWrites()[0];
        auto const &lightsWrite = snapshot.descriptorWrites()[1];
        auto const &aliasWrite = snapshot.descriptorWrites()[2];
        nr::test::requireEqual(headerWrite.binding.set, 5u);
        nr::test::requireEqual(headerWrite.binding.binding, 0u);
        nr::test::requireEqual(lightsWrite.binding.set, 5u);
        nr::test::requireEqual(lightsWrite.binding.binding, 1u);
        nr::test::requireEqual(aliasWrite.binding.set, 5u);
        nr::test::requireEqual(aliasWrite.binding.binding, 2u);
    }};

const nr::test::CaseRegistrar appUiCursorCase{
    "rhi shader cursor captures runtime descriptor array and push constants", [] {
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
        nr::test::require(!root.hasField("gSceneTextures"),
                          "appUi shader must not inherit the scene texture table from common");
        auto texturesCursor = root["gUiTextures"];
        nr::test::require(texturesCursor.valid(), "runtime texture cursor should resolve");
        nr::test::require(texturesCursor.referencesRuntimeDescriptorArray(), "gUiTextures should be runtime-sized");
        nr::test::require(texturesCursor.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::requireEqual(*texturesCursor.bindingDescriptorCount(), 16u);
        auto textureBinding = texturesCursor.descriptorBinding();
        nr::test::require(textureBinding.has_value(), "gUiTextures should expose descriptor binding reflection");
        nr::test::requireEqual(textureBinding->set, 1u);
        nr::test::requireEqual(textureBinding->binding, 0u);
        nr::test::requireEqual(textureBinding->descriptorCount, 16u,
                               "The test's runtime descriptor policy should resolve gUiTextures to 16 descriptors");
        nr::test::require(textureBinding->isRuntimeSized, "gUiTextures should remain a runtime descriptor array");
        nr::test::require(textureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler);
        nr::test::require(textureBinding->supportsVariableDescriptorCount());
        nr::test::require(textureBinding->isPartiallyBound());
        nr::test::require(textureBinding->stageFlags ==
                              vk::ShaderStageFlags{vk::ShaderStageFlagBits::eAll},
                          "gUiTextures should retain the canonical all-stage reflection policy");

        root.beginRecording();
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
        auto pushRange = pushCursor.pushConstantRange();
        nr::test::require(pushRange.has_value(), "gUiPush should expose push constant range reflection");
        nr::test::requireEqual(pushRange->offset, 0u);
        nr::test::requireEqual(pushRange->size, 32u);
        nr::test::require(pushRange->stageFlags == vk::ShaderStageFlags{vk::ShaderStageFlagBits::eAll},
                          "gUiPush should retain the canonical all-stage reflection policy");
        auto const expectedPushFieldOffsets = std::array{
            std::pair{"scale", std::size_t{0u}},
            std::pair{"translate", std::size_t{8u}},
            std::pair{"textureIndex", std::size_t{16u}},
            std::pair{"padding", std::size_t{20u}},
        };
        std::ranges::for_each(expectedPushFieldOffsets, [&](auto const &expected) {
            auto fieldCursor = pushCursor[expected.first];
            nr::test::require(fieldCursor.valid(),
                              std::format("appUi push field '{}' should resolve", expected.first));
            nr::test::requireEqual(fieldCursor.address().uniformOffset, expected.second,
                                   std::format("appUi push field '{}' offset should match C++", expected.first));
        });
        nr::test::require(pushCursor.setData(UiPushConstants{
            .scale = DirectX::XMFLOAT2{2.0f, 2.0f},
            .translate = DirectX::XMFLOAT2{-1.0f, -1.0f},
            .textureIndex = 5,
        }));

        auto snapshot = root.takeSnapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{1});
        nr::test::requireEqual(snapshot.pushConstantWriteCount(), std::size_t{1});
        auto const &write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.arrayElement, 5u);
        nr::test::require(write.binding.descriptorType == vk::DescriptorType::eCombinedImageSampler);
        nr::test::require(std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(write.payload));
        nr::test::requireEqual(std::get<nr::rhi::LogicalResourceDescriptorWrite>(write.payload).logicalResourceId,
                               std::uint64_t{42});

        auto resolvedWrites = nr::rhi::resolveDescriptorWriteRequests(
            snapshot,
            [](const nr::rhi::LogicalResourceDescriptorWrite &logicalResource, const nr::rhi::DescriptorBindingInfo &,
               std::uint32_t) -> std::optional<nr::rhi::DescriptorWritePayload> {
                nr::test::requireEqual(logicalResource.logicalResourceId, std::uint64_t{42});
                return nr::rhi::DescriptorWritePayload{nr::rhi::ImageDescriptorWrite{
                    .imageView = vk::ImageView{fakeVkHandle<VkImageView>(0x2001u)},
                    .imageLayout = logicalResource.imageLayout,
                }};
            });
        nr::test::requireEqual(resolvedWrites.size(), std::size_t{1});

        root.beginRecording();
        nr::test::require(textureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 43,
            .debugName = "ui-test-texture-replaced",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        nr::test::require(textureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 44,
            .debugName = "ui-test-texture-final",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        auto nextSnapshot = root.takeSnapshot();
        nr::test::requireEqual(nextSnapshot.descriptorWriteCount(), std::size_t{1});
        nr::test::requireEqual(nextSnapshot.pushConstantWriteCount(), std::size_t{0});
        nr::test::requireEqual(std::get<nr::rhi::LogicalResourceDescriptorWrite>(
                                   nextSnapshot.descriptorWrites().front().payload)
                                   .logicalResourceId,
                               std::uint64_t{44});
        nr::test::requireEqual(snapshot.pushConstantWriteCount(), std::size_t{1},
                               "a consumed snapshot must remain immutable after the next recording epoch");
        nr::test::requireEqual(std::get<nr::rhi::LogicalResourceDescriptorWrite>(
                                   snapshot.descriptorWrites().front().payload)
                                   .logicalResourceId,
                               std::uint64_t{42});

    }};
} // namespace
