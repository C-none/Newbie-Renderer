#include <cstddef>

import std;
import dependency.slang;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
inline constexpr auto kLatentWidth = std::uint32_t{64u};
inline constexpr auto kLatentHeight = std::uint32_t{64u};
inline constexpr auto kLatentTexelCount = std::uint32_t{4096u};
inline constexpr auto kLatentChunkCount = std::size_t{8192u};
inline constexpr auto kParameterChunkCount = std::size_t{484u};
inline constexpr auto kFrameWeightScalarCountPerFrame = std::size_t{48u};
inline constexpr auto kContractBatchSize = std::uint32_t{32u};
inline constexpr auto kShaderBatchSize = std::uint32_t{64u};
inline constexpr auto kSampleTargetChunkCount = std::size_t{64u};
inline constexpr auto kGradientChunkCount = std::size_t{492u};
inline constexpr auto kTrainingStepCount = std::uint32_t{512u};
inline constexpr auto kTotalTrainingStepCount = std::uint32_t{32'768u};
inline constexpr auto kProductionLatentWarmupStepCount = std::uint32_t{2'048u};
// The GPU-AV smoke scales warmup and optimized latent coverage only. Production
// retains 2,048 warmup steps and all 4,096 texels / 64 optimizer workgroups.
inline constexpr auto kContractLatentWarmupStepCount = std::uint32_t{32u};
inline constexpr auto kContractOptimizedLatentTexelCount = std::uint32_t{512u};
inline constexpr auto kMollificationSampleCount = std::uint32_t{8u};
inline constexpr auto kMollificationStepCount = std::uint32_t{32u};
inline constexpr auto kMollificationInitialAngleRadians = 0.1745329252f;
inline constexpr auto kInitialLearningRate = 0.001f;
inline constexpr auto kFinalLearningRate = 0.0001f;
inline constexpr auto kLatentLearningRateScale = 0.1f;
inline constexpr auto kAdamBeta1 = 0.9f;
inline constexpr auto kAdamBeta2 = 0.999f;
inline constexpr auto kAdamEpsilon = 1.0e-7f;
inline constexpr auto kInitializeGroupCount = std::uint32_t{128u};
inline constexpr auto kOptimizeGroupCount = std::uint32_t{8u};
inline constexpr auto kQualitySampleCount = std::size_t{256u};
inline constexpr auto kQualityRecordFloatCount = std::size_t{8u};
inline constexpr auto kQualityGroupCount = std::uint32_t{4u};
inline constexpr auto kQualityHeldoutSeedBase = std::uint32_t{0x8000'0000u};
inline constexpr auto kGrazingZeroBaselineTolerance = 0.01f;
inline constexpr auto kViewerWidth = std::uint32_t{48u};
inline constexpr auto kViewerHeight = std::uint32_t{16u};
inline constexpr auto kTrainingControlMagic = std::uint32_t{0x4E41'5450u};
inline constexpr auto kSpirvHeaderWordCount = std::size_t{5u};
inline constexpr auto kSpirvOpImageSampleImplicitLod = std::uint16_t{87u};
inline constexpr auto kSpirvOpImageSampleExplicitLod = std::uint16_t{88u};
inline constexpr auto kSpirvOpImageSampleProjDrefExplicitLod = std::uint16_t{94u};
inline constexpr auto kSpirvOpImageSparseSampleImplicitLod = std::uint16_t{305u};
inline constexpr auto kSpirvOpImageSparseSampleProjDrefExplicitLod = std::uint16_t{312u};
inline constexpr auto kSpirvOpTypeFloat = std::uint16_t{22u};
inline constexpr auto kSpirvOpTypeVector = std::uint16_t{23u};
inline constexpr auto kSpirvOpTypeArray = std::uint16_t{28u};
inline constexpr auto kSpirvOpTypePointer = std::uint16_t{32u};
inline constexpr auto kSpirvOpConstant = std::uint16_t{43u};
inline constexpr auto kSpirvOpVariable = std::uint16_t{59u};
inline constexpr auto kSpirvFunctionStorageClass = std::uint32_t{7u};

inline constexpr auto kContractResultNames = std::array{
    std::string_view{"latent finite"},
    std::string_view{"all runtime parameter chunks finite"},
    std::string_view{"spatial prefix finite"},
    std::string_view{"frame 0 N/T/B finite and unit length"},
    std::string_view{"frame 0 B equals normalize(cross(N,T))"},
    std::string_view{"frame 1 N/T/B finite and unit length"},
    std::string_view{"frame 1 B equals normalize(cross(N,T))"},
    std::string_view{"runtime/differentiable four-path direction pair 0"},
    std::string_view{"runtime/differentiable four-path direction pair 1"},
    std::string_view{"runtime/differentiable four-path direction pair 2"},
    std::string_view{"runtime/differentiable four-path direction pair 3"},
    std::string_view{"32-value runtime/differentiable spatial prefix equivalence"},
    std::string_view{"two-frame runtime/differentiable equivalence"},
    std::string_view{"fixture shading normal tilted and finite"},
    std::string_view{"fixture anisotropy active with unit tangent"},
    std::string_view{"aggregate neural model contract"},
};

enum class NeuralProgram : std::size_t
{
    Initialize,
    Target,
    Gradient,
    Optimize,
    Pack,
    Viewer,
    ModelContract,
    AutodiffContract,
    Quality,
};

struct NeuralAppearanceGradientPushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = kContractBatchSize;
    std::uint32_t latentWidth = kLatentWidth;
    std::uint32_t latentHeight = kLatentHeight;
    std::uint32_t mollificationSampleCount = kMollificationSampleCount;
    std::uint32_t mollificationStepCount = kMollificationStepCount;
    float initialAngleRadians = kMollificationInitialAngleRadians;
    float padding = 0.0f;
};

struct NeuralAppearanceOptimizePushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = kContractBatchSize;
    std::uint32_t parameterChunkCount = static_cast<std::uint32_t>(kParameterChunkCount);
    std::uint32_t latentTexelCount = kContractOptimizedLatentTexelCount;
    std::uint32_t totalTrainingSteps = kTotalTrainingStepCount;
    std::uint32_t latentWarmupSteps = kContractLatentWarmupStepCount;
    float initialLearningRate = kInitialLearningRate;
    float finalLearningRate = kFinalLearningRate;
    float latentLearningRateScale = kLatentLearningRateScale;
    float beta1 = kAdamBeta1;
    float beta2 = kAdamBeta2;
    float epsilon = kAdamEpsilon;
};

struct NeuralAppearanceViewerPushConstants
{
    std::uint32_t width = kViewerWidth;
    std::uint32_t height = kViewerHeight;
    std::uint32_t frameIndex = kTrainingStepCount;
    std::uint32_t totalTrainingSteps = kTrainingStepCount;
    std::uint32_t comparisonEnabled = 1u;
    float errorGain = 4.0f;
};

enum class QualityStratum : std::uint32_t
{
    Uniform = 0u,
    Highlight = 1u,
    Grazing = 2u,
};

struct DistributionMetrics
{
    float mean = 0.0f;
    float percentile95 = 0.0f;
};

struct QualitySample
{
    float mappedMae = 0.0f;
    float safeLogL1 = 0.0f;
    float rawMae = 0.0f;
    float zeroMappedBaseline = 0.0f;
    float zeroLogBaseline = 0.0f;
    float maximumPhysicalCosine = 0.0f;
    float minimumPhysicalCosine = 0.0f;
    QualityStratum stratum = QualityStratum::Uniform;
};

struct QualityStratumMetrics
{
    std::size_t sampleCount = 0u;
    DistributionMetrics mapped{};
    DistributionMetrics safeLog{};
    float zeroMappedMean = 0.0f;
    float zeroLogMean = 0.0f;
};

struct QualityMetrics
{
    QualityStratumMetrics overall{};
    QualityStratumMetrics highlight{};
    QualityStratumMetrics grazing{};
};

static_assert(sizeof(NeuralAppearanceGradientPushConstants) == 32u);
static_assert(sizeof(NeuralAppearanceOptimizePushConstants) == 48u);
static_assert(sizeof(NeuralAppearanceViewerPushConstants) == 24u);
static_assert(kSampleTargetChunkCount == kShaderBatchSize);
static_assert(kQualityHeldoutSeedBase > kTrainingStepCount * kShaderBatchSize + kShaderBatchSize);
static_assert(kContractLatentWarmupStepCount < kProductionLatentWarmupStepCount);
static_assert(kContractOptimizedLatentTexelCount < kLatentTexelCount);
static_assert(kOptimizeGroupCount * 64u >= kParameterChunkCount &&
              kOptimizeGroupCount * 64u >= kContractOptimizedLatentTexelCount);

struct SpirvSampleInstructionCounts
{
    std::size_t total = 0u;
    std::size_t explicitLod = 0u;
};

struct ExpectedDescriptorBinding
{
    std::string_view name;
    std::uint32_t set = 0u;
    std::uint32_t binding = 0u;
    vk::DescriptorType type = vk::DescriptorType::eStorageBuffer;
};

struct SpirvVectorType
{
    std::uint32_t componentType = 0u;
    std::uint32_t componentCount = 0u;
};

struct SpirvArrayType
{
    std::uint32_t elementType = 0u;
    std::uint32_t lengthConstant = 0u;
};

struct SpirvPointerType
{
    std::uint32_t storageClass = 0u;
    std::uint32_t pointeeType = 0u;
};

struct PreparedComputeBindings
{
    nr::rhi::ShaderCursor root;
    std::vector<nr::rhi::ShaderBindingSet> sets;
    nr::rhi::DescriptorWriteCache writeCache;
};

[[nodiscard]] constexpr std::size_t programIndex(NeuralProgram program) noexcept
{
    return static_cast<std::size_t>(program);
}

[[nodiscard]] bool isImageSampleOpcode(std::uint16_t opcode) noexcept
{
    auto const isStandardSample =
        opcode >= kSpirvOpImageSampleImplicitLod && opcode <= kSpirvOpImageSampleProjDrefExplicitLod;
    auto const isSparseSample =
        opcode >= kSpirvOpImageSparseSampleImplicitLod && opcode <= kSpirvOpImageSparseSampleProjDrefExplicitLod;
    return isStandardSample || isSparseSample;
}

[[nodiscard]] SpirvSampleInstructionCounts inspectSpirvSampleInstructions(std::span<const std::uint32_t> words)
{
    nr::test::require(words.size() >= kSpirvHeaderWordCount && words.front() == 0x0723'0203u,
                      "neural appearance sampling inspection requires a valid SPIR-V module");

    auto result = SpirvSampleInstructionCounts{};
    auto instructionOffset = kSpirvHeaderWordCount;
    while (instructionOffset < words.size())
    {
        auto const instructionHeader = words[instructionOffset];
        auto const wordCount = static_cast<std::size_t>(instructionHeader >> 16u);
        auto const opcode = static_cast<std::uint16_t>(instructionHeader & 0xffffu);
        nr::test::require(wordCount > 0u && wordCount <= words.size() - instructionOffset,
                          "neural appearance sampling inspection encountered malformed SPIR-V");
        result.total += isImageSampleOpcode(opcode) ? 1u : 0u;
        result.explicitLod += opcode == kSpirvOpImageSampleExplicitLod ? 1u : 0u;
        instructionOffset += wordCount;
    }
    return result;
}

[[nodiscard]] bool declaresFunctionFloat4ArrayVariable(std::span<const std::uint32_t> words,
                                                       std::uint32_t expectedArrayLength)
{
    nr::test::require(words.size() >= kSpirvHeaderWordCount && words.front() == 0x0723'0203u,
                      "neural appearance local-array inspection requires valid SPIR-V");

    auto floatWidths = std::map<std::uint32_t, std::uint32_t>{};
    auto vectorTypes = std::map<std::uint32_t, SpirvVectorType>{};
    auto arrayTypes = std::map<std::uint32_t, SpirvArrayType>{};
    auto pointerTypes = std::map<std::uint32_t, SpirvPointerType>{};
    auto constants = std::map<std::uint32_t, std::uint32_t>{};
    auto functionVariablePointerTypes = std::vector<std::uint32_t>{};

    auto instructionOffset = kSpirvHeaderWordCount;
    while (instructionOffset < words.size())
    {
        auto const instructionHeader = words[instructionOffset];
        auto const wordCount = static_cast<std::size_t>(instructionHeader >> 16u);
        auto const opcode = static_cast<std::uint16_t>(instructionHeader & 0xffffu);
        nr::test::require(wordCount > 0u && wordCount <= words.size() - instructionOffset,
                          "neural appearance local-array inspection encountered malformed SPIR-V");
        auto const instruction = words.subspan(instructionOffset, wordCount);

        if (opcode == kSpirvOpTypeFloat && wordCount >= 3u)
        {
            floatWidths.insert_or_assign(instruction[1], instruction[2]);
        }
        else if (opcode == kSpirvOpTypeVector && wordCount >= 4u)
        {
            vectorTypes.insert_or_assign(
                instruction[1], SpirvVectorType{.componentType = instruction[2], .componentCount = instruction[3]});
        }
        else if (opcode == kSpirvOpTypeArray && wordCount >= 4u)
        {
            arrayTypes.insert_or_assign(
                instruction[1], SpirvArrayType{.elementType = instruction[2], .lengthConstant = instruction[3]});
        }
        else if (opcode == kSpirvOpTypePointer && wordCount >= 4u)
        {
            pointerTypes.insert_or_assign(
                instruction[1], SpirvPointerType{.storageClass = instruction[2], .pointeeType = instruction[3]});
        }
        else if (opcode == kSpirvOpConstant && wordCount >= 4u)
        {
            constants.insert_or_assign(instruction[2], instruction[3]);
        }
        else if (opcode == kSpirvOpVariable && wordCount >= 4u && instruction[3] == kSpirvFunctionStorageClass)
        {
            functionVariablePointerTypes.push_back(instruction[1]);
        }
        instructionOffset += wordCount;
    }

    return std::ranges::any_of(functionVariablePointerTypes, [&](std::uint32_t pointerTypeId) {
        auto const pointer = pointerTypes.find(pointerTypeId);
        if (pointer == pointerTypes.end() || pointer->second.storageClass != kSpirvFunctionStorageClass)
        {
            return false;
        }
        auto const array = arrayTypes.find(pointer->second.pointeeType);
        if (array == arrayTypes.end())
        {
            return false;
        }
        auto const length = constants.find(array->second.lengthConstant);
        if (length == constants.end() || length->second != expectedArrayLength)
        {
            return false;
        }
        auto const vector = vectorTypes.find(array->second.elementType);
        if (vector == vectorTypes.end() || vector->second.componentCount != 4u)
        {
            return false;
        }
        auto const scalar = floatWidths.find(vector->second.componentType);
        return scalar != floatWidths.end() && scalar->second == 32u;
    });
}

void requireLocalArrayInspectorPositiveControl()
{
    constexpr auto syntheticSpirv = std::array<std::uint32_t, 28u>{
        0x0723'0203u,
        0u,
        0u,
        0u,
        0u,
        (3u << 16u) | kSpirvOpTypeFloat,
        1u,
        32u,
        (4u << 16u) | kSpirvOpTypeVector,
        2u,
        1u,
        4u,
        (4u << 16u) | kSpirvOpConstant,
        0u,
        3u,
        484u,
        (4u << 16u) | kSpirvOpTypeArray,
        4u,
        2u,
        3u,
        (4u << 16u) | kSpirvOpTypePointer,
        5u,
        kSpirvFunctionStorageClass,
        4u,
        (4u << 16u) | kSpirvOpVariable,
        5u,
        6u,
        kSpirvFunctionStorageClass,
    };
    nr::test::require(declaresFunctionFloat4ArrayVariable(syntheticSpirv, 484u),
                      "SPIR-V local-array inspector positive control should detect Function float4[484]");
}

void requireComputeProgram(const nr::rhi::SlangProgram &program, std::string_view name)
{
    nr::test::require(program.valid(), std::format("{} should compile", name));
    auto const *entryPoint = program.entryPoint();
    nr::test::require(entryPoint != nullptr && entryPoint->spirv != nullptr && !entryPoint->spirv->empty(),
                      std::format("{} should expose compute SPIR-V", name));
    nr::test::require(entryPoint->stage == SLANG_STAGE_COMPUTE,
                      std::format("{} should be a compute entry point", name));
}

void requireDescriptorBindings(const nr::rhi::SlangProgram &program,
                               std::initializer_list<ExpectedDescriptorBinding> expectedBindings,
                               std::string_view programName)
{
    auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    nr::test::require(layout.valid(), std::format("{} descriptor layout should be valid", programName));
    auto root = layout.rootCursor();
    std::ranges::for_each(expectedBindings, [&](const ExpectedDescriptorBinding &expected) {
        auto cursor = root[expected.name];
        nr::test::require(cursor.valid(), std::format("{} should reflect {}", programName, expected.name));
        auto binding = cursor.descriptorBinding();
        nr::test::require(binding.has_value(),
                          std::format("{}.{} should expose a descriptor binding", programName, expected.name));
        nr::test::require(binding->set == expected.set && binding->binding == expected.binding &&
                              binding->descriptorType == expected.type,
                          std::format("{}.{} descriptor ABI should be set {} binding {} type {}", programName,
                                      expected.name, expected.set, expected.binding, vk::to_string(expected.type)));
    });
}

void requirePushConstant(const nr::rhi::SlangProgram &program, std::string_view rootName, std::uint32_t expectedSize,
                         std::initializer_list<std::pair<std::string_view, std::size_t>> expectedFields,
                         std::string_view programName)
{
    auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    auto push = layout.rootCursor()[rootName];
    nr::test::require(push.valid(), std::format("{} should reflect {}", programName, rootName));
    auto range = push.pushConstantRange();
    nr::test::require(range.has_value() && range->offset == 0u && range->size == expectedSize,
                      std::format("{}.{} should expose a {}-byte push range", programName, rootName, expectedSize));
    std::ranges::for_each(expectedFields, [&](const auto &expected) {
        auto field = push[expected.first];
        nr::test::require(field.valid() && field.address().uniformOffset == expected.second,
                          std::format("{}.{}.{} should remain at byte offset {}", programName, rootName, expected.first,
                                      expected.second));
    });
}

void requireNeuralMaterialContextLayout(const nr::rhi::SlangProgram &program, std::string_view programName)
{
    auto *programLayout = program.programLayout();
    nr::test::require(programLayout != nullptr, std::format("{} should expose reflection", programName));
    auto *contextType = programLayout->findTypeByName("NeuralMaterialContext");
    auto *contextLayout = contextType != nullptr ? programLayout->getTypeLayout(contextType) : nullptr;
    nr::test::require(contextLayout != nullptr, std::format("{} should reflect NeuralMaterialContext", programName));
    auto const size = static_cast<std::size_t>(contextLayout->getSize());
    auto const stride = static_cast<std::size_t>(contextLayout->getStride());
    nr::test::require(size == 224u && stride == 224u,
                      std::format("NeuralMaterialContext should remain 224 bytes: size={}, stride={}", size, stride));
}

[[nodiscard]] std::vector<nr::rhi::SlangProgram> compileNeuralPrograms()
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto const requests = std::array{
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/initializeTraining"}},
        nr::rhi::SlangProgramCompileFileRequest{.sourcePath =
                                                    std::filesystem::path{"renderer/neuralAppearance/evaluateTargets"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateGradients"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/optimizeTraining"}},
        nr::rhi::SlangProgramCompileFileRequest{.sourcePath =
                                                    std::filesystem::path{"renderer/neuralAppearance/packLatent"}},
        nr::rhi::SlangProgramCompileFileRequest{.sourcePath =
                                                    std::filesystem::path{"renderer/neuralAppearance/viewer"}},
        nr::rhi::SlangProgramCompileFileRequest{.sourcePath =
                                                    std::filesystem::path{"test/neuralAppearance/modelContract"}},
        nr::rhi::SlangProgramCompileFileRequest{.sourcePath =
                                                    std::filesystem::path{"test/neuralAppearance/autodiffContract"}},
        nr::rhi::SlangProgramCompileFileRequest{.sourcePath =
                                                    std::filesystem::path{"test/neuralAppearance/qualityContract"}},
    };
    auto programs = shaderService.compileProgramsByFile(requests);
    nr::test::requireEqual(programs.size(), requests.size());
    return programs;
}

void inspectNeuralPrograms(const std::vector<nr::rhi::SlangProgram> &programs)
{
    auto const programNames = std::array{
        std::string_view{"initializeTraining"}, std::string_view{"evaluateTargets"},
        std::string_view{"evaluateGradients"},  std::string_view{"optimizeTraining"},
        std::string_view{"packLatent"},         std::string_view{"viewer"},
        std::string_view{"modelContract"},      std::string_view{"autodiffContract"},
        std::string_view{"qualityContract"},
    };
    std::ranges::for_each(std::views::iota(std::size_t{0u}, programs.size()),
                          [&](std::size_t index) { requireComputeProgram(programs[index], programNames[index]); });

    auto const &initialize = programs[programIndex(NeuralProgram::Initialize)];
    auto const &target = programs[programIndex(NeuralProgram::Target)];
    auto const &gradient = programs[programIndex(NeuralProgram::Gradient)];
    auto const &optimize = programs[programIndex(NeuralProgram::Optimize)];
    auto const &pack = programs[programIndex(NeuralProgram::Pack)];
    auto const &viewer = programs[programIndex(NeuralProgram::Viewer)];
    auto const &modelContract = programs[programIndex(NeuralProgram::ModelContract)];
    auto const &autodiffContract = programs[programIndex(NeuralProgram::AutodiffContract)];
    auto const &quality = programs[programIndex(NeuralProgram::Quality)];

    requireDescriptorBindings(initialize,
                              {{"gTrainingControl", 1u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelParameters", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelMoments", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingLatent", 2u, 2u, vk::DescriptorType::eStorageBuffer},
                               {"gLatentMoments", 2u, 3u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingStatus", 2u, 4u, vk::DescriptorType::eStorageBuffer}},
                              "initializeTraining");
    requireDescriptorBindings(target, {{"gSampleTargets", 2u, 0u, vk::DescriptorType::eStorageBuffer}},
                              "evaluateTargets");
    requireDescriptorBindings(gradient,
                              {{"gModelParameters", 1u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingLatent", 1u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleTargets", 2u, 3u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleGradients", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleTexelIndices", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleMetrics", 2u, 2u, vk::DescriptorType::eStorageBuffer}},
                              "evaluateGradients");
    requireDescriptorBindings(optimize,
                              {{"gSampleGradients", 1u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleTexelIndices", 1u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleMetrics", 2u, 6u, vk::DescriptorType::eStorageBuffer},
                               {"gModelParameters", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelMoments", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingLatent", 2u, 2u, vk::DescriptorType::eStorageBuffer},
                               {"gLatentMoments", 2u, 3u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingStatus", 2u, 4u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingControl", 2u, 5u, vk::DescriptorType::eStorageBuffer}},
                              "optimizeTraining");
    requireDescriptorBindings(pack,
                              {{"gTrainingLatent", 1u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gLatentTexture0", 2u, 0u, vk::DescriptorType::eStorageImage},
                               {"gLatentTexture1", 2u, 1u, vk::DescriptorType::eStorageImage}},
                              "packLatent");
    requireDescriptorBindings(viewer,
                              {{"gLatentTexture0", 1u, 0u, vk::DescriptorType::eCombinedImageSampler},
                               {"gLatentTexture1", 1u, 1u, vk::DescriptorType::eCombinedImageSampler},
                               {"gModelParameters", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingStatus", 2u, 2u, vk::DescriptorType::eStorageBuffer},
                               {"gOutputColor", 2u, 0u, vk::DescriptorType::eStorageImage}},
                              "viewer");
    requireDescriptorBindings(modelContract,
                              {{"gLatentTexture0", 1u, 0u, vk::DescriptorType::eCombinedImageSampler},
                               {"gLatentTexture1", 1u, 1u, vk::DescriptorType::eCombinedImageSampler},
                               {"gModelParameters", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gContractResults", 2u, 0u, vk::DescriptorType::eStorageBuffer}},
                              "modelContract");
    requireDescriptorBindings(autodiffContract, {{"gAutodiffResults", 2u, 0u, vk::DescriptorType::eStorageBuffer}},
                              "autodiffContract");
    requireDescriptorBindings(quality,
                              {{"gLatentTexture0", 1u, 0u, vk::DescriptorType::eCombinedImageSampler},
                               {"gLatentTexture1", 1u, 1u, vk::DescriptorType::eCombinedImageSampler},
                               {"gModelParameters", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gQualityMetrics", 2u, 0u, vk::DescriptorType::eStorageBuffer}},
                              "qualityContract");

    requirePushConstant(target, "gTraining", 32u,
                        {{"trainingStep", 0u},
                         {"batchSize", 4u},
                         {"latentWidth", 8u},
                         {"latentHeight", 12u},
                         {"mollificationSampleCount", 16u},
                         {"mollificationStepCount", 20u},
                         {"initialAngleRadians", 24u},
                         {"padding", 28u}},
                        "evaluateTargets");
    requirePushConstant(gradient, "gTraining", 32u,
                        {{"trainingStep", 0u},
                         {"batchSize", 4u},
                         {"latentWidth", 8u},
                         {"latentHeight", 12u},
                         {"mollificationSampleCount", 16u},
                         {"mollificationStepCount", 20u},
                         {"initialAngleRadians", 24u},
                         {"padding", 28u}},
                        "evaluateGradients");
    requirePushConstant(optimize, "gTraining", 48u,
                        {{"trainingStep", 0u},
                         {"batchSize", 4u},
                         {"parameterChunkCount", 8u},
                         {"latentTexelCount", 12u},
                         {"totalTrainingSteps", 16u},
                         {"latentWarmupSteps", 20u},
                         {"initialLearningRate", 24u},
                         {"finalLearningRate", 28u},
                         {"latentLearningRateScale", 32u},
                         {"beta1", 36u},
                         {"beta2", 40u},
                         {"epsilon", 44u}},
                        "optimizeTraining");
    requirePushConstant(viewer, "gNeuralAppearance", 24u,
                        {{"width", 0u},
                         {"height", 4u},
                         {"frameIndex", 8u},
                         {"totalTrainingSteps", 12u},
                         {"comparisonEnabled", 16u},
                         {"errorGain", 20u}},
                        "viewer");

    requireNeuralMaterialContextLayout(viewer, "viewer");
    requireNeuralMaterialContextLayout(modelContract, "modelContract");
    requireNeuralMaterialContextLayout(quality, "qualityContract");
    requireLocalArrayInspectorPositiveControl();

    auto const *entryPoint = viewer.entryPoint();
    auto const samples = inspectSpirvSampleInstructions(std::span<const std::uint32_t>{*entryPoint->spirv});
    nr::test::requireEqual(samples.total, std::size_t{2u}, "viewer should contain exactly two static image samples");
    nr::test::requireEqual(samples.explicitLod, std::size_t{2u}, "viewer should use exactly two explicit mip0 samples");
    nr::test::require(!declaresFunctionFloat4ArrayVariable(std::span<const std::uint32_t>{*entryPoint->spirv},
                                                           static_cast<std::uint32_t>(kParameterChunkCount)),
                      "viewer must not materialize the 484-float4 runtime model in Function storage");
}

[[nodiscard]] nr::rhi::Buffer createStorageBuffer(nr::rhi::Device &device, vk::DeviceSize size,
                                                  vk::BufferUsageFlags extraUsage, std::string_view name)
{
    auto buffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(size, vk::BufferUsageFlagBits::eStorageBuffer | extraUsage),
        nr::rhi::MemoryUsage::GpuOnly, name);
    nr::test::require(buffer.valid(), std::format("{} should be a valid GPU buffer", name));
    return buffer;
}

[[nodiscard]] nr::rhi::Image createLatentImage(nr::rhi::Device &device, std::string_view name)
{
    auto image = device.resourceFactory.createImage(
        nr::rhi::makeImageCreateInfo(vk::Format::eR16G16B16A16Sfloat, vk::Extent2D{kLatentWidth, kLatentHeight},
                                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled),
        nr::rhi::MemoryUsage::GpuOnly, name);
    nr::test::require(image.valid() && image.mipLevels() == 1u,
                      std::format("{} should be a single-mip RGBA16F image", name));
    return image;
}

[[nodiscard]] nr::rhi::Image createViewerOutputImage(nr::rhi::Device &device, std::string_view name)
{
    auto image = device.resourceFactory.createImage(
        nr::rhi::makeImageCreateInfo(vk::Format::eR16G16B16A16Sfloat, vk::Extent2D{kViewerWidth, kViewerHeight},
                                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc),
        nr::rhi::MemoryUsage::GpuOnly, name);
    nr::test::require(image.valid() && image.mipLevels() == 1u,
                      "headless viewer output should be a single-mip RGBA16F image");
    return image;
}

void bindBuffer(const nr::rhi::ShaderCursor &root, std::string_view name, const nr::rhi::Buffer &buffer)
{
    auto cursor = root[name];
    nr::test::require(cursor.valid() && cursor.setObject(buffer),
                      std::format("{} should bind through reflection", name));
}

void bindImage(const nr::rhi::ShaderCursor &root, std::string_view name, const nr::rhi::Image &image,
               vk::ImageLayout layout)
{
    auto cursor = root[name];
    nr::test::require(cursor.valid() && cursor.setObject(image, layout),
                      std::format("{} should bind through reflection", name));
}

void bindSampledImage(const nr::rhi::ShaderCursor &root, std::string_view name, const nr::rhi::Image &image,
                      vk::Sampler sampler)
{
    auto cursor = root[name];
    nr::test::require(cursor.valid() && cursor.setObject(image, sampler, vk::ImageLayout::eGeneral),
                      std::format("{} should bind through reflection", name));
}

template <typename TBinder>
[[nodiscard]] PreparedComputeBindings prepareBindings(nr::rhi::PipelineState<nr::rhi::ComputePipeline> &pipeline,
                                                      TBinder &&binder)
{
    auto root = pipeline.descriptorLayout.rootCursor();
    std::invoke(std::forward<TBinder>(binder), root);
    auto snapshot = root.snapshot();
    auto sets = nr::rhi::allocateBindingSetsForLayout(pipeline.layout, pipeline.bindingPool);
    auto writeCache = nr::rhi::DescriptorWriteCache{};
    nr::rhi::updateResourcesForBindingSnapshot(pipeline.bindingPool, sets, writeCache, snapshot, {});
    root.clearSnapshot();
    return PreparedComputeBindings{
        .root = std::move(root), .sets = std::move(sets), .writeCache = std::move(writeCache)};
}

template <typename T> void setTrainingPushConstants(PreparedComputeBindings &bindings, const T &pushConstants)
{
    nr::test::require(bindings.root["gTraining"].setData(pushConstants),
                      "gTraining push constants should bind through reflection");
}

void recordDispatch(const vk::raii::CommandBuffer &commandBuffer,
                    const nr::rhi::PipelineState<nr::rhi::ComputePipeline> &pipeline,
                    const PreparedComputeBindings &bindings, std::uint32_t x, std::uint32_t y = 1u,
                    std::uint32_t z = 1u)
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline.raw());
    nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer, vk::PipelineBindPoint::eCompute, pipeline.layout,
                                                  bindings.sets);
    nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipeline.layout, bindings.root.snapshot());
    commandBuffer.dispatch(x, y, z);
}

void recordComputeReadWriteBarrier(const vk::raii::CommandBuffer &commandBuffer)
{
    auto barrier = vk::MemoryBarrier2{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite;
    auto barriers = nr::rhi::ops::BarrierBatch{};
    barriers.add(barrier);
    nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
}

void recordImageTransitionForStorageWrite(const vk::raii::CommandBuffer &commandBuffer, const nr::rhi::Image &image)
{
    auto barrier = vk::ImageMemoryBarrier2{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eGeneral;
    barrier.image = image.handle();
    barrier.subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u};
    auto barriers = nr::rhi::ops::BarrierBatch{};
    barriers.add(barrier);
    nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
}

void recordPackToInferenceBarrier(const vk::raii::CommandBuffer &commandBuffer,
                                  std::span<const std::reference_wrapper<const nr::rhi::Image>> images)
{
    auto barriers = nr::rhi::ops::BarrierBatch{};
    std::ranges::for_each(images, [&](const nr::rhi::Image &image) {
        auto barrier = vk::ImageMemoryBarrier2{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.image = image.handle();
        barrier.subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u};
        barriers.add(barrier);
    });
    nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
}

void recordInferenceToPackBarrier(const vk::raii::CommandBuffer &commandBuffer,
                                  std::span<const std::reference_wrapper<const nr::rhi::Image>> images)
{
    auto barriers = nr::rhi::ops::BarrierBatch{};
    std::ranges::for_each(images, [&](const nr::rhi::Image &image) {
        auto barrier = vk::ImageMemoryBarrier2{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        barrier.oldLayout = vk::ImageLayout::eGeneral;
        barrier.newLayout = vk::ImageLayout::eGeneral;
        barrier.image = image.handle();
        barrier.subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u};
        barriers.add(barrier);
    });
    nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
}

template <typename T>
[[nodiscard]] std::vector<T> readbackBuffer(nr::rhi::Device &device, const nr::rhi::Buffer &buffer)
{
    nr::test::require(buffer.size() % sizeof(T) == 0u, "typed readback size should divide the buffer size");
    auto ticket = device.uploadReadback().readbackBuffer(
        buffer, 0u, buffer.size(), nr::rhi::QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{.stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                       .access = vk::AccessFlagBits2::eShaderStorageRead |
                                                                 vk::AccessFlagBits2::eShaderStorageWrite},
            .postCopy = nr::rhi::ops::ReadbackSyncScope{.stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                        .access = vk::AccessFlagBits2::eShaderStorageRead |
                                                                  vk::AccessFlagBits2::eShaderStorageWrite}});
    auto bytes = device.uploadReadback().readbackBytes(ticket);
    auto values = std::vector<T>(bytes.size() / sizeof(T));
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

[[nodiscard]] std::vector<std::byte> readbackViewerOutput(nr::rhi::Device &device, const nr::rhi::Image &image)
{
    auto ticket = device.uploadReadback().readbackImage(
        image, vk::ImageLayout::eGeneral, nr::rhi::QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{.stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                       .access = vk::AccessFlagBits2::eShaderStorageWrite},
            .postCopy = nr::rhi::ops::ReadbackSyncScope{.stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                        .access = vk::AccessFlagBits2::eShaderStorageRead |
                                                                  vk::AccessFlagBits2::eShaderStorageWrite}});
    return device.uploadReadback().readbackBytes(ticket);
}

[[nodiscard]] bool finiteNonNegativeHalf(std::uint16_t bits) noexcept
{
    auto const exponent = (bits >> 10u) & 0x1fu;
    auto const magnitude = bits & 0x7fffu;
    return exponent != 0x1fu && ((bits & 0x8000u) == 0u || magnitude == 0u);
}

[[nodiscard]] float halfToFloat(std::uint16_t bits) noexcept
{
    auto const negative = (bits & 0x8000u) != 0u;
    auto const exponent = static_cast<std::uint32_t>((bits >> 10u) & 0x1fu);
    auto const mantissa = static_cast<std::uint32_t>(bits & 0x03ffu);
    if (exponent == 0x1fu)
    {
        return mantissa == 0u
                   ? (negative ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity())
                   : std::numeric_limits<float>::quiet_NaN();
    }

    auto const magnitude =
        exponent == 0u ? std::ldexp(static_cast<float>(mantissa), -24)
                       : std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
    return negative ? -magnitude : magnitude;
}

[[nodiscard]] DistributionMetrics distributionMetrics(std::span<const float> values)
{
    nr::test::require(!values.empty(), "distribution metrics require at least one sample");
    nr::test::require(std::ranges::all_of(values, [](float value) { return std::isfinite(value) && value >= 0.0f; }),
                      "distribution samples should be finite and non-negative");
    auto sorted = std::vector<float>{values.begin(), values.end()};
    std::ranges::sort(sorted);
    auto const percentileIndex = (sorted.size() * std::size_t{95u} + std::size_t{99u}) / std::size_t{100u} - 1u;
    auto const sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    return DistributionMetrics{
        .mean = static_cast<float>(sum / static_cast<double>(sorted.size())),
        .percentile95 = sorted[percentileIndex],
    };
}

[[nodiscard]] std::size_t viewerPanelDifferenceCount(std::span<const std::uint16_t> pixels, std::uint32_t firstPanel,
                                                     std::uint32_t secondPanel)
{
    auto const panelWidth = kViewerWidth / 3u;
    auto const panelPixelCount = static_cast<std::size_t>(panelWidth) * kViewerHeight;
    return static_cast<std::size_t>(
        std::ranges::count_if(std::views::iota(std::size_t{0u}, panelPixelCount), [&](std::size_t panelPixel) {
            auto const x = static_cast<std::uint32_t>(panelPixel % panelWidth);
            auto const y = static_cast<std::uint32_t>(panelPixel / panelWidth);
            auto const firstPixel = (static_cast<std::size_t>(y) * kViewerWidth + firstPanel * panelWidth + x) * 4u;
            auto const secondPixel = (static_cast<std::size_t>(y) * kViewerWidth + secondPanel * panelWidth + x) * 4u;
            return std::ranges::any_of(std::views::iota(std::size_t{0u}, std::size_t{3u}), [&](std::size_t channel) {
                return pixels[firstPixel + channel] != pixels[secondPixel + channel];
            });
        }));
}

void requireViewerOutputContract(std::span<const std::byte> bytes)
{
    constexpr auto kRgba16fBytesPerPixel = std::size_t{8u};
    auto const expectedByteCount = static_cast<std::size_t>(kViewerWidth) * kViewerHeight * kRgba16fBytesPerPixel;
    nr::test::requireEqual(bytes.size(), expectedByteCount);
    auto pixels = std::vector<std::uint16_t>(bytes.size() / sizeof(std::uint16_t));
    std::memcpy(pixels.data(), bytes.data(), bytes.size());

    auto const pixelCount = static_cast<std::size_t>(kViewerWidth) * kViewerHeight;
    nr::test::require(std::ranges::all_of(std::views::iota(std::size_t{0u}, pixelCount),
                                          [&](std::size_t pixel) {
                                              auto const base = pixel * 4u;
                                              return finiteNonNegativeHalf(pixels[base]) &&
                                                     finiteNonNegativeHalf(pixels[base + 1u]) &&
                                                     finiteNonNegativeHalf(pixels[base + 2u]) &&
                                                     pixels[base + 3u] == 0x3c00u;
                                          }),
                      "headless viewer RGBA16F output should be finite, non-negative, with alpha one");

    auto const panelWidth = kViewerWidth / 3u;
    auto const panelPixelCount = static_cast<std::size_t>(panelWidth) * kViewerHeight;
    auto mappedErrors = std::vector<float>{};
    mappedErrors.reserve(panelPixelCount);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, panelPixelCount), [&](std::size_t panelPixel) {
        auto const x = static_cast<std::uint32_t>(panelPixel % panelWidth);
        auto const y = static_cast<std::uint32_t>(panelPixel / panelWidth);
        auto const nativeBase = (static_cast<std::size_t>(y) * kViewerWidth + x) * 4u;
        auto const neuralBase = (static_cast<std::size_t>(y) * kViewerWidth + panelWidth + x) * 4u;
        auto channels = std::views::iota(std::size_t{0u}, std::size_t{3u});
        auto const rgbError =
            std::accumulate(channels.begin(), channels.end(), 0.0f, [&](float sum, std::size_t channel) {
                auto const native = halfToFloat(pixels[nativeBase + channel]);
                auto const neural = halfToFloat(pixels[neuralBase + channel]);
                auto const mappedNative = native / (1.0f + native);
                auto const mappedNeural = neural / (1.0f + neural);
                return sum + std::abs(mappedNative - mappedNeural);
            });
        mappedErrors.push_back(rgbError / 3.0f);
    });
    auto const mapped = distributionMetrics(mappedErrors);
    std::println("[neural-viewer-quality] mapped mean={} p95={}", mapped.mean, mapped.percentile95);
    nr::test::require(mapped.mean <= 0.45f && mapped.percentile95 <= 0.80f,
                      std::format("viewer native-vs-neural mapped RGB error should remain bounded: mean={}, p95={}",
                                  mapped.mean, mapped.percentile95));

    auto const minimumDifferentPixels = static_cast<std::size_t>(kViewerWidth / 3u) * kViewerHeight / 8u;
    auto const panelPairs = std::array{std::pair{0u, 1u}, std::pair{0u, 2u}, std::pair{1u, 2u}};
    std::ranges::for_each(panelPairs, [&](const auto &panels) {
        auto const differentPixels = viewerPanelDifferenceCount(pixels, panels.first, panels.second);
        nr::test::require(differentPixels >= minimumDifferentPixels,
                          std::format("viewer panels {} and {} should differ at >= {} corresponding pixels (actual={})",
                                      panels.first, panels.second, minimumDifferentPixels, differentPixels));
    });
}

void requireCompletedViewerProgressOutputContract(std::span<const std::byte> bytes,
                                                  std::span<const std::byte> comparisonBytes)
{
    constexpr auto kRgba16fBytesPerPixel = std::size_t{8u};
    constexpr auto kHalfTolerance = 1.0e-3f;
    auto const expectedByteCount = static_cast<std::size_t>(kViewerWidth) * kViewerHeight * kRgba16fBytesPerPixel;
    nr::test::requireEqual(bytes.size(), expectedByteCount);
    nr::test::requireEqual(comparisonBytes.size(), expectedByteCount);
    nr::test::require(!std::ranges::equal(bytes, comparisonBytes),
                      "comparison-disabled viewer should replace the three-panel quality output with progress");

    auto pixels = std::vector<std::uint16_t>(bytes.size() / sizeof(std::uint16_t));
    std::memcpy(pixels.data(), bytes.data(), bytes.size());
    auto const pixelCount = static_cast<std::size_t>(kViewerWidth) * kViewerHeight;
    auto const pixelsIndices = std::views::iota(std::size_t{0u}, pixelCount);
    auto const mismatch = std::ranges::find_if(pixelsIndices, [&](std::size_t pixel) {
        auto const x = static_cast<std::uint32_t>(pixel % kViewerWidth);
        auto const y = static_cast<std::uint32_t>(pixel / kViewerWidth);
        auto const outputUvX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kViewerWidth);
        auto const outputUvY = (static_cast<float>(y) + 0.5f) / static_cast<float>(kViewerHeight);
        auto const insideTrack = std::abs(outputUvY - 0.5f) <= 0.035f && outputUvX >= 0.08f && outputUvX <= 0.92f;
        auto const expected =
            insideTrack ? std::array{0.08f, 0.72f, 0.92f}
                        : std::array{0.008f + (0.025f - 0.008f) * outputUvY, 0.012f + (0.04f - 0.012f) * outputUvY,
                                     0.02f + (0.065f - 0.02f) * outputUvY};
        auto const base = pixel * 4u;
        auto const channels = std::views::iota(std::size_t{0u}, std::size_t{3u});
        return pixels[base + 3u] != 0x3c00u || std::ranges::any_of(channels, [&](std::size_t channel) {
                   auto const actual = halfToFloat(pixels[base + channel]);
                   return !std::isfinite(actual) || std::abs(actual - expected[channel]) > kHalfTolerance;
               });
    });
    nr::test::require(
        mismatch == pixelsIndices.end(),
        std::format("completed comparison-disabled viewer should emit the exact finite progress image; first "
                    "mismatched pixel={}",
                    mismatch == pixelsIndices.end() ? pixelCount : *mismatch));
}

[[nodiscard]] std::vector<QualitySample> decodeQualitySamples(std::span<const float> metrics, std::string_view phase)
{
    nr::test::requireEqual(metrics.size(), kQualitySampleCount * kQualityRecordFloatCount);
    auto samples = std::vector<QualitySample>(kQualitySampleCount);
    std::ranges::transform(
        std::views::iota(std::size_t{0u}, kQualitySampleCount), samples.begin(), [&](std::size_t sampleIndex) {
            auto const offset = sampleIndex * kQualityRecordFloatCount;
            auto const finiteFlag = metrics[offset + 3u];
            auto const encodedStratum = metrics[offset + 7u];
            nr::test::require(
                std::ranges::all_of(metrics.subspan(offset, kQualityRecordFloatCount),
                                    [](float value) { return std::isfinite(value); }) &&
                    finiteFlag > 0.5f && encodedStratum >= 0.0f && encodedStratum < 3.0f,
                std::format("{} quality sample {} should contain finite valid records", phase, sampleIndex));

            auto const stratumValue = static_cast<std::uint32_t>(std::floor(encodedStratum));
            nr::test::require(stratumValue <= static_cast<std::uint32_t>(QualityStratum::Grazing),
                              std::format("{} quality sample {} should encode a known stratum", phase, sampleIndex));
            auto const minimumPhysicalCosine = (encodedStratum - static_cast<float>(stratumValue)) * 4.0f;
            auto const sample = QualitySample{
                .mappedMae = metrics[offset + 0u],
                .safeLogL1 = metrics[offset + 1u],
                .rawMae = metrics[offset + 2u],
                .zeroMappedBaseline = metrics[offset + 4u],
                .zeroLogBaseline = metrics[offset + 5u],
                .maximumPhysicalCosine = metrics[offset + 6u],
                .minimumPhysicalCosine = minimumPhysicalCosine,
                .stratum = static_cast<QualityStratum>(stratumValue),
            };
            nr::test::require(
                sample.mappedMae >= 0.0f && sample.mappedMae <= 1.0f && sample.safeLogL1 >= 0.0f &&
                    sample.rawMae >= 0.0f && sample.zeroMappedBaseline >= 0.0f && sample.zeroMappedBaseline <= 1.0f &&
                    sample.zeroLogBaseline >= 0.0f && sample.maximumPhysicalCosine >= -1.0e-5f &&
                    sample.maximumPhysicalCosine <= 1.0f + 1.0e-5f && sample.minimumPhysicalCosine >= -1.0e-5f &&
                    sample.minimumPhysicalCosine <= sample.maximumPhysicalCosine + 1.0e-5f,
                std::format("{} quality sample {} metrics should be physically bounded", phase, sampleIndex));
            if (sample.stratum == QualityStratum::Grazing)
            {
                nr::test::require(
                    sample.maximumPhysicalCosine <= 0.081f,
                    std::format("{} grazing quality sample {} should keep both directions near the horizon: "
                                "min/max NoV/NoL={}/{}",
                                phase, sampleIndex, sample.minimumPhysicalCosine, sample.maximumPhysicalCosine));
            }
            return sample;
        });

    auto const uniformCount = std::ranges::count(samples, QualityStratum::Uniform, &QualitySample::stratum);
    auto const highlightCount = std::ranges::count(samples, QualityStratum::Highlight, &QualitySample::stratum);
    auto const grazingCount = std::ranges::count(samples, QualityStratum::Grazing, &QualitySample::stratum);
    nr::test::require(uniformCount == 128 && highlightCount == 64 && grazingCount == 64,
                      std::format("{} quality strata should remain 128/64/64 (actual={}/{}/{})", phase, uniformCount,
                                  highlightCount, grazingCount));
    return samples;
}

template <typename TPredicate>
[[nodiscard]] QualityStratumMetrics summarizeQualityStratum(std::span<const QualitySample> samples,
                                                            TPredicate &&predicate)
{
    auto mapped = std::vector<float>{};
    auto safeLog = std::vector<float>{};
    auto zeroMappedSum = 0.0;
    auto zeroLogSum = 0.0;
    mapped.reserve(samples.size());
    safeLog.reserve(samples.size());
    std::ranges::for_each(samples, [&](const QualitySample &sample) {
        if (std::invoke(predicate, sample))
        {
            mapped.push_back(sample.mappedMae);
            safeLog.push_back(sample.safeLogL1);
            zeroMappedSum += sample.zeroMappedBaseline;
            zeroLogSum += sample.zeroLogBaseline;
        }
    });
    nr::test::require(mapped.size() >= 64u, "quality summary bins should contain at least 64 samples");
    return QualityStratumMetrics{
        .sampleCount = mapped.size(),
        .mapped = distributionMetrics(mapped),
        .safeLog = distributionMetrics(safeLog),
        .zeroMappedMean = static_cast<float>(zeroMappedSum / static_cast<double>(mapped.size())),
        .zeroLogMean = static_cast<float>(zeroLogSum / static_cast<double>(mapped.size())),
    };
}

[[nodiscard]] QualityMetrics summarizeQuality(std::span<const QualitySample> samples)
{
    return QualityMetrics{
        .overall = summarizeQualityStratum(samples, [](const QualitySample &) { return true; }),
        .highlight = summarizeQualityStratum(
            samples, [](const QualitySample &sample) { return sample.stratum == QualityStratum::Highlight; }),
        .grazing = summarizeQualityStratum(
            samples, [](const QualitySample &sample) { return sample.stratum == QualityStratum::Grazing; }),
    };
}

void requireQualityContract(std::span<const float> initialMetrics, std::span<const float> finalMetrics)
{
    auto const initialSamples = decodeQualitySamples(initialMetrics, "initial");
    auto const finalSamples = decodeQualitySamples(finalMetrics, "trained");
    std::ranges::for_each(std::views::iota(std::size_t{0u}, kQualitySampleCount), [&](std::size_t sampleIndex) {
        auto const &initial = initialSamples[sampleIndex];
        auto const &final = finalSamples[sampleIndex];
        nr::test::require(
            initial.stratum == final.stratum &&
                std::abs(initial.zeroMappedBaseline - final.zeroMappedBaseline) <= 1.0e-6f &&
                std::abs(initial.zeroLogBaseline - final.zeroLogBaseline) <= 1.0e-6f &&
                std::abs(initial.maximumPhysicalCosine - final.maximumPhysicalCosine) <= 1.0e-5f &&
                std::abs(initial.minimumPhysicalCosine - final.minimumPhysicalCosine) <= 1.0e-5f,
            std::format("held-out quality sample {} target metadata should be deterministic", sampleIndex));
    });

    auto const initial = summarizeQuality(initialSamples);
    auto const final = summarizeQuality(finalSamples);
    std::println("[neural-heldout-quality] mapped initial overall/highlight/grazing={}/{}/{}; "
                 "final overall={}/{} highlight={}/{} grazing={}/{}; "
                 "log initial overall/highlight/grazing={}/{}/{}; "
                 "final overall={}/{} highlight={}/{} grazing={}/{}; "
                 "zero-baseline overall={}/{} highlight={}/{} grazing={}/{}",
                 initial.overall.mapped.mean, initial.highlight.mapped.mean, initial.grazing.mapped.mean,
                 final.overall.mapped.mean, final.overall.mapped.percentile95, final.highlight.mapped.mean,
                 final.highlight.mapped.percentile95, final.grazing.mapped.mean, final.grazing.mapped.percentile95,
                 initial.overall.safeLog.mean, initial.highlight.safeLog.mean, initial.grazing.safeLog.mean,
                 final.overall.safeLog.mean, final.overall.safeLog.percentile95, final.highlight.safeLog.mean,
                 final.highlight.safeLog.percentile95, final.grazing.safeLog.mean, final.grazing.safeLog.percentile95,
                 final.overall.zeroMappedMean, final.overall.zeroLogMean, final.highlight.zeroMappedMean,
                 final.highlight.zeroLogMean, final.grazing.zeroMappedMean, final.grazing.zeroLogMean);
    auto requireImprovesInitializer = [](const QualityStratumMetrics &before, const QualityStratumMetrics &after,
                                         std::string_view stratum) {
        nr::test::require(
            after.mapped.mean <= before.mapped.mean * 0.95f && after.safeLog.mean <= before.safeLog.mean * 0.95f,
            std::format("trained {} quality should improve over initialization by at least 5%: "
                        "mapped={}/{}, log={}/{}",
                        stratum, after.mapped.mean, before.mapped.mean, after.safeLog.mean, before.safeLog.mean));
    };
    requireImprovesInitializer(initial.overall, final.overall, "overall");
    requireImprovesInitializer(initial.highlight, final.highlight, "highlight");
    requireImprovesInitializer(initial.grazing, final.grazing, "grazing");

    nr::test::require(
        final.overall.mapped.mean <= 0.095f && final.overall.mapped.percentile95 <= 0.54f &&
            final.highlight.mapped.mean <= 0.24f && final.highlight.mapped.percentile95 <= 0.63f &&
            final.grazing.mapped.mean <= 0.045f && final.grazing.mapped.percentile95 <= 0.132f,
        std::format("held-out mapped error ceilings failed: overall={}/{}, highlight={}/{}, grazing={}/{}",
                    final.overall.mapped.mean, final.overall.mapped.percentile95, final.highlight.mapped.mean,
                    final.highlight.mapped.percentile95, final.grazing.mapped.mean, final.grazing.mapped.percentile95));
    nr::test::require(final.overall.safeLog.mean <= 0.135f && final.overall.safeLog.percentile95 <= 0.85f &&
                          final.highlight.safeLog.mean <= 0.35f && final.highlight.safeLog.percentile95 <= 1.03f &&
                          final.grazing.safeLog.mean <= 0.062f && final.grazing.safeLog.percentile95 <= 0.157f,
                      std::format("held-out log error ceilings failed: overall={}/{}, highlight={}/{}, grazing={}/{}",
                                  final.overall.safeLog.mean, final.overall.safeLog.percentile95,
                                  final.highlight.safeLog.mean, final.highlight.safeLog.percentile95,
                                  final.grazing.safeLog.mean, final.grazing.safeLog.percentile95));
    auto requireValidZeroBaseline = [](const QualityStratumMetrics &metrics, std::string_view stratum) {
        nr::test::require(std::isfinite(metrics.zeroMappedMean) && metrics.zeroMappedMean > 0.0f &&
                              metrics.zeroMappedMean <= 1.0f && std::isfinite(metrics.zeroLogMean) &&
                              metrics.zeroLogMean > 0.0f,
                          std::format("{} zero-prediction baselines should be finite and positive: mapped={}, log={}",
                                      stratum, metrics.zeroMappedMean, metrics.zeroLogMean));
    };
    auto requireBeatsZeroPredictor = [](const QualityStratumMetrics &metrics, std::string_view stratum) {
        nr::test::require(metrics.mapped.mean <= metrics.zeroMappedMean * 0.99f &&
                              metrics.safeLog.mean <= metrics.zeroLogMean * 0.99f,
                          std::format("trained {} quality should beat the zero predictor by at least 1%: "
                                      "mapped={}/{}, log={}/{}",
                                      stratum, metrics.mapped.mean, metrics.zeroMappedMean, metrics.safeLog.mean,
                                      metrics.zeroLogMean));
    };
    requireValidZeroBaseline(final.overall, "overall");
    requireValidZeroBaseline(final.highlight, "highlight");
    requireValidZeroBaseline(final.grazing, "grazing");
    requireBeatsZeroPredictor(final.overall, "overall");
    requireBeatsZeroPredictor(final.highlight, "highlight");
    nr::test::require(final.grazing.mapped.mean <= final.grazing.zeroMappedMean + kGrazingZeroBaselineTolerance &&
                          final.grazing.safeLog.mean <= final.grazing.zeroLogMean + kGrazingZeroBaselineTolerance,
                      std::format("trained grazing quality should remain within {} of the zero predictor: "
                                  "mapped={}/{}, log={}/{}",
                                  kGrazingZeroBaselineTolerance, final.grazing.mapped.mean,
                                  final.grazing.zeroMappedMean, final.grazing.safeLog.mean, final.grazing.zeroLogMean));
    // The double-grazing stratum contains many exactly-zero projected targets,
    // while the paper-style exponential decoder is strictly positive. Its zero
    // baseline is diagnostic; the initializer and tight absolute/P95 gates above
    // remain the actionable grazing-quality contracts.
}

[[nodiscard]] float meanValidationLoss(std::span<const float> metrics)
{
    nr::test::require(metrics.size() >= static_cast<std::size_t>(kContractBatchSize) * 4u,
                      "training metrics should contain the contract batch");
    auto samples = std::views::iota(std::size_t{0u}, static_cast<std::size_t>(kContractBatchSize));
    nr::test::require(std::ranges::all_of(samples,
                                          [&](std::size_t sample) {
                                              auto const offset = sample * 4u;
                                              return std::isfinite(metrics[offset]) && metrics[offset] >= 0.0f &&
                                                     std::isfinite(metrics[offset + 1u]) &&
                                                     std::isfinite(metrics[offset + 2u]) && metrics[offset + 3u] > 0.5f;
                                          }),
                      "fixed validation batch metrics should remain finite");
    auto losses =
        samples | std::views::transform([&](std::size_t sample) { return static_cast<double>(metrics[sample * 4u]); });
    return static_cast<float>(std::accumulate(losses.begin(), losses.end(), 0.0) /
                              static_cast<double>(kContractBatchSize));
}

void requireFullBatchMetrics(std::span<const float> metrics)
{
    nr::test::requireEqual(metrics.size(), static_cast<std::size_t>(kShaderBatchSize) * 4u);
    auto samples = std::views::iota(std::size_t{0u}, static_cast<std::size_t>(kShaderBatchSize));
    nr::test::require(std::ranges::all_of(samples,
                                          [&](std::size_t sample) {
                                              auto const offset = sample * 4u;
                                              return std::isfinite(metrics[offset]) && metrics[offset] >= 0.0f &&
                                                     std::isfinite(metrics[offset + 1u]) &&
                                                     std::isfinite(metrics[offset + 2u]) && metrics[offset + 3u] > 0.5f;
                                          }),
                      "the 64-sample shader batch should write all finite metric rows");
}

void requireSampleTargets(std::span<const float> targets, std::size_t activeSampleCount, float expectedAngle,
                          std::string_view phase)
{
    nr::test::requireEqual(targets.size(), kSampleTargetChunkCount * 4u);
    nr::test::require(activeSampleCount > 0u && activeSampleCount <= kSampleTargetChunkCount,
                      std::format("{} target validation requires 1..{} active rows", phase, kSampleTargetChunkCount));
    nr::test::require(std::isfinite(expectedAngle) && expectedAngle >= 0.0f &&
                          expectedAngle <= kMollificationInitialAngleRadians + 1.0e-6f,
                      std::format("{} target angle should remain finite and scheduled", phase));

    auto samples = std::views::iota(std::size_t{0u}, activeSampleCount);
    auto const validRowCount = static_cast<std::size_t>(std::ranges::count_if(samples, [&](std::size_t sampleIndex) {
        auto const row = targets.subspan(sampleIndex * 4u, 4u);
        return std::ranges::all_of(row.first(3u), [](float value) { return std::isfinite(value) && value >= 0.0f; }) &&
               std::isfinite(row[3u]) && row[3u] >= 0.0f && std::abs(row[3u] - expectedAngle) <= 1.0e-6f;
    }));
    nr::test::require(validRowCount == activeSampleCount,
                      std::format("{} target dispatch should write all {} finite, non-negative rows at angle {} "
                                  "(valid={})",
                                  phase, activeSampleCount, expectedAngle, validRowCount));
}

[[nodiscard]] std::size_t changedScalarCount(std::span<const float> initial, std::span<const float> final)
{
    nr::test::requireEqual(initial.size(), final.size());
    return static_cast<std::size_t>(
        std::ranges::count_if(std::views::iota(std::size_t{0u}, initial.size()),
                              [&](std::size_t index) { return std::abs(initial[index] - final[index]) > 1.0e-7f; }));
}

[[nodiscard]] std::size_t changedFrameLatentScalarCount(std::span<const float> initial, std::span<const float> final,
                                                        std::size_t texelCount)
{
    auto lanes = std::views::iota(std::size_t{0u}, texelCount * 4u);
    return static_cast<std::size_t>(std::ranges::count_if(lanes, [&](std::size_t lane) {
        auto const flatIndex = (lane / 4u) * 8u + 4u + lane % 4u;
        return std::abs(initial[flatIndex] - final[flatIndex]) > 1.0e-7f;
    }));
}

[[nodiscard]] std::size_t changedFrameWeightScalarCount(std::span<const float> initial, std::span<const float> final,
                                                        std::size_t frameIndex)
{
    auto const weightOffset = frameIndex * kFrameWeightScalarCountPerFrame;
    return changedScalarCount(initial.subspan(weightOffset, kFrameWeightScalarCountPerFrame),
                              final.subspan(weightOffset, kFrameWeightScalarCountPerFrame));
}

void requirePackedAdamMoments(std::span<const float> moments, std::size_t valueChunkCount,
                              std::size_t minimumPositiveVarianceCount, std::string_view label)
{
    auto const halfScalarCount = valueChunkCount * 4u;
    nr::test::requireEqual(moments.size(), halfScalarCount * 2u);
    auto const firstMoments = moments.first(halfScalarCount);
    auto const secondMoments = moments.subspan(halfScalarCount, halfScalarCount);
    nr::test::require(
        std::ranges::all_of(firstMoments, [](float value) { return std::isfinite(value); }) &&
            std::ranges::all_of(secondMoments, [](float value) { return std::isfinite(value) && value >= 0.0f; }),
        std::format("{} packed Adam m/v halves should remain finite and v non-negative", label));
    auto const positiveVarianceCount =
        static_cast<std::size_t>(std::ranges::count_if(secondMoments, [](float value) { return value > 0.0f; }));
    nr::test::require(positiveVarianceCount >= minimumPositiveVarianceCount,
                      std::format("{} packed Adam v half should update at least {} scalars (actual={})", label,
                                  minimumPositiveVarianceCount, positiveVarianceCount));
}

void requireParameterPadding(std::span<const float> parameters, std::span<const float> packedMoments)
{
    auto const parameterScalarCount = kParameterChunkCount * 4u;
    nr::test::requireEqual(parameters.size(), parameterScalarCount);
    nr::test::requireEqual(packedMoments.size(), parameterScalarCount * 2u);
    auto const paddingScalar = parameterScalarCount - 1u;
    nr::test::require(parameters[paddingScalar] == 0.0f && packedMoments[paddingScalar] == 0.0f &&
                          packedMoments[parameterScalarCount + paddingScalar] == 0.0f,
                      "parameter, Adam m, and Adam v padding lanes should remain zero");
}

void requireAutodiffOracle(std::span<const float> actual)
{
    constexpr auto expected = std::array{3.0f, 2.0f, 7.0f, 5.0f};
    nr::test::requireEqual(actual.size(), expected.size());
    std::ranges::for_each(std::views::iota(std::size_t{0u}, expected.size()), [&](std::size_t index) {
        nr::test::require(std::isfinite(actual[index]) && std::abs(actual[index] - expected[index]) <= 1.0e-6f,
                          std::format("Slang reverse-mode derivative {} should be {} (actual={})", index,
                                      expected[index], actual[index]));
    });
}

void executeNeuralTrainingContract(const std::vector<nr::rhi::SlangProgram> &programs)
{
    auto device = nr::rhi::Device{};
    device.initialize("nr_rhi_neural_appearance_shader_contract_test", "NewbieRenderer");

    auto initializeBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::Initialize)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_initialize_training");
    auto targetBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::Target)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_evaluate_targets");
    auto gradientBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::Gradient)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_evaluate_gradients");
    auto optimizeBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::Optimize)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_optimize_training");
    auto packBuild = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Pack)], nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_pack_latent");
    auto viewerBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::Viewer)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_headless_viewer");
    auto contractBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::ModelContract)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_model_contract");
    auto autodiffBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::AutodiffContract)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_autodiff_contract");
    auto qualityBuild =
        device.pipeline().createComputePipeline(programs[programIndex(NeuralProgram::Quality)],
                                                nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_quality_contract");
    auto initializePipeline = initializeBuild.get();
    auto targetPipeline = targetBuild.get();
    auto gradientPipeline = gradientBuild.get();
    auto optimizePipeline = optimizeBuild.get();
    auto packPipeline = packBuild.get();
    auto viewerPipeline = viewerBuild.get();
    auto contractPipeline = contractBuild.get();
    auto autodiffPipeline = autodiffBuild.get();
    auto qualityPipeline = qualityBuild.get();

    auto const parameterBytes = static_cast<vk::DeviceSize>(kParameterChunkCount * 4u * sizeof(float));
    auto const latentBytes = static_cast<vk::DeviceSize>(kLatentChunkCount * 4u * sizeof(float));
    auto const targetBytes = static_cast<vk::DeviceSize>(kSampleTargetChunkCount * 4u * sizeof(float));
    auto const gradientBytes = static_cast<vk::DeviceSize>(kShaderBatchSize * kGradientChunkCount * 4u * sizeof(float));
    auto const batchRecordBytes = static_cast<vk::DeviceSize>(kShaderBatchSize * 4u * sizeof(std::uint32_t));
    auto const qualityMetricBytes =
        static_cast<vk::DeviceSize>(kQualitySampleCount * kQualityRecordFloatCount * sizeof(float));
    auto modelParameters = createStorageBuffer(device, parameterBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                               "neural_contract_model_parameters");
    auto poisonedModelParameters = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(parameterBytes, vk::BufferUsageFlagBits::eStorageBuffer),
        nr::rhi::MemoryUsage::CpuToGpu, "neural_contract_poisoned_viewer_model_parameters");
    nr::test::require(poisonedModelParameters.valid(),
                      "comparison-disabled viewer should bind a valid CPU-writable poison model buffer");
    auto poisonedParameters = std::vector<float>(kParameterChunkCount * 4u, std::numeric_limits<float>::quiet_NaN());
    poisonedModelParameters.writeMappedAndFlush(std::span<const float>{poisonedParameters});
    auto modelMoments = createStorageBuffer(device, 2u * parameterBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                            "neural_contract_model_moments");
    auto trainingLatent = createStorageBuffer(device, latentBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                              "neural_contract_training_latent");
    auto latentMoments = createStorageBuffer(device, 2u * latentBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                             "neural_contract_latent_moments");
    auto trainingControl = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(4u * sizeof(std::uint32_t),
                                      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc),
        nr::rhi::MemoryUsage::CpuToGpu, "neural_contract_training_control");
    nr::test::require(trainingControl.valid(),
                      "neural contract training control should be a valid CPU-writable buffer");
    auto trainingStatus = createStorageBuffer(device, 8u * sizeof(float), vk::BufferUsageFlagBits::eTransferSrc,
                                              "neural_contract_training_status");
    auto sampleTargets = createStorageBuffer(device, targetBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                             "neural_contract_sample_targets");
    auto sampleGradients = createStorageBuffer(device, gradientBytes, {}, "neural_contract_sample_gradients");
    auto sampleIndices = createStorageBuffer(device, batchRecordBytes, {}, "neural_contract_sample_indices");
    auto sampleMetrics = createStorageBuffer(device, batchRecordBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                             "neural_contract_sample_metrics");
    auto qualityMetrics = createStorageBuffer(device, qualityMetricBytes, vk::BufferUsageFlagBits::eTransferSrc,
                                              "neural_contract_quality_metrics");
    auto contractResults =
        createStorageBuffer(device, static_cast<vk::DeviceSize>(kContractResultNames.size() * sizeof(std::uint32_t)),
                            vk::BufferUsageFlagBits::eTransferSrc, "neural_contract_results");
    auto autodiffResults = createStorageBuffer(device, 4u * sizeof(float), vk::BufferUsageFlagBits::eTransferSrc,
                                               "neural_autodiff_contract_results");
    auto latentTexture0 = createLatentImage(device, "neural_contract_latent_texture_0");
    auto latentTexture1 = createLatentImage(device, "neural_contract_latent_texture_1");
    auto viewerOutput = createViewerOutputImage(device, "neural_contract_viewer_comparison_output");
    auto progressViewerOutput = createViewerOutputImage(device, "neural_contract_viewer_progress_output");

    constexpr auto initialControl = std::array<std::uint32_t, 4u>{0u, 1u, kTrainingControlMagic, 0u};
    trainingControl.writeMappedAndFlush(initialControl);

    auto sampler =
        device.pipeline().createSampler(nr::rhi::SlangSamplerDesc{.maxLod = 0.0f}, "neural_contract_mip0_sampler");
    nr::test::require(sampler.valid(), "neural appearance mip0 sampler should be valid");

    auto initializeBindings = prepareBindings(initializePipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindBuffer(root, "gTrainingControl", trainingControl);
        bindBuffer(root, "gModelParameters", modelParameters);
        bindBuffer(root, "gModelMoments", modelMoments);
        bindBuffer(root, "gTrainingLatent", trainingLatent);
        bindBuffer(root, "gLatentMoments", latentMoments);
        bindBuffer(root, "gTrainingStatus", trainingStatus);
    });
    auto targetBindings = prepareBindings(
        targetPipeline, [&](const nr::rhi::ShaderCursor &root) { bindBuffer(root, "gSampleTargets", sampleTargets); });
    auto gradientBindings = prepareBindings(gradientPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindBuffer(root, "gModelParameters", modelParameters);
        bindBuffer(root, "gTrainingLatent", trainingLatent);
        bindBuffer(root, "gSampleTargets", sampleTargets);
        bindBuffer(root, "gSampleGradients", sampleGradients);
        bindBuffer(root, "gSampleTexelIndices", sampleIndices);
        bindBuffer(root, "gSampleMetrics", sampleMetrics);
    });
    auto optimizeBindings = prepareBindings(optimizePipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindBuffer(root, "gSampleGradients", sampleGradients);
        bindBuffer(root, "gSampleTexelIndices", sampleIndices);
        bindBuffer(root, "gSampleMetrics", sampleMetrics);
        bindBuffer(root, "gModelParameters", modelParameters);
        bindBuffer(root, "gModelMoments", modelMoments);
        bindBuffer(root, "gTrainingLatent", trainingLatent);
        bindBuffer(root, "gLatentMoments", latentMoments);
        bindBuffer(root, "gTrainingStatus", trainingStatus);
        bindBuffer(root, "gTrainingControl", trainingControl);
    });
    auto packBindings = prepareBindings(packPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindBuffer(root, "gTrainingLatent", trainingLatent);
        bindImage(root, "gLatentTexture0", latentTexture0, vk::ImageLayout::eGeneral);
        bindImage(root, "gLatentTexture1", latentTexture1, vk::ImageLayout::eGeneral);
    });
    auto viewerBindings = prepareBindings(viewerPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindSampledImage(root, "gLatentTexture0", latentTexture0, sampler.raw());
        bindSampledImage(root, "gLatentTexture1", latentTexture1, sampler.raw());
        bindBuffer(root, "gModelParameters", modelParameters);
        bindBuffer(root, "gTrainingStatus", trainingStatus);
        bindImage(root, "gOutputColor", viewerOutput, vk::ImageLayout::eGeneral);
    });
    nr::test::require(viewerBindings.root["gNeuralAppearance"].setData(NeuralAppearanceViewerPushConstants{}),
                      "headless viewer push constants should bind through reflection");
    auto progressViewerBindings = prepareBindings(viewerPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindSampledImage(root, "gLatentTexture0", latentTexture0, sampler.raw());
        bindSampledImage(root, "gLatentTexture1", latentTexture1, sampler.raw());
        bindBuffer(root, "gModelParameters", poisonedModelParameters);
        bindBuffer(root, "gTrainingStatus", trainingStatus);
        bindImage(root, "gOutputColor", progressViewerOutput, vk::ImageLayout::eGeneral);
    });
    nr::test::require(progressViewerBindings.root["gNeuralAppearance"].setData(NeuralAppearanceViewerPushConstants{
                          .comparisonEnabled = 0u,
                          .errorGain = std::numeric_limits<float>::quiet_NaN(),
                      }),
                      "comparison-disabled viewer push constants should bind through the unchanged 24-byte ABI");
    auto contractBindings = prepareBindings(contractPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindSampledImage(root, "gLatentTexture0", latentTexture0, sampler.raw());
        bindSampledImage(root, "gLatentTexture1", latentTexture1, sampler.raw());
        bindBuffer(root, "gModelParameters", modelParameters);
        bindBuffer(root, "gContractResults", contractResults);
    });
    auto autodiffBindings = prepareBindings(autodiffPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindBuffer(root, "gAutodiffResults", autodiffResults);
    });
    auto qualityBindings = prepareBindings(qualityPipeline, [&](const nr::rhi::ShaderCursor &root) {
        bindSampledImage(root, "gLatentTexture0", latentTexture0, sampler.raw());
        bindSampledImage(root, "gLatentTexture1", latentTexture1, sampler.raw());
        bindBuffer(root, "gModelParameters", modelParameters);
        bindBuffer(root, "gQualityMetrics", qualityMetrics);
    });
    auto const packedImages = std::array<std::reference_wrapper<const nr::rhi::Image>, 2u>{std::cref(latentTexture0),
                                                                                           std::cref(latentTexture1)};

    auto commandPool = nr::rhi::CommandPool{device.device, device.queueManager.compute().queueFamilyIndex(),
                                            vk::CommandPoolCreateFlagBits::eTransient};
    auto commandBuffers = commandPool.allocatePrimary(2u);
    auto const &initialCommand = commandBuffers[0];
    nr::rhi::CommandRecorder::beginPrimary(initialCommand, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    recordDispatch(initialCommand, autodiffPipeline, autodiffBindings, 1u);
    recordComputeReadWriteBarrier(initialCommand);
    recordDispatch(initialCommand, initializePipeline, initializeBindings, kInitializeGroupCount);
    recordComputeReadWriteBarrier(initialCommand);
    auto const initialSamplingPush = NeuralAppearanceGradientPushConstants{.batchSize = kShaderBatchSize};
    setTrainingPushConstants(targetBindings, initialSamplingPush);
    recordDispatch(initialCommand, targetPipeline, targetBindings, kShaderBatchSize);
    recordComputeReadWriteBarrier(initialCommand);
    setTrainingPushConstants(gradientBindings, initialSamplingPush);
    recordDispatch(initialCommand, gradientPipeline, gradientBindings, 2u);
    recordComputeReadWriteBarrier(initialCommand);
    recordImageTransitionForStorageWrite(initialCommand, latentTexture0);
    recordImageTransitionForStorageWrite(initialCommand, latentTexture1);
    recordDispatch(initialCommand, packPipeline, packBindings, kLatentWidth / 8u, kLatentHeight / 8u);
    recordPackToInferenceBarrier(initialCommand, packedImages);
    recordDispatch(initialCommand, qualityPipeline, qualityBindings, kQualityGroupCount);
    nr::rhi::CommandRecorder::end(initialCommand);

    auto initialBatch = nr::rhi::CommandBatch{};
    initialBatch.addCommandBuffer(initialCommand);
    device.queueManager.compute().submit(std::move(initialBatch));

    auto autodiffValues = readbackBuffer<float>(device, autodiffResults);
    requireAutodiffOracle(autodiffValues);
    auto initialParameters = readbackBuffer<float>(device, modelParameters);
    auto initialLatent = readbackBuffer<float>(device, trainingLatent);
    auto initialTargets = readbackBuffer<float>(device, sampleTargets);
    auto initialMetrics = readbackBuffer<float>(device, sampleMetrics);
    auto initialQualityMetrics = readbackBuffer<float>(device, qualityMetrics);
    requireSampleTargets(initialTargets, kSampleTargetChunkCount, kMollificationInitialAngleRadians,
                         "initial 64-sample");
    requireFullBatchMetrics(initialMetrics);
    auto const initialValidationLoss = meanValidationLoss(initialMetrics);

    auto const &trainingCommand = commandBuffers[1];
    nr::rhi::CommandRecorder::beginPrimary(trainingCommand, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    std::ranges::for_each(
        std::views::iota(std::uint32_t{1u}, kTrainingStepCount + 1u), [&](std::uint32_t trainingStep) {
            auto const samplingPush = NeuralAppearanceGradientPushConstants{.trainingStep = trainingStep};
            setTrainingPushConstants(targetBindings, samplingPush);
            recordDispatch(trainingCommand, targetPipeline, targetBindings, kContractBatchSize);
            recordComputeReadWriteBarrier(trainingCommand);
            setTrainingPushConstants(gradientBindings, samplingPush);
            recordDispatch(trainingCommand, gradientPipeline, gradientBindings, 1u);
            recordComputeReadWriteBarrier(trainingCommand);
            setTrainingPushConstants(optimizeBindings,
                                     NeuralAppearanceOptimizePushConstants{.trainingStep = trainingStep});
            recordDispatch(trainingCommand, optimizePipeline, optimizeBindings, kOptimizeGroupCount);
            recordComputeReadWriteBarrier(trainingCommand);
        });

    auto const finalSamplingPush = NeuralAppearanceGradientPushConstants{};
    setTrainingPushConstants(targetBindings, finalSamplingPush);
    recordDispatch(trainingCommand, targetPipeline, targetBindings, kContractBatchSize);
    recordComputeReadWriteBarrier(trainingCommand);
    setTrainingPushConstants(gradientBindings, finalSamplingPush);
    recordDispatch(trainingCommand, gradientPipeline, gradientBindings, 1u);
    recordComputeReadWriteBarrier(trainingCommand);
    recordInferenceToPackBarrier(trainingCommand, packedImages);
    recordDispatch(trainingCommand, packPipeline, packBindings, kLatentWidth / 8u, kLatentHeight / 8u);
    recordPackToInferenceBarrier(trainingCommand, packedImages);
    recordDispatch(trainingCommand, qualityPipeline, qualityBindings, kQualityGroupCount);
    recordDispatch(trainingCommand, contractPipeline, contractBindings, 1u);
    recordImageTransitionForStorageWrite(trainingCommand, viewerOutput);
    recordDispatch(trainingCommand, viewerPipeline, viewerBindings, kViewerWidth / 8u, kViewerHeight / 8u);
    recordImageTransitionForStorageWrite(trainingCommand, progressViewerOutput);
    // NaN model state makes an accidental three-panel neural evaluation observable;
    // the comparison-disabled path must return after writing only the progress image.
    recordDispatch(trainingCommand, viewerPipeline, progressViewerBindings, kViewerWidth / 8u, kViewerHeight / 8u);
    nr::rhi::CommandRecorder::end(trainingCommand);

    auto trainingBatch = nr::rhi::CommandBatch{};
    trainingBatch.addCommandBuffer(trainingCommand);
    device.queueManager.compute().submit(std::move(trainingBatch));

    auto finalParameters = readbackBuffer<float>(device, modelParameters);
    auto finalLatent = readbackBuffer<float>(device, trainingLatent);
    auto finalModelMoments = readbackBuffer<float>(device, modelMoments);
    auto finalLatentMoments = readbackBuffer<float>(device, latentMoments);
    auto finalTargets = readbackBuffer<float>(device, sampleTargets);
    auto finalMetrics = readbackBuffer<float>(device, sampleMetrics);
    auto finalQualityMetrics = readbackBuffer<float>(device, qualityMetrics);
    auto status = readbackBuffer<float>(device, trainingStatus);
    auto control = readbackBuffer<std::uint32_t>(device, trainingControl);
    auto results = readbackBuffer<std::uint32_t>(device, contractResults);
    auto viewerBytes = readbackViewerOutput(device, viewerOutput);
    auto progressViewerBytes = readbackViewerOutput(device, progressViewerOutput);
    auto const finalValidationLoss = meanValidationLoss(finalMetrics);

    requireSampleTargets(finalTargets, kContractBatchSize, kMollificationInitialAngleRadians, "final validation");
    nr::test::require(std::ranges::all_of(finalParameters, [](float value) { return std::isfinite(value); }),
                      "trained model parameters should remain finite");
    nr::test::require(std::ranges::all_of(finalLatent, [](float value) { return std::isfinite(value); }),
                      "trained latent values should remain finite");
    nr::test::require(finalValidationLoss < initialValidationLoss * 0.98f,
                      std::format("fixed GPU validation loss should fall by at least 2%: initial={}, final={}",
                                  initialValidationLoss, finalValidationLoss));
    nr::test::require(changedScalarCount(initialParameters, finalParameters) >= 16u,
                      "GPU optimization should change multiple runtime model parameters");
    nr::test::require(changedFrameWeightScalarCount(initialParameters, finalParameters, 0u) > 0u,
                      "GPU optimization should update learned-frame 0 latent weights");
    nr::test::require(changedFrameWeightScalarCount(initialParameters, finalParameters, 1u) > 0u,
                      "GPU optimization should update learned-frame 1 latent weights");
    auto const optimizedLatentScalarCount = static_cast<std::size_t>(kContractOptimizedLatentTexelCount) * 2u * 4u;
    auto const initialLatentSpan = std::span<const float>{initialLatent};
    auto const finalLatentSpan = std::span<const float>{finalLatent};
    nr::test::require(changedScalarCount(initialLatentSpan.first(optimizedLatentScalarCount),
                                         finalLatentSpan.first(optimizedLatentScalarCount)) >= 32u,
                      "GPU optimization should change multiple mip0 latent scalars after the scaled warmup");
    nr::test::require(changedFrameLatentScalarCount(initialLatent, finalLatent, kContractOptimizedLatentTexelCount) >
                          0u,
                      "GPU optimization should change learned-frame-driving latent lanes after the scaled warmup");
    nr::test::require(changedScalarCount(initialLatentSpan.subspan(optimizedLatentScalarCount),
                                         finalLatentSpan.subspan(optimizedLatentScalarCount)) == 0u,
                      "the GPU-AV smoke optimizer should not mutate latent texels outside its 512-texel coverage");
    requirePackedAdamMoments(finalModelMoments, kParameterChunkCount, 16u, "model");
    requirePackedAdamMoments(finalLatentMoments, kLatentChunkCount, 16u, "latent");
    auto const latentMomentHalfScalarCount = kLatentChunkCount * 4u;
    auto const finalLatentMomentSpan = std::span<const float>{finalLatentMoments};
    auto const firstMomentTail = finalLatentMomentSpan.subspan(
        optimizedLatentScalarCount, latentMomentHalfScalarCount - optimizedLatentScalarCount);
    auto const secondMomentTail =
        finalLatentMomentSpan.subspan(latentMomentHalfScalarCount + optimizedLatentScalarCount,
                                      latentMomentHalfScalarCount - optimizedLatentScalarCount);
    nr::test::require(std::ranges::all_of(firstMomentTail, [](float value) { return value == 0.0f; }) &&
                          std::ranges::all_of(secondMomentTail, [](float value) { return value == 0.0f; }),
                      "latent Adam m/v tails outside the 512-texel GPU-AV smoke coverage should remain zero");
    requireParameterPadding(finalParameters, finalModelMoments);
    requireQualityContract(initialQualityMetrics, finalQualityMetrics);

    nr::test::requireEqual(status.size(), std::size_t{8u});
    nr::test::require(std::ranges::all_of(status, [](float value) { return std::isfinite(value); }) &&
                          status[0] >= 0.0f && status[1] >= 0.0f &&
                          status[2] == static_cast<float>(kTrainingStepCount) && status[3] > 0.5f,
                      std::format("training status should report finite loss telemetry at step {}: "
                                  "[{}, {}, {}, {}]",
                                  kTrainingStepCount, status[0], status[1], status[2], status[3]));
    auto const learningRateProgress =
        static_cast<float>(kTrainingStepCount) / static_cast<float>(kTotalTrainingStepCount);
    auto const expectedModelLearningRate =
        kFinalLearningRate + (kInitialLearningRate - kFinalLearningRate) * 0.5f *
                                 (1.0f + std::cos(std::numbers::pi_v<float> * learningRateProgress));
    auto const mollificationProgress =
        std::min(static_cast<float>(kTrainingStepCount) / static_cast<float>(kMollificationStepCount), 1.0f);
    auto const expectedMollificationAngle =
        kMollificationInitialAngleRadians * 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * mollificationProgress));
    nr::test::require(
        std::abs(status[4] - expectedModelLearningRate) <= 1.0e-7f &&
            std::abs(status[5] - expectedModelLearningRate * kLatentLearningRateScale) <= 1.0e-8f &&
            std::abs(status[6] - expectedMollificationAngle) <= 1.0e-6f &&
            status[7] == static_cast<float>(kContractBatchSize),
        std::format("training status schedule telemetry should report model LR, latent LR, angle, and batch: "
                    "actual=[{}, {}, {}, {}], expected=[{}, {}, {}, {}]",
                    status[4], status[5], status[6], status[7], expectedModelLearningRate,
                    expectedModelLearningRate * kLatentLearningRateScale, expectedMollificationAngle,
                    kContractBatchSize));
    nr::test::requireEqual(control.size(), std::size_t{4u});
    nr::test::require(control[0] == kTrainingStepCount && control[1] == 0u && control[2] == kTrainingControlMagic &&
                          control[3] == 1u,
                      std::format("training control should publish ready NATP step {}: "
                                  "[{}, {}, 0x{:08X}, {}]",
                                  kTrainingStepCount, control[0], control[1], control[2], control[3]));
    nr::test::requireEqual(results.size(), kContractResultNames.size());
    std::ranges::for_each(std::views::iota(std::size_t{0u}, results.size()), [&](std::size_t index) {
        nr::test::require(results[index] == 1u, std::format("neural GPU contract failed: {} (value={})",
                                                            kContractResultNames[index], results[index]));
    });
    requireViewerOutputContract(viewerBytes);
    requireCompletedViewerProgressOutputContract(progressViewerBytes, viewerBytes);

    device.waitIdle();
}

const nr::test::CaseRegistrar neuralAppearanceGpuTrainingContractCase{
    "neural appearance held-out fidelity improves through Vulkan Slang training", [] {
        auto programs = compileNeuralPrograms();
        inspectNeuralPrograms(programs);
        executeNeuralTrainingContract(programs);
    }};
} // namespace
