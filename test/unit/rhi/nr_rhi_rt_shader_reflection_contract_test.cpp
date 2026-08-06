#include <cstddef>

import std;
import dependency.math;
import dependency.slang;
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

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingRaygenVariant(std::uint32_t maxSurfaceBounces = 16u,
                                                                            bool enableRussianRoulette = true)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign("kMaxSurfaceBounces", "uint", maxSurfaceBounces)
        .assign("RussianRoulettePolicy", "IRussianRoulettePolicy",
                std::string{enableRussianRoulette ? "RussianRouletteEnabledPolicy" : "RussianRouletteDisabledPolicy"});
    return variant;
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc makePathTracingClosestHitVariant(std::uint32_t layerMask = 0u,
                                                                                bool enableFilterAfterShading = false)
{
    auto variant = nr::rhi::SlangProgramVariantDesc{};
    variant.assign("CHS", "ICHS", std::format("MaterialCHS<RtMaterialLayerFlag({}u)>", layerMask))
        .assign("kEnableFilterAfterShading", "bool", enableFilterAfterShading);
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

inline constexpr std::uint16_t kSpirvOpTypeInt = 21u;
inline constexpr std::uint16_t kSpirvOpTypeStruct = 30u;
inline constexpr std::uint16_t kSpirvOpTypePointer = 32u;
inline constexpr std::uint16_t kSpirvOpVariable = 59u;
inline constexpr std::uint16_t kSpirvOpTraceRayKhr = 4445u;
inline constexpr std::uint16_t kSpirvOpHitObjectTraceRayNv = 5260u;
inline constexpr std::uint16_t kSpirvOpHitObjectExecuteShaderNv = 5264u;
inline constexpr std::uint16_t kSpirvOpHitObjectTraceRayExt = 5316u;
inline constexpr std::uint16_t kSpirvOpHitObjectExecuteShaderExt = 5319u;
inline constexpr std::uint32_t kSpirvStorageClassRayPayloadKhr = 5338u;
inline constexpr std::uint32_t kSpirvStorageClassIncomingRayPayloadKhr = 5342u;

struct SpirvIntegerType
{
    std::uint32_t width = 0u;
    bool signedness = false;
};

struct SpirvPointerType
{
    std::uint32_t storageClass = 0u;
    std::uint32_t pointeeType = 0u;
};

struct SpirvPayloadModule
{
    std::map<std::uint32_t, SpirvIntegerType> integerTypes{};
    std::map<std::uint32_t, std::vector<std::uint32_t>> structTypes{};
    std::map<std::uint32_t, SpirvPointerType> pointerTypes{};
    std::map<std::uint32_t, std::uint32_t> variablePointerTypes{};
    std::vector<std::uint32_t> traceRayPayloads{};
    std::vector<std::uint32_t> hitObjectTracePayloads{};
    std::vector<std::uint32_t> hitObjectInvokePayloads{};
    std::vector<std::uint32_t> incomingPayloadVariables{};
};

[[nodiscard]] SpirvPayloadModule inspectSpirvPayloadModule(std::span<const std::uint32_t> words)
{
    nr::test::require(words.size() >= 5u && words.front() == 0x0723'0203u,
                      "payload ABI inspection requires a valid SPIR-V module");

    auto result = SpirvPayloadModule{};
    auto instructionOffset = std::size_t{5u};
    while (instructionOffset < words.size())
    {
        auto const instructionHeader = words[instructionOffset];
        auto const wordCount = static_cast<std::size_t>(instructionHeader >> 16u);
        auto const opcode = static_cast<std::uint16_t>(instructionHeader & 0xffffu);
        nr::test::require(wordCount > 0u && instructionOffset + wordCount <= words.size(),
                          "SPIR-V payload inspection encountered a malformed instruction");
        auto const instruction = words.subspan(instructionOffset, wordCount);

        if (opcode == kSpirvOpTypeInt)
        {
            nr::test::requireEqual(wordCount, std::size_t{4u});
            result.integerTypes.emplace(instruction[1], SpirvIntegerType{
                                                            .width = instruction[2],
                                                            .signedness = instruction[3] != 0u,
                                                        });
        }
        else if (opcode == kSpirvOpTypeStruct)
        {
            nr::test::require(wordCount >= 2u, "OpTypeStruct must expose a result id");
            result.structTypes.emplace(instruction[1],
                                       instruction.subspan(2u) | std::ranges::to<std::vector>());
        }
        else if (opcode == kSpirvOpTypePointer)
        {
            nr::test::requireEqual(wordCount, std::size_t{4u});
            result.pointerTypes.emplace(instruction[1], SpirvPointerType{
                                                            .storageClass = instruction[2],
                                                            .pointeeType = instruction[3],
                                                        });
        }
        else if (opcode == kSpirvOpVariable)
        {
            nr::test::require(wordCount >= 4u, "OpVariable must expose type, result, and storage class");
            result.variablePointerTypes.emplace(instruction[2], instruction[1]);
            if (instruction[3] == kSpirvStorageClassIncomingRayPayloadKhr)
            {
                result.incomingPayloadVariables.push_back(instruction[2]);
            }
        }
        else if (opcode == kSpirvOpTraceRayKhr)
        {
            result.traceRayPayloads.push_back(instruction.back());
        }
        else if (opcode == kSpirvOpHitObjectTraceRayNv || opcode == kSpirvOpHitObjectTraceRayExt)
        {
            result.hitObjectTracePayloads.push_back(instruction.back());
        }
        else if (opcode == kSpirvOpHitObjectExecuteShaderNv || opcode == kSpirvOpHitObjectExecuteShaderExt)
        {
            result.hitObjectInvokePayloads.push_back(instruction.back());
        }

        instructionOffset += wordCount;
    }
    return result;
}

[[nodiscard]] const SpirvPointerType &requireSpirvPayloadVariable(const SpirvPayloadModule &module,
                                                                  std::uint32_t variableId,
                                                                  std::uint32_t storageClass)
{
    auto const variable = module.variablePointerTypes.find(variableId);
    nr::test::require(variable != module.variablePointerTypes.end(),
                      "ray tracing instruction payload operand must be an OpVariable id");
    auto const pointer = module.pointerTypes.find(variable->second);
    nr::test::require(pointer != module.pointerTypes.end(),
                      "ray tracing payload variable must use an OpTypePointer result type");
    nr::test::requireEqual(pointer->second.storageClass, storageClass,
                           "ray tracing payload pointer must use the expected SPIR-V storage class");
    return pointer->second;
}

[[nodiscard]] std::string spirvPayloadTypeShape(const SpirvPayloadModule &module, std::uint32_t typeId)
{
    if (auto integer = module.integerTypes.find(typeId); integer != module.integerTypes.end())
    {
        return std::format("{}{}", integer->second.signedness ? "i" : "u", integer->second.width);
    }
    if (auto structure = module.structTypes.find(typeId); structure != module.structTypes.end())
    {
        auto members = structure->second |
                       std::views::transform([&](std::uint32_t memberType) {
                           return spirvPayloadTypeShape(module, memberType);
                       }) |
                       std::ranges::to<std::vector>();
        return std::format("struct({})", std::views::join_with(members, std::string_view{","}) |
                                                 std::ranges::to<std::string>());
    }
    return std::format("type#{}", typeId);
}

[[nodiscard]] bool isSingleUint32PayloadType(const SpirvPayloadModule &module, std::uint32_t typeId)
{
    if (auto integer = module.integerTypes.find(typeId); integer != module.integerTypes.end())
    {
        return integer->second.width == 32u && !integer->second.signedness;
    }
    if (auto structure = module.structTypes.find(typeId); structure != module.structTypes.end())
    {
        return structure->second.size() == 1u && isSingleUint32PayloadType(module, structure->second.front());
    }
    return false;
}

const nr::test::CaseRegistrar variantAssignmentSourceTextCase{
    "rhi shader variants generate deterministic assignment source", [] {
        auto constants = nr::rhi::SlangProgramVariantDesc{};
        constants.assign("kUIntValue", "uint", 7u)
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
        alias.assign("Policy", "IPolicy", std::string{"ConcretePolicy<1u, SomeEnum.value>"});

        auto aliasLines = effectiveShaderLines(alias.sourceText());
        nr::test::requireEqual(aliasLines.size(), std::size_t{2u});
        nr::test::requireEqual(aliasLines[0], std::string{"import common;"});
        nr::test::requireEqual(aliasLines[1],
                               std::string{"export struct Policy : IPolicy = ConcretePolicy<1u, SomeEnum.value>;"});

        auto constantsReordered = nr::rhi::SlangProgramVariantDesc{};
        constantsReordered.assign("kIntValue", "int", std::int32_t{-3})
            .assign("kUIntValue", "uint", 7u)
            .assign("kBoolValue", "bool", true)
            .assign("kFloatValue", "float", 1.5f);

        nr::test::requireEqual(constantsReordered.hashValue(), constants.hashValue());
        nr::test::requireEqual(constantsReordered.sourceText(), constants.sourceText());
    }};

const nr::test::CaseRegistrar pathTracingChsLinkTimeTypeCase{
    "rhi shader service compiles path tracing CHS link-time generic type aliases", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto chsVariant = makePathTracingClosestHitVariant();
        auto chsSource = chsVariant.sourceText();
        auto effectiveLines = effectiveShaderLines(chsSource);
        nr::test::requireEqual(effectiveLines.size(), std::size_t{3u});
        nr::test::requireEqual(effectiveLines[0], std::string{"import common;"});
        nr::test::requireEqual(effectiveLines[1],
                               std::string{"export struct CHS : ICHS = MaterialCHS<RtMaterialLayerFlag(0u)>;"});
        nr::test::requireEqual(effectiveLines[2],
                               std::string{"export static const bool kEnableFilterAfterShading = false;"});

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
            .variant = chsVariant,
        });
        nr::test::require(program.valid(),
                          "path tracing closest-hit shader should compile with an entry-local CHS type");
    }};

const nr::test::CaseRegistrar pathTracingTransmissionChsFamiliesCase{
    "rhi shader service links separate base-only and transmission CHS families", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto baseOnlyVariant = makePathTracingClosestHitVariant(1u);
        auto transmissionVariant = makePathTracingClosestHitVariant(9u);
        nr::test::require(baseOnlyVariant.hashValue() != transmissionVariant.hashValue(),
                          "base-only and transmission CHS aliases must remain distinct entry variants");

        auto const layerMasks = std::array{
            1u, 9u, 3u, 5u, 7u, 11u, 13u, 15u, 17u,
        };
        auto requests = layerMasks | std::views::transform([](std::uint32_t layerMask) {
                            return nr::rhi::SlangProgramCompileFileRequest{
                                .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
                                .variant = makePathTracingClosestHitVariant(layerMask),
                            };
                        }) |
                        std::ranges::to<std::vector>();
        requests.push_back(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/materialAnisotropyContract"},
        });
        auto programs = shaderService.compileProgramsByFile(requests);
        nr::test::requireEqual(programs.size(), requests.size());

        auto const &baseOnlyProgram = programs[0];
        auto const &transmissionProgram = programs[1];
        nr::test::require(baseOnlyProgram.valid(), "base-only CHS family should compile");
        nr::test::require(transmissionProgram.valid(), "transmission-enabled CHS family should compile");

        nr::test::require(baseOnlyProgram.entryPoint()->spirv->size() < transmissionProgram.entryPoint()->spirv->size(),
                          "link specialization should prune transmission record and BTDF code from the base-only CHS");

        std::ranges::for_each(std::views::iota(std::size_t{2}, std::size_t{8}), [&](std::size_t index) {
            nr::test::require(programs[index].valid(),
                              std::format("path tracing layer-mask {} CHS should compile", layerMasks[index]));
        });

        auto const &anisotropicProgram = programs[8];
        nr::test::require(anisotropicProgram.valid(),
                          "path tracing anisotropic base-only closest-hit shader should compile");

        auto const &matrixProgram = programs[9];
        nr::test::require(
            matrixProgram.valid(),
            "lightweight anisotropy contract should instantiate both base variants for all eight lit masks");

        auto matrixLayout = nr::rhi::ShaderDescriptorLayout::create(matrixProgram);
        nr::test::require(matrixLayout.valid(), "anisotropy contract layout should be valid");
        auto materialHeader = matrixLayout.rootCursor()["gMaterialHeader"];
        auto anisotropyMember = materialHeader["anisotropy"];
        nr::test::require(anisotropyMember.valid(), "RtMaterialHeader anisotropy member should reflect");
        nr::test::requireEqual(anisotropyMember.address().uniformOffset, std::size_t{96u},
                               "RtMaterialHeader anisotropy member offset must match C++");
        auto *headerLayout = materialHeader.typeLayout()->getElementTypeLayout();
        nr::test::require(headerLayout != nullptr, "RtMaterialHeader element layout should reflect");
        nr::test::requireEqual(static_cast<std::size_t>(headerLayout->getSize(slang::ParameterCategory::Uniform)),
                               std::size_t{112u}, "RtMaterialHeader reflected size must match C++");
        nr::test::requireEqual(static_cast<std::size_t>(headerLayout->getStride(slang::ParameterCategory::Uniform)),
                               std::size_t{112u}, "RtMaterialHeader reflected stride must match C++");

        auto isotropicLayout = nr::rhi::ShaderDescriptorLayout::create(baseOnlyProgram);
        auto anisotropicLayout = nr::rhi::ShaderDescriptorLayout::create(anisotropicProgram);
        nr::test::require(isotropicLayout.valid() && anisotropicLayout.valid(),
                          "isotropic and anisotropic closest-hit layouts should be valid");
        nr::test::require(
            nr::rhi::shaderLayoutAbiEquivalent(isotropicLayout.abiSignature(), anisotropicLayout.abiSignature()),
            "isotropic and anisotropic CHS variants must preserve descriptor and push ABI");
    }};

const nr::test::CaseRegistrar rtPipelineStageSelectionCase{
    "rhi rt stage selections can give specialized chMain stages distinct logical names", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto opaqueProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
            .variant = makePathTracingClosestHitVariant(),
        });
        nr::test::require(opaqueProgram.valid(), "opaque CHS path tracing shader should compile");

        auto alphaMaskProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
            .variant = makePathTracingClosestHitVariant(),
        });
        nr::test::require(alphaMaskProgram.valid(), "reused alpha-mask CHS path tracing shader should compile");

        auto selectedStages = std::array{
            nr::rhi::RayTracingPipelineStageSelection{
                .program = std::cref(opaqueProgram),
                .logicalEntryPointName = "ch_opaque",
            },
            nr::rhi::RayTracingPipelineStageSelection{
                .program = std::cref(alphaMaskProgram),
                .logicalEntryPointName = "ch_alphaMask",
            },
        };

        nr::test::require(selectedStages[0].program.get().entryPoint() != nullptr);
        nr::test::require(selectedStages[1].program.get().entryPoint() != nullptr);
        nr::test::require(selectedStages[0].logicalEntryPointName != selectedStages[1].logicalEntryPointName);

        auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{
            .stages = selectedStages | std::ranges::to<std::vector>(),
            .groups =
                {
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
        nr::test::require(!nr::rhi::validateRayTracingProgramAssemblyDesc(assembly).has_value(),
                          "named RT program assembly should accept distinct logical CHS stages");

        auto duplicateGroupAssembly = assembly;
        duplicateGroupAssembly.groups[1].name = duplicateGroupAssembly.groups[0].name;
        nr::test::require(nr::rhi::validateRayTracingProgramAssemblyDesc(duplicateGroupAssembly).has_value(),
                          "RT program assembly should reject duplicate group names");

        auto unknownEntryAssembly = assembly;
        unknownEntryAssembly.groups[0].closestHitEntryPoint = "ch_missing";
        nr::test::require(nr::rhi::validateRayTracingProgramAssemblyDesc(unknownEntryAssembly).has_value(),
                          "RT program assembly should reject unknown logical entry points");
    }};

const nr::test::CaseRegistrar rtShaderReflectionCase{
    "rhi rt shader reflection exposes AS image and camera bindings", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/minimalRtTriangle/raygen"},
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
                         std::views::transform(
                             [](const nr::rhi::ShaderBindingRecord &record) { return record.binding.semantic(); }) |
                         std::ranges::to<std::vector>();
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::AccelerationStructure));
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::StorageImage));
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::UniformBuffer));
    }};

const nr::test::CaseRegistrar rtObjectShaderReflectionCase{
    "rtobject present and path tracing shaders compile and expose expected bindings", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto presentProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/presentConvert"},
        });
        nr::test::require(presentProgram.valid(), "rtobject present conversion shader should compile");
        nr::test::require(presentProgram.entryPoint()->stage == SLANG_STAGE_COMPUTE,
                          "present conversion entry point should remain compute");

        auto *presentEntryPointLayout = presentProgram.programLayout()->getEntryPointByIndex(0u);
        nr::test::require(presentEntryPointLayout != nullptr,
                          "present conversion entry-point reflection should resolve");
        auto reflectedThreadGroupSize = std::array<SlangUInt, 3>{};
        presentEntryPointLayout->getComputeThreadGroupSize(reflectedThreadGroupSize.size(),
                                                           reflectedThreadGroupSize.data());
        nr::test::require(reflectedThreadGroupSize == std::array<SlangUInt, 3>{16u, 16u, 1u},
                          "present conversion thread-group ABI should remain 16x16x1");

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
        auto presentPushConstantRange = presentPushConstants.pushConstantRange();
        nr::test::require(presentPushConstantRange.has_value(),
                          "present conversion push-constant range should reflect");
        nr::test::requireEqual(presentPushConstantRange->offset, 0u,
                               "present conversion push constants should start at byte zero");
        nr::test::requireEqual(presentPushConstantRange->size, 24u,
                               "present conversion push constants should occupy exactly 24 bytes");
        nr::test::require(presentPushConstantRange->stageFlags == vk::ShaderStageFlagBits::eAll,
                          "the canonical RHI layout should preserve its established all-stage visibility");

        auto presentWidth = presentPushConstants["width"];
        auto presentHeight = presentPushConstants["height"];
        auto presentSwizzleBgr = presentPushConstants["swizzleBgr"];
        auto presentOutputEncoding = presentPushConstants["outputEncoding"];
        auto presentToneMapping = presentPushConstants["toneMapping"];
        auto presentUiOpacity = presentPushConstants["uiOpacity"];
        nr::test::require(presentWidth.valid() && presentHeight.valid() && presentSwizzleBgr.valid() &&
                              presentOutputEncoding.valid() && presentToneMapping.valid() && presentUiOpacity.valid(),
                          "present conversion should reflect every CPU-authored push-constant field");
        nr::test::requireEqual(presentWidth.address().uniformOffset, std::size_t{0u});
        nr::test::requireEqual(presentHeight.address().uniformOffset, std::size_t{4u});
        nr::test::requireEqual(presentSwizzleBgr.address().uniformOffset, std::size_t{8u});
        nr::test::requireEqual(presentOutputEncoding.address().uniformOffset, std::size_t{12u});
        nr::test::requireEqual(presentToneMapping.address().uniformOffset, std::size_t{16u});
        nr::test::requireEqual(presentUiOpacity.address().uniformOffset, std::size_t{20u});

        auto requireDescriptorBinding = [](const nr::rhi::ShaderCursor &cursor, std::string_view symbol,
                                           std::uint32_t expectedSet, std::uint32_t expectedBinding) {
            auto binding = cursor.descriptorBinding();
            nr::test::require(binding.has_value(),
                              std::format("{} should expose descriptor binding reflection", symbol));
            nr::test::requireEqual(binding->set, expectedSet,
                                   std::format("{} descriptor set should match the semantic ABI", symbol));
            nr::test::requireEqual(binding->binding, expectedBinding,
                                   std::format("{} descriptor binding should match the pass ABI", symbol));
        };
        requireDescriptorBinding(sourceColor, "gSourceColor", 1u, 0u);
        requireDescriptorBinding(uiColor, "gUiColor", 1u, 1u);
        requireDescriptorBinding(convertedColor, "gConvertedColor", 2u, 0u);

        auto rtProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/raygen"},
            .variant = makePathTracingRaygenVariant(),
        });
        nr::test::require(rtProgram.valid(), "rtobject path tracing shader should compile");

        auto rtLayout = nr::rhi::ShaderDescriptorLayout::create(rtProgram);
        nr::test::require(rtLayout.valid(), "rtobject path tracing layout should be valid");

        auto rtRoot = rtLayout.rootCursor();
        auto scene = rtRoot["scene"];
        auto outputImage = rtRoot["outputImage"];
        auto depthImage = rtRoot["depthImage"];
        auto diffuseAlbedoImage = rtRoot["diffuseAlbedoImage"];
        auto specularAlbedoImage = rtRoot["specularAlbedoImage"];
        auto normalRoughnessImage = rtRoot["normalRoughnessImage"];
        auto motionVectorsImage = rtRoot["motionVectorsImage"];
        auto specularHitDistanceImage = rtRoot["specularHitDistanceImage"];
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
        auto frameView = frameUniform["view"];
        auto frameProjection = frameUniform["projection"];
        auto frameViewProjection = frameUniform["viewProjection"];
        auto frameInverseViewProjection = frameUniform["inverseViewProjection"];
        auto frameUnjitteredViewProjection = frameUniform["unjitteredViewProjection"];
        auto framePreviousViewProjection = frameUniform["previousViewProjection"];
        auto frameCameraWorld = frameUniform["cameraWorld"];
        auto frameState = frameUniform["frameState"];

        nr::test::require(scene.valid(), "path tracing TLAS cursor should resolve");
        nr::test::require(outputImage.valid(), "path tracing output cursor should resolve");
        nr::test::require(depthImage.valid(), "path tracing depth output cursor should resolve");
        nr::test::require(diffuseAlbedoImage.valid(), "path tracing diffuse albedo output cursor should resolve");
        nr::test::require(specularAlbedoImage.valid(), "path tracing specular albedo output cursor should resolve");
        nr::test::require(normalRoughnessImage.valid(), "path tracing normal-roughness output cursor should resolve");
        nr::test::require(motionVectorsImage.valid(), "path tracing motion-vectors output cursor should resolve");
        nr::test::require(specularHitDistanceImage.valid(),
                          "path tracing specular hit-distance output cursor should resolve");
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
        nr::test::require(frameView.valid() && frameProjection.valid() && frameViewProjection.valid() &&
                              frameInverseViewProjection.valid() && frameUnjitteredViewProjection.valid() &&
                              framePreviousViewProjection.valid() && frameCameraWorld.valid() && frameState.valid(),
                          "path tracing should reflect the complete global frame uniform ABI");
        nr::test::require(!frameUniform.hasField("previousView"),
                          "the shader-dead previousView field must stay removed from the frame ABI");
        nr::test::requireEqual(frameView.address().uniformOffset, std::size_t{0u});
        nr::test::requireEqual(frameProjection.address().uniformOffset, std::size_t{64u});
        nr::test::requireEqual(frameViewProjection.address().uniformOffset, std::size_t{128u});
        nr::test::requireEqual(frameInverseViewProjection.address().uniformOffset, std::size_t{192u});
        nr::test::requireEqual(frameUnjitteredViewProjection.address().uniformOffset, std::size_t{256u});
        nr::test::requireEqual(framePreviousViewProjection.address().uniformOffset, std::size_t{320u});
        nr::test::requireEqual(frameCameraWorld.address().uniformOffset, std::size_t{384u});
        nr::test::requireEqual(frameState.address().uniformOffset, std::size_t{400u});
        auto *frameUniformElementLayout = frameUniform.typeLayout()->getElementTypeLayout();
        nr::test::require(frameUniformElementLayout != nullptr,
                          "global frame constant buffer should expose its element layout");
        nr::test::requireEqual(static_cast<std::size_t>(frameUniformElementLayout->getSize()), std::size_t{416u},
                               "GlobalFrameUniforms must preserve its 416-byte reflected ABI");
        nr::test::require(scene.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(outputImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(depthImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(diffuseAlbedoImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(specularAlbedoImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(normalRoughnessImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(motionVectorsImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(specularHitDistanceImage.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(frameUniform.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(instanceMetadata.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(geometryMetadata.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(materialHeaders.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(materialLayers.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(materialTextureRefs.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(vertexData.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(indexData.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(sceneTextures.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::require(sceneLightHeader.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(sceneLights.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(sceneLightAliasTable.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(environmentMap.descriptorSemantic() ==
                              nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler,
                          "path tracing environment map should use a combined image sampler");
        auto environmentPushRange = environmentParameters.pushConstantRange();
        nr::test::require(environmentPushRange.has_value(),
                          "path tracing environment parameters should use push constants");
        nr::test::requireEqual(environmentPushRange->size, 16u);
        nr::test::require(environmentMap.makeImmutableSamplerBinding(nr::rhi::SlangSamplerDesc{}).has_value(),
                          "path tracing environment map should support an immutable sampler");

        requireDescriptorBinding(environmentMap, "gEnvironmentMap", 1u, 0u);
        requireDescriptorBinding(sceneTextures, "gSceneTextures", 1u, 2u);
        requireDescriptorBinding(outputImage, "outputImage", 2u, 0u);
        requireDescriptorBinding(depthImage, "depthImage", 2u, 1u);
        requireDescriptorBinding(diffuseAlbedoImage, "diffuseAlbedoImage", 2u, 2u);
        requireDescriptorBinding(specularAlbedoImage, "specularAlbedoImage", 2u, 3u);
        requireDescriptorBinding(normalRoughnessImage, "normalRoughnessImage", 2u, 4u);
        requireDescriptorBinding(motionVectorsImage, "motionVectorsImage", 2u, 5u);
        requireDescriptorBinding(specularHitDistanceImage, "specularHitDistanceImage", 2u, 6u);
        requireDescriptorBinding(frameUniform, "gFrame", 3u, 0u);
        requireDescriptorBinding(instanceMetadata, "rtInstanceMetadata", 3u, 1u);
        requireDescriptorBinding(geometryMetadata, "rtGeometryMetadata", 3u, 2u);
        requireDescriptorBinding(materialHeaders, "rtMaterialHeaders", 3u, 3u);
        requireDescriptorBinding(materialLayers, "rtMaterialLayers", 3u, 4u);
        requireDescriptorBinding(materialTextureRefs, "rtMaterialTextureRefs", 3u, 5u);
        requireDescriptorBinding(vertexData, "rtVertexData", 3u, 6u);
        requireDescriptorBinding(indexData, "rtIndexData", 3u, 7u);
        requireDescriptorBinding(scene, "scene", 4u, 0u);
        requireDescriptorBinding(sceneLightHeader, "gSceneLightHeader", 5u, 0u);
        requireDescriptorBinding(sceneLights, "gSceneLights", 5u, 1u);
        requireDescriptorBinding(sceneLightAliasTable, "gSceneLightAliasTable", 5u, 2u);
    }};

const nr::test::CaseRegistrar pathTracingLinkTimeVariantCase{
    "path tracing link-time variants compile and keep shader layout ABI", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto const requests = std::array{
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/raygen"},
                .variant = makePathTracingRaygenVariant(),
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/raygen"},
                .variant = makePathTracingRaygenVariant(2u),
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/raygen"},
                .variant = makePathTracingRaygenVariant(16u, false),
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/miss"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/anyHit"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/shadowMiss"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/shadowAnyHit"},
            },
            nr::rhi::SlangProgramCompileFileRequest{
                .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
                .variant = makePathTracingClosestHitVariant(),
            },
        };
        auto programs = shaderService.compileProgramsByFile(requests);
        nr::test::requireEqual(programs.size(), requests.size());
        nr::test::require(std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
                          "every path tracing raygen variant should compile in one backend batch");

        auto const &baselineProgram = programs[0];
        auto const &constantProgram = programs[1];
        auto const &typeProgram = programs[2];
        nr::test::require(requests[3].variant.empty() && requests[4].variant.empty() &&
                              requests[5].variant.empty() && requests[6].variant.empty(),
                          "path tracing material/shadow miss and any-hit entries must remain fixed empty-variant "
                          "programs");
        nr::test::require(programs[3].entryPoint()->stage == SLANG_STAGE_MISS,
                          "path tracing material miss should compile without unrelated assignments");
        nr::test::require(programs[4].entryPoint()->stage == SLANG_STAGE_ANY_HIT,
                          "path tracing material any-hit should compile without unrelated assignments");
        nr::test::require(programs[5].entryPoint()->stage == SLANG_STAGE_MISS,
                          "path tracing shadow miss should compile without unrelated assignments");
        nr::test::require(programs[6].entryPoint()->stage == SLANG_STAGE_ANY_HIT,
                          "path tracing shadow any-hit should compile without unrelated assignments");
        nr::test::require(
            programs[7].entryPoint()->stage == SLANG_STAGE_CLOSEST_HIT,
            "path tracing closest-hit should compile beside fixed miss/any-hit entries in the same batch");

        auto requirePayloadLayout = [](const nr::rhi::SlangProgram &program, std::string_view typeName,
                                        std::size_t expectedSize) {
            auto *programLayout = program.programLayout();
            nr::test::require(programLayout != nullptr, "path tracing payload contract should expose reflection");
            auto *payloadType = programLayout->findTypeByName(std::string{typeName}.c_str());
            auto *payloadLayout = payloadType != nullptr ? programLayout->getTypeLayout(payloadType) : nullptr;
            nr::test::require(payloadLayout != nullptr,
                              std::format("path tracing stage should reflect {}", typeName));
            nr::test::requireEqual(static_cast<std::size_t>(payloadLayout->getSize()), expectedSize,
                                   std::format("{} reflected size should remain stable", typeName));
            nr::test::requireEqual(static_cast<std::size_t>(payloadLayout->getStride()), expectedSize,
                                   std::format("{} reflected stride should remain stable", typeName));
        };
        auto requireEntryPayloadParameter = [](const nr::rhi::SlangProgram &program,
                                               std::string_view expectedTypeName) {
            auto *programLayout = program.programLayout();
            auto *entryPoint = programLayout != nullptr ? programLayout->getEntryPointByIndex(0u) : nullptr;
            nr::test::require(entryPoint != nullptr, "path tracing payload contract should reflect an entry point");
            auto payloadParameters = std::views::iota(0u, entryPoint->getParameterCount()) |
                                     std::views::transform([&](unsigned parameterIndex) {
                                         return entryPoint->getParameterByIndex(parameterIndex);
                                     }) |
                                     std::views::filter([](slang::VariableLayoutReflection *parameter) {
                                         return parameter != nullptr && parameter->getName() != nullptr &&
                                                std::string_view{parameter->getName()} == "payload";
                                     }) |
                                     std::ranges::to<std::vector>();
            nr::test::requireEqual(payloadParameters.size(), std::size_t{1u},
                                   "ray tracing entry should reflect exactly one payload parameter");
            auto *payloadType = payloadParameters.front()->getVariable()->getType();
            nr::test::require(payloadType != nullptr && payloadType->getName() != nullptr,
                              "ray tracing payload parameter should expose its declared type");
            nr::test::requireEqual(std::string_view{payloadType->getName()}, expectedTypeName,
                                   "ray tracing payload parameter should retain its declared type name");
            nr::test::require(payloadType->getKind() == slang::TypeReflection::Kind::Struct,
                              "ray tracing payload parameter should remain a struct");
            nr::test::requireEqual(payloadType->getFieldCount(), 1u,
                                   "compact shadow payload should contain exactly one field");
            auto *payloadField = payloadType->getFieldByIndex(0u);
            nr::test::require(payloadField != nullptr,
                              "compact shadow payload should reflect its uint32 field");
            auto *fieldType = payloadField->getType();
            nr::test::require(fieldType != nullptr && fieldType->getKind() == slang::TypeReflection::Kind::Scalar &&
                                  fieldType->getScalarType() == slang::TypeReflection::ScalarType::UInt32,
                              "compact shadow payload field should be uint32");
        };
        requirePayloadLayout(baselineProgram, "MaterialRayPayload", 128u);
        requirePayloadLayout(programs[5], "ShadowRayPayload", 4u);
        requirePayloadLayout(programs[6], "ShadowRayPayload", 4u);
        requireEntryPayloadParameter(programs[5], "ShadowRayPayload");
        requireEntryPayloadParameter(programs[6], "ShadowRayPayload");

        auto const raygenSpirv = inspectSpirvPayloadModule(*baselineProgram.entryPoint()->spirv);
        nr::test::require(!raygenSpirv.traceRayPayloads.empty(),
                          "path tracing raygen SPIR-V should retain direct shadow TraceRay instructions");
        nr::test::require(std::ranges::all_of(raygenSpirv.traceRayPayloads, [&](std::uint32_t payloadVariable) {
                              auto const &pointer = requireSpirvPayloadVariable(
                                  raygenSpirv, payloadVariable, kSpirvStorageClassRayPayloadKhr);
                              return isSingleUint32PayloadType(raygenSpirv, pointer.pointeeType);
                          }),
                          "every direct shadow TraceRay payload pointer must point to one uint32 lane");
        nr::test::require(!raygenSpirv.hitObjectTracePayloads.empty() &&
                              !raygenSpirv.hitObjectInvokePayloads.empty(),
                          "material traversal should retain typed HitObject trace and invoke instructions");
        auto const materialTraceShapes =
            raygenSpirv.hitObjectTracePayloads |
            std::views::transform([&](std::uint32_t payloadVariable) {
                auto const &pointer = requireSpirvPayloadVariable(raygenSpirv, payloadVariable,
                                                                  kSpirvStorageClassRayPayloadKhr);
                nr::test::require(!isSingleUint32PayloadType(raygenSpirv, pointer.pointeeType),
                                  "material HitObject trace must not use the compact shadow payload");
                return spirvPayloadTypeShape(raygenSpirv, pointer.pointeeType);
            }) |
            std::ranges::to<std::set>();
        auto const materialInvokeShapes =
            raygenSpirv.hitObjectInvokePayloads |
            std::views::transform([&](std::uint32_t payloadVariable) {
                auto const &pointer = requireSpirvPayloadVariable(raygenSpirv, payloadVariable,
                                                                  kSpirvStorageClassRayPayloadKhr);
                return spirvPayloadTypeShape(raygenSpirv, pointer.pointeeType);
            }) |
            std::ranges::to<std::set>();
        nr::test::requireEqual(materialInvokeShapes, materialTraceShapes,
                               "typed HitObject trace and invoke must use the same material payload pointee");

        auto const shadowMissSpirv = inspectSpirvPayloadModule(*programs[5].entryPoint()->spirv);
        auto const shadowAnyHitSpirv = inspectSpirvPayloadModule(*programs[6].entryPoint()->spirv);
        nr::test::requireEqual(shadowMissSpirv.incomingPayloadVariables.size(), std::size_t{1u});
        nr::test::require(shadowAnyHitSpirv.incomingPayloadVariables.size() <= 1u,
                          "Slang may eliminate an unused any-hit incoming payload variable");
        auto const &shadowMissPointer = requireSpirvPayloadVariable(
            shadowMissSpirv, shadowMissSpirv.incomingPayloadVariables.front(),
            kSpirvStorageClassIncomingRayPayloadKhr);
        nr::test::require(isSingleUint32PayloadType(shadowMissSpirv, shadowMissPointer.pointeeType),
                          "shadow miss incoming payload must contain one uint32 lane");
        if (!shadowAnyHitSpirv.incomingPayloadVariables.empty())
        {
            auto const &shadowAnyHitPointer = requireSpirvPayloadVariable(
                shadowAnyHitSpirv, shadowAnyHitSpirv.incomingPayloadVariables.front(),
                kSpirvStorageClassIncomingRayPayloadKhr);
            nr::test::require(isSingleUint32PayloadType(shadowAnyHitSpirv, shadowAnyHitPointer.pointeeType),
                              "retained shadow any-hit incoming payload must contain one uint32 lane");
            nr::test::requireEqual(spirvPayloadTypeShape(shadowMissSpirv, shadowMissPointer.pointeeType),
                                   spirvPayloadTypeShape(shadowAnyHitSpirv, shadowAnyHitPointer.pointeeType),
                                   "retained shadow miss and any-hit incoming payload pointee types must match");
        }

        auto descriptorPolicy = nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 1024u,
        };
        auto baselineLayout = nr::rhi::ShaderDescriptorLayout::create(baselineProgram, descriptorPolicy);
        auto constantLayout = nr::rhi::ShaderDescriptorLayout::create(constantProgram, descriptorPolicy);
        auto typeLayout = nr::rhi::ShaderDescriptorLayout::create(typeProgram, descriptorPolicy);
        nr::test::require(baselineLayout.valid(), "path tracing baseline layout should be valid");
        nr::test::require(constantLayout.valid(), "path tracing constant variant layout should be valid");
        nr::test::require(typeLayout.valid(), "path tracing type variant layout should be valid");
        auto const baselineSignature = baselineLayout.abiSignature();
        auto requireRootCoverage = [&](const nr::rhi::SlangProgram &requiredProgram) {
            auto requiredLayout = nr::rhi::ShaderDescriptorLayout::create(requiredProgram, descriptorPolicy);
            nr::test::require(requiredLayout.valid(), "path tracing required-stage layout should be valid");

            auto const requiredSignature = requiredLayout.abiSignature();
            auto const requiredName = std::string_view{requiredProgram.entryPoint()->debugName};
            std::ranges::for_each(
                requiredSignature.descriptorBindings, [&](const nr::rhi::ShaderDescriptorAbiBinding &requiredBinding) {
                    auto ownerBinding = std::ranges::find_if(baselineSignature.descriptorBindings,
                                                             [&](const nr::rhi::ShaderDescriptorAbiBinding &candidate) {
                                                                 return candidate.set == requiredBinding.set &&
                                                                        candidate.binding == requiredBinding.binding;
                                                             });
                    nr::test::require(
                        ownerBinding != baselineSignature.descriptorBindings.end(),
                        std::format("raygen reflection root should expose set={}, binding={} required by '{}'",
                                    requiredBinding.set, requiredBinding.binding, requiredName));
                    nr::test::require(ownerBinding->descriptorCount == requiredBinding.descriptorCount &&
                                          ownerBinding->isRuntimeSized == requiredBinding.isRuntimeSized &&
                                          ownerBinding->descriptorType == requiredBinding.descriptorType &&
                                          ownerBinding->bindingFlags == requiredBinding.bindingFlags &&
                                          (ownerBinding->stageFlags & requiredBinding.stageFlags) ==
                                              requiredBinding.stageFlags,
                                      std::format("raygen reflection root should expose a compatible descriptor at "
                                                  "set={}, binding={} required by '{}'",
                                                  requiredBinding.set, requiredBinding.binding, requiredName));
                });

            std::ranges::for_each(
                requiredSignature.pushConstantRanges, [&](const nr::rhi::ShaderPushConstantAbiRange &requiredRange) {
                    auto const requiredEnd = static_cast<std::uint64_t>(requiredRange.offset) + requiredRange.size;
                    auto ownerRange = std::ranges::find_if(
                        baselineSignature.pushConstantRanges,
                        [&](const nr::rhi::ShaderPushConstantAbiRange &candidate) {
                            auto const candidateEnd = static_cast<std::uint64_t>(candidate.offset) + candidate.size;
                            return candidate.offset <= requiredRange.offset && candidateEnd >= requiredEnd &&
                                   (candidate.stageFlags & requiredRange.stageFlags) == requiredRange.stageFlags;
                        });
                    nr::test::require(
                        ownerRange != baselineSignature.pushConstantRanges.end(),
                        std::format("raygen reflection root should cover push-constant bytes [{}, {}) required by '{}'",
                                    requiredRange.offset, requiredEnd, requiredName));
                });
        };
        requireRootCoverage(programs[3]);
        requireRootCoverage(programs[4]);
        requireRootCoverage(programs[5]);
        requireRootCoverage(programs[6]);
        requireRootCoverage(programs[7]);
        nr::test::require(nr::rhi::shaderLayoutAbiEquivalent(baselineSignature, constantLayout.abiSignature()),
                          "path tracing constant variant should keep descriptor/push ABI");
        nr::test::require(nr::rhi::shaderLayoutAbiEquivalent(baselineSignature, typeLayout.abiSignature()),
                          "path tracing type variant should keep descriptor/push ABI");

        auto presentProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/presentConvert"},
        });
        nr::test::require(presentProgram.valid(), "present shader should compile for ABI difference check");
        auto presentLayout = nr::rhi::ShaderDescriptorLayout::create(presentProgram, descriptorPolicy);
        nr::test::require(presentLayout.valid(), "present shader layout should be valid for ABI difference check");
        auto abiDiff = nr::rhi::describeShaderLayoutAbiDifference(baselineSignature, presentLayout.abiSignature());
        nr::test::require(!abiDiff.empty(), "ABI difference diagnostics should describe a mismatched layout");

        auto const generationBeforeReload = shaderService.sessionGeneration();
        shaderService.reloadSession();
        nr::test::require(shaderService.sessionGeneration() > generationBeforeReload,
                          "shader service reload should advance session generation for variant cache invalidation");

        auto reloadedVariantProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/pathTracing/closestHit"},
            .variant = makePathTracingClosestHitVariant(5u),
        });
        nr::test::require(reloadedVariantProgram.valid(), "path tracing variant should compile after session reload");
    }};

const nr::test::CaseRegistrar rtVertexAtlasLayoutCase{
    "path tracing shader vertex atlas offsets match resource vertex layout", [] {
        nr::test::requireEqual(offsetof(nr::resource::Vertex, position), std::size_t{0u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, normal), std::size_t{12u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, tangent), std::size_t{24u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, texCoord0), std::size_t{40u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, texCoord1), std::size_t{48u});
        nr::test::requireEqual(offsetof(nr::resource::Vertex, color0), std::size_t{56u});
    }};

const nr::test::CaseRegistrar rtMaterialTextureIdsReflectionCase{
    "rt shader common material texture id helper exposes scene texture table", [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/materialTextureIdsRt/raygen"},
        });
        nr::test::require(program.valid(), "material texture id RT shader should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program, nr::rhi::DescriptorBindingPolicy{
                                                                           .defaultRuntimeDescriptorCount = 1024,
                                                                       });
        nr::test::require(layout.valid(), "material texture id RT descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto sceneTextures = root["gSceneTextures"];
        nr::test::require(sceneTextures.valid(), "RT gSceneTextures cursor should resolve");
        nr::test::require(sceneTextures.referencesRuntimeDescriptorArray(),
                          "RT gSceneTextures should be runtime-sized");
        nr::test::require(sceneTextures.descriptorSemantic() ==
                          nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::requireEqual(*sceneTextures.bindingDescriptorCount(), 1024u);

        auto sceneTextureBinding = sceneTextures.descriptorBinding();
        nr::test::require(sceneTextureBinding.has_value(),
                          "RT gSceneTextures should expose descriptor binding reflection");
        nr::test::requireEqual(sceneTextureBinding->set, 1u);
        nr::test::requireEqual(sceneTextureBinding->binding, 2u);
        nr::test::require(sceneTextureBinding->descriptorType == vk::DescriptorType::eCombinedImageSampler);
        nr::test::require(sceneTextureBinding->supportsVariableDescriptorCount());

        auto sceneTextureImmutableSampler = sceneTextures.makeImmutableSamplerBinding(nr::rhi::SlangSamplerDesc{});
        nr::test::require(sceneTextureImmutableSampler.has_value(),
                          "RT gSceneTextures should allow immutable sampler binding");
        nr::test::requireEqual(sceneTextureImmutableSampler->set, 1u);
        nr::test::requireEqual(sceneTextureImmutableSampler->binding, 2u);
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
        auto const &write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.binding.set, 1u);
        nr::test::requireEqual(write.binding.binding, 2u);
        nr::test::requireEqual(write.arrayElement, 7u);
        nr::test::require(write.binding.descriptorType == vk::DescriptorType::eCombinedImageSampler);
    }};
} // namespace
