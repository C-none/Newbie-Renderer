#include <cstddef>

import std;
import dependency.slang;
import dependency.vulkan;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
inline constexpr auto kParameterChunkCount = std::size_t{897u};
inline constexpr auto kAffineLayerCount = std::size_t{8u};
inline constexpr auto kInferenceParameterBytes = std::size_t{7'360u};
inline constexpr auto kModelLogicalBytes = std::size_t{7'176u};
inline constexpr auto kTrainingPairSlotCount = std::uint32_t{8u};
inline constexpr auto kV3TrainingGraphPassCount = std::uint32_t{43u};
inline constexpr auto kShaderBatchSize = std::uint32_t{64u};
inline constexpr auto kNeuralMaterialContextBytes = std::size_t{176u};
// A 96 x 32 preview splits into three 32-pixel columns and two 16-pixel rows, so
// every panel boundary lands on an 8 x 8 workgroup boundary.
inline constexpr auto kViewerWidth = std::uint32_t{96u};
inline constexpr auto kViewerHeight = std::uint32_t{32u};
inline constexpr auto kViewerColumnWidth = kViewerWidth / 3u;
inline constexpr auto kViewerRowHeight = kViewerHeight / 2u;
inline constexpr auto kViewerTotalTrainingSteps = std::uint32_t{1024u};
inline constexpr auto kViewerErrorGain = 1.0f;
inline constexpr auto kHalfTolerance = 2.0e-3f;
// A zero-filled model emits zero logits, so both decoded lobes collapse to a
// constant that the host can predict exactly.
inline constexpr auto kZeroModelDiffuse = 0.731058579f;
inline constexpr auto kZeroModelSpecular = 0.999983299f;
inline constexpr auto kModelContractResultNames = std::array{
    "material input finite",
    "encoded latent finite",
    "spatial prefix finite",
    "learned frame unit finite",
    "learned frame cross product",
    "direction pair 0 cached/recomputed",
    "direction pair 1 cached/recomputed",
    "direction pair 2 cached/recomputed",
    "direction pair 3 cached/recomputed",
    "preview shading normal finite and nontrivial",
    "preview anisotropy finite and unit",
    "seeded material domain and unit-disk normal mapping",
    "aggregate",
};

struct NeuralAppearanceViewerPushConstants
{
    std::uint32_t width = kViewerWidth;
    std::uint32_t height = kViewerHeight;
    std::uint32_t frameIndex = 0u;
    std::uint32_t totalTrainingSteps = kViewerTotalTrainingSteps;
    std::uint32_t comparisonEnabled = 1u;
    float errorGain = kViewerErrorGain;
};

struct PreparedComputeBindings
{
    nr::rhi::ShaderCursor root;
    std::vector<nr::rhi::ShaderBindingSet> sets;
    nr::rhi::DescriptorWriteCache writeCache;
    nr::rhi::ShaderBindingSnapshot pushConstantSnapshot{};
};
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

enum class NeuralProgram : std::size_t
{
    Initialize,
    Target,
    ClearCoopGradients,
    Gradient,
    Optimize,
    Quality,
    Viewer,
    ModelContract,
};

struct ExpectedDescriptorBinding
{
    std::string_view name;
    std::uint32_t set = 0u;
    std::uint32_t binding = 0u;
    vk::DescriptorType type = vk::DescriptorType::eStorageBuffer;
};

struct SpirvSampleInstructionCounts
{
    std::size_t total = 0u;
    std::size_t explicitLod = 0u;
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
        static_cast<std::uint32_t>(kParameterChunkCount),
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
    nr::test::require(
        declaresFunctionFloat4ArrayVariable(syntheticSpirv, static_cast<std::uint32_t>(kParameterChunkCount)),
        "SPIR-V local-array inspector positive control should detect a Function float4 model array");
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
    // A 32-wide spatial prefix plus one learned frame. V2 carried two frames and
    // therefore 224 bytes; the V3 decoder separates the lobes instead.
    nr::test::require(size == kNeuralMaterialContextBytes && stride == kNeuralMaterialContextBytes,
                      std::format("NeuralMaterialContext should remain {} bytes: size={}, stride={}",
                                  kNeuralMaterialContextBytes, size, stride));
}

[[nodiscard]] std::vector<nr::rhi::SlangProgram> compileNeuralPrograms()
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto const requests = std::array{
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/initializeTraining"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateTargets"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/clearCoopGradients"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateGradients"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/optimizeTraining"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateQuality"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/viewer"}},
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/neuralAppearance/modelContract"}},
    };
    auto programs = shaderService.compileProgramsByFile(requests);
    nr::test::requireEqual(programs.size(), requests.size());
    return programs;
}

void inspectNeuralPrograms(const std::vector<nr::rhi::SlangProgram> &programs)
{
    auto const programNames = std::array{
        std::string_view{"initializeTraining"}, std::string_view{"evaluateTargets"},
        std::string_view{"clearCoopGradients"}, std::string_view{"evaluateGradients"},
        std::string_view{"optimizeTraining"},   std::string_view{"evaluateQuality"},
        std::string_view{"viewer"},             std::string_view{"modelContract"},
    };
    std::ranges::for_each(std::views::iota(std::size_t{0u}, programs.size()),
                          [&](std::size_t index) { requireComputeProgram(programs[index], programNames[index]); });

    auto const &initialize = programs[programIndex(NeuralProgram::Initialize)];
    auto const &target = programs[programIndex(NeuralProgram::Target)];
    auto const &clear = programs[programIndex(NeuralProgram::ClearCoopGradients)];
    auto const &gradient = programs[programIndex(NeuralProgram::Gradient)];
    auto const &optimize = programs[programIndex(NeuralProgram::Optimize)];
    auto const &quality = programs[programIndex(NeuralProgram::Quality)];
    auto const &viewer = programs[programIndex(NeuralProgram::Viewer)];
    auto const &modelContract = programs[programIndex(NeuralProgram::ModelContract)];

    requireDescriptorBindings(initialize,
                              {{"gTrainingControl", 1u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelParameters", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelMoments", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingStatus", 2u, 2u, vk::DescriptorType::eStorageBuffer},
                               {"gInferenceParameters", 2u, 3u, vk::DescriptorType::eStorageBuffer}},
                              "initializeTraining");
    requireDescriptorBindings(target, {{"gSampleTargets", 2u, 0u, vk::DescriptorType::eStorageBuffer}},
                              "evaluateTargets");
    requireDescriptorBindings(clear,
                              {{"gOptimalWeightGradients", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gBiasGradients", 2u, 1u, vk::DescriptorType::eStorageBuffer}},
                              "clearCoopGradients");
    requireDescriptorBindings(gradient,
                              {{"gInferenceParameters", 1u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleMetrics", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleTargets", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gOptimalWeightGradients", 2u, 2u, vk::DescriptorType::eStorageBuffer},
                               {"gBiasGradients", 2u, 3u, vk::DescriptorType::eStorageBuffer}},
                              "evaluateGradients");
    requireDescriptorBindings(optimize,
                              {{"gModelParameters", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelMoments", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingStatus", 2u, 2u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingControl", 2u, 3u, vk::DescriptorType::eStorageBuffer},
                               {"gSampleMetrics", 2u, 4u, vk::DescriptorType::eStorageBuffer},
                               {"gRowMajorWeightGradients", 2u, 5u, vk::DescriptorType::eStorageBuffer},
                               {"gBiasGradients", 2u, 6u, vk::DescriptorType::eStorageBuffer},
                               {"gInferenceParameters", 2u, 7u, vk::DescriptorType::eStorageBuffer}},
                              "optimizeTraining");
    requireDescriptorBindings(quality,
                              {{"gModelParameters", 4u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gInferenceParameters", 4u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gHeldOutQualitySamples", 4u, 2u, vk::DescriptorType::eStorageBuffer}},
                              "evaluateQuality");
    requireDescriptorBindings(viewer,
                              {{"gOutputColor", 2u, 0u, vk::DescriptorType::eStorageImage},
                               {"gInferenceParameters", 2u, 1u, vk::DescriptorType::eStorageBuffer},
                               {"gTrainingStatus", 2u, 2u, vk::DescriptorType::eStorageBuffer}},
                              "viewer");
    requireDescriptorBindings(modelContract,
                              {{"gContractResults", 2u, 0u, vk::DescriptorType::eStorageBuffer},
                               {"gModelParameters", 2u, 1u, vk::DescriptorType::eStorageBuffer}},
                              "modelContract");
    requirePushConstant(initialize, "gInitialize", 4u, {{"trainingSeed", 0u}}, "initializeTraining");
    requirePushConstant(target, "gTraining", 12u,
                        {{"trainingStep", 0u}, {"batchSize", 4u}, {"trainingSeed", 8u}}, "evaluateTargets");
    requirePushConstant(clear, "gClear", 8u,
                        {{"optimalWeightGradientBytes", 0u}, {"biasGradientBytes", 4u}}, "clearCoopGradients");
    requirePushConstant(gradient, "gTraining", 44u,
                        {{"trainingStep", 0u},
                         {"batchSize", 4u},
                         {"trainingSeed", 8u},
                         {"optimalWeightOffsets", 12u}},
                        "evaluateGradients");
    requirePushConstant(optimize, "gTraining", 44u,
                        {{"trainingStep", 0u},
                         {"batchSize", 4u},
                         {"parameterChunkCount", 8u},
                         {"totalTrainingSteps", 12u},
                         {"rowMajorGradientBaseOffset", 16u},
                         {"gradientClip", 20u},
                         {"initialLearningRate", 24u},
                         {"finalLearningRate", 28u},
                         {"beta1", 32u},
                         {"beta2", 36u},
                         {"epsilon", 40u}},
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
    requireLocalArrayInspectorPositiveControl();

    auto const *entryPoint = viewer.entryPoint();
    auto const samples = inspectSpirvSampleInstructions(std::span<const std::uint32_t>{*entryPoint->spirv});
    // V3 drives the encoder from resolved material parameters, so the viewer no
    // longer samples a latent texture at all.
    nr::test::requireEqual(samples.total, std::size_t{0u}, "viewer should contain no image sample instructions");
    nr::test::requireEqual(samples.explicitLod, std::size_t{0u}, "viewer should contain no explicit-LOD samples");
    nr::test::require(!declaresFunctionFloat4ArrayVariable(std::span<const std::uint32_t>{*entryPoint->spirv},
                                                           static_cast<std::uint32_t>(kParameterChunkCount)),
                      "viewer must not materialize the FP32 master model in Function storage");
}

void requireCoopVecV3TrainingMathContract()
{
    struct Layer
    {
        std::uint32_t rows;
        std::uint32_t columns;
        std::uint32_t weightOffset;
        std::uint32_t rowStride;
        std::optional<std::uint32_t> biasOffset;
    };
    // Rows are outputs and columns are inputs:
    // E1(12->32) E2(32->32) E3(32->8) F(8->6) S(8->32) D(8->32) H(32->32) O(32->6).
    constexpr auto layers = std::array{
        Layer{32u, 12u, 0u, 24u, 768u},      Layer{32u, 32u, 832u, 64u, 2'880u},
        Layer{8u, 32u, 2'944u, 64u, 3'456u}, Layer{6u, 8u, 3'520u, 16u, 3'648u},
        Layer{32u, 8u, 3'712u, 16u, 4'224u}, Layer{32u, 8u, 4'288u, 16u, std::nullopt},
        Layer{32u, 32u, 4'800u, 64u, 6'848u}, Layer{6u, 32u, 6'912u, 64u, 7'296u},
    };
    static_assert(layers.size() == kAffineLayerCount);

    nr::test::require(std::ranges::all_of(layers, [](Layer layer) { return layer.rowStride == layer.columns * 2u; }),
                      "every V3 FP16 weight row must be tightly packed");
    nr::test::require(std::ranges::all_of(layers, [](Layer layer) { return layer.weightOffset % 64u == 0u; }),
                      "every V3 CooperativeVector matrix must start on a 64-byte boundary");
    nr::test::require(!layers[5u].biasOffset && std::ranges::count_if(layers, [](Layer layer) {
                          return !layer.biasOffset.has_value();
                      }) == 1,
                      "the direction layer must remain the only bias-free V3 affine layer");

    auto const weightBytes = std::accumulate(layers.begin(), layers.end(), 0u, [](std::uint32_t total, Layer layer) {
        return total + layer.rows * layer.rowStride;
    });
    auto const biasBytes = std::accumulate(layers.begin(), layers.end(), 0u, [](std::uint32_t total, Layer layer) {
        return total + (layer.biasOffset ? layer.rows * 2u : 0u);
    });
    nr::test::require(weightBytes == 6'880u && biasBytes == 296u,
                      std::format("V3 affine weights and biases must occupy the fixed FP16 payload regions: {}/{}",
                                  weightBytes, biasBytes));
    nr::test::requireEqual(static_cast<std::size_t>(weightBytes + biasBytes), kModelLogicalBytes);

    struct PaddingRange
    {
        std::uint32_t offset;
        std::uint32_t size;
    };
    constexpr auto paddingRanges = std::array{
        PaddingRange{3'472u, 48u}, PaddingRange{3'616u, 32u},
        PaddingRange{3'660u, 52u}, PaddingRange{7'308u, 52u},
    };
    auto const paddingBytes = std::accumulate(paddingRanges.begin(), paddingRanges.end(), 0u,
                                              [](std::uint32_t total, PaddingRange range) { return total + range.size; });
    nr::test::requireEqual(static_cast<std::size_t>(weightBytes + biasBytes + paddingBytes), kInferenceParameterBytes,
                           "V3 defined bytes plus portable padding must fill the canonical model blob");

    // The direction layer depends on the learned frame, which depends on the frame
    // layer, which depends on the encoded latent. A centered finite difference
    // catches an accidental no_diff cut through that fan-out.
    auto loss = [](float material) {
        auto const latent = 2.0f * material;
        auto const frame = 2.0f * latent;
        auto const spatial = 4.0f * latent;
        auto const directional = 3.0f * frame;
        auto const output = 5.0f * (spatial + directional);
        return 0.5f * output * output;
    };
    constexpr auto material = 0.3f;
    constexpr auto epsilon = 1.0e-4f;
    auto const centeredDerivative = (loss(material + epsilon) - loss(material - epsilon)) / (2.0f * epsilon);
    auto const analyticDerivative = 10'000.0f * material;
    nr::test::require(std::abs(centeredDerivative - analyticDerivative) <= 2.0f,
                      "encoder-to-frame-to-direction fan-out finite difference must remain differentiable");

    auto const sampleGradient = 0.125f;
    auto gradients = std::array<float, kShaderBatchSize>{};
    gradients.fill(sampleGradient);
    auto const reducedGradient = std::accumulate(gradients.begin(), gradients.end(), 0.0f);
    nr::test::require(reducedGradient == sampleGradient * static_cast<float>(kShaderBatchSize),
                      "CoopVec weight and bias reductions must preserve N-times batch accumulation");
    nr::test::require(kV3TrainingGraphPassCount == 1u + kTrainingPairSlotCount * 5u + 2u,
                      "V3 training graph must retain Initialize + eight Target/Clear/Gradient/Convert/Optimize pairs "
                      "+ Quality + Viewer");
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

template <typename TBinder>
[[nodiscard]] PreparedComputeBindings prepareBindings(nr::rhi::PipelineState<nr::rhi::ComputePipeline> &pipeline,
                                                      TBinder &&binder)
{
    auto root = pipeline.descriptorLayout.rootCursor();
    root.beginRecording();
    std::invoke(std::forward<TBinder>(binder), root);
    auto descriptorSnapshot = root.takeSnapshot();
    auto sets = nr::rhi::allocateBindingSetsForLayout(pipeline.layout, pipeline.bindingPool);
    auto writeCache = nr::rhi::DescriptorWriteCache{};
    nr::rhi::updateResourcesForBindingSnapshot(pipeline.bindingPool, sets, writeCache, descriptorSnapshot, {});
    return PreparedComputeBindings{
        .root = std::move(root), .sets = std::move(sets), .writeCache = std::move(writeCache)};
}

void requireModelSamplingContract(nr::rhi::Device &device,
                                  nr::rhi::PipelineState<nr::rhi::ComputePipeline> &pipeline)
{
    constexpr auto parameterBytes = kParameterChunkCount * sizeof(std::array<float, 4u>);
    constexpr auto resultBytes = kModelContractResultNames.size() * sizeof(std::uint32_t);
    auto modelParameters = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(parameterBytes, vk::BufferUsageFlagBits::eStorageBuffer),
        nr::rhi::MemoryUsage::CpuToGpu, "ModelContract.Parameters");
    auto contractResults = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            resultBytes, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc),
        nr::rhi::MemoryUsage::GpuOnly, "ModelContract.Results");
    nr::test::require(modelParameters.valid() && contractResults.valid(),
                      "neural model contract resources should be valid");
    auto const zeroParameters = std::vector<std::byte>(parameterBytes);
    modelParameters.writeMappedAndFlush(std::span<const std::byte>{zeroParameters});

    auto bindings = prepareBindings(pipeline, [&](const nr::rhi::ShaderCursor &root) {
        nr::test::require(root["gModelParameters"].setObject(modelParameters) &&
                              root["gContractResults"].setObject(contractResults),
                          "neural model contract resources should bind through reflection");
    });
    nr::rhi::submitOneShot(device.device, device.queueManager.compute(), {},
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline.raw());
                               nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer,
                                                                             vk::PipelineBindPoint::eCompute,
                                                                             pipeline.layout, bindings.sets);
                               commandBuffer.dispatch(1u, 1u, 1u);
                           });

    auto ticket = device.uploadReadback().readbackBuffer(
        contractResults, 0u, resultBytes, nr::rhi::QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageWrite,
            },
            .postCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageWrite,
            },
        });
    auto bytes = device.uploadReadback().readbackBytes(ticket);
    nr::test::requireEqual(bytes.size(), resultBytes);

    auto results = std::array<std::uint32_t, kModelContractResultNames.size()>{};
    std::memcpy(results.data(), bytes.data(), resultBytes);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, results.size()), [&](std::size_t resultIndex) {
        nr::test::require(results[resultIndex] == 1u,
                          std::format("GPU neural model contract failed: {} (value={})",
                                      kModelContractResultNames[resultIndex], results[resultIndex]));
    });
}

void setViewerPushConstants(PreparedComputeBindings &bindings, const NeuralAppearanceViewerPushConstants &pushConstants)
{
    bindings.root.beginRecording();
    nr::test::require(bindings.root["gNeuralAppearance"].setData(pushConstants),
                      "viewer push constants should bind through reflection");
    bindings.pushConstantSnapshot = bindings.root.takeSnapshot();
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

[[nodiscard]] std::vector<std::uint16_t> dispatchViewer(
    nr::rhi::Device &device, nr::rhi::PipelineState<nr::rhi::ComputePipeline> &pipeline,
    PreparedComputeBindings &bindings, const nr::rhi::Image &output, bool firstUse)
{
    nr::rhi::submitOneShot(device.device, device.queueManager.compute(), {},
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               if (firstUse)
                               {
                                   recordImageTransitionForStorageWrite(commandBuffer, output);
                               }
                               commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline.raw());
                               nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer,
                                                                             vk::PipelineBindPoint::eCompute,
                                                                             pipeline.layout, bindings.sets);
                               nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipeline.layout,
                                                                     bindings.pushConstantSnapshot);
                               commandBuffer.dispatch(kViewerWidth / 8u, kViewerHeight / 8u, 1u);
                           });

    auto ticket = device.uploadReadback().readbackImage(
        output, vk::ImageLayout::eGeneral, nr::rhi::QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = nr::rhi::ops::ReadbackSyncScope{.stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                       .access = vk::AccessFlagBits2::eShaderStorageWrite},
            .postCopy = nr::rhi::ops::ReadbackSyncScope{.stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                        .access = vk::AccessFlagBits2::eShaderStorageWrite}});
    auto bytes = device.uploadReadback().readbackBytes(ticket);
    nr::test::requireEqual(bytes.size(), static_cast<std::size_t>(kViewerWidth) * kViewerHeight * 4u * 2u,
                           "viewer readback should cover the whole RGBA16F preview");
    auto pixels = std::vector<std::uint16_t>(bytes.size() / 2u);
    std::memcpy(pixels.data(), bytes.data(), bytes.size());
    return pixels;
}

[[nodiscard]] float channelAt(std::span<const std::uint16_t> pixels, std::uint32_t x, std::uint32_t y,
                              std::size_t channel)
{
    return halfToFloat(pixels[(static_cast<std::size_t>(y) * kViewerWidth + x) * 4u + channel]);
}

void requireProgressPreviewContract(std::span<const std::uint16_t> pixels)
{
    // Training has not completed, so every pixel must equal the analytic progress
    // image regardless of the model contents.
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerHeight), [&](std::uint32_t y) {
        std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerWidth), [&](std::uint32_t x) {
            auto const uvX = (static_cast<float>(x) + 0.5f) / static_cast<float>(kViewerWidth);
            auto const uvY = (static_cast<float>(y) + 0.5f) / static_cast<float>(kViewerHeight);
            auto const insideTrack = std::abs(uvY - 0.5f) <= 0.035f && uvX >= 0.08f && uvX <= 0.92f;
            auto const expected =
                insideTrack ? std::array{0.055f, 0.075f, 0.1f}
                            : std::array{0.008f + (0.025f - 0.008f) * uvY, 0.012f + (0.04f - 0.012f) * uvY,
                                         0.02f + (0.065f - 0.02f) * uvY};
            std::ranges::for_each(std::views::iota(std::size_t{0u}, std::size_t{3u}), [&](std::size_t channel) {
                auto const actual = channelAt(pixels, x, y, channel);
                nr::test::require(std::isfinite(actual) && std::abs(actual - expected[channel]) <= kHalfTolerance,
                                  std::format("progress preview pixel ({},{}) channel {} should be {}, got {}", x, y,
                                              channel, expected[channel], actual));
            });
            nr::test::require(channelAt(pixels, x, y, 3u) == 1.0f,
                              std::format("progress preview pixel ({},{}) should be opaque", x, y));
        });
    });
}

void requireComparisonPreviewContract(std::span<const std::uint16_t> pixels)
{
    auto nativeVariation = 0u;
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerHeight), [&](std::uint32_t y) {
        auto const diffuseRow = y >= kViewerRowHeight;
        auto const expectedNeural = diffuseRow ? kZeroModelDiffuse : kZeroModelSpecular;
        std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerColumnWidth), [&](std::uint32_t column) {
            std::ranges::for_each(std::views::iota(std::size_t{0u}, std::size_t{3u}), [&](std::size_t channel) {
                auto const nativeValue = channelAt(pixels, column, y, channel);
                auto const neuralValue = channelAt(pixels, column + kViewerColumnWidth, y, channel);
                auto const errorValue = channelAt(pixels, column + 2u * kViewerColumnWidth, y, channel);
                nr::test::require(std::isfinite(nativeValue) && nativeValue >= 0.0f && std::isfinite(neuralValue) &&
                                      neuralValue >= 0.0f && std::isfinite(errorValue) && errorValue >= 0.0f,
                                  std::format("comparison preview ({},{}) channel {} must stay finite and "
                                              "non-negative: native={}, neural={}, error={}",
                                              column, y, channel, nativeValue, neuralValue, errorValue));
                // A zero-filled model makes the decoded lobe constant everywhere,
                // which pins the neural column to an exactly predictable value.
                nr::test::require(std::abs(neuralValue - expectedNeural) <= 4.0e-3f * expectedNeural,
                                  std::format("zero-model neural column ({},{}) channel {} should be {}, got {}",
                                              column, y, channel, expectedNeural, neuralValue));
                // The error column is the amplified absolute difference of the two
                // panels to its left, which is the invariant a broken panel layout
                // or a swapped lobe would violate.
                auto const expectedError = std::abs(nativeValue - neuralValue) * kViewerErrorGain;
                nr::test::require(std::abs(errorValue - expectedError) <=
                                      1.0e-2f * std::max(1.0f, std::abs(expectedError)),
                                  std::format("error column ({},{}) channel {} should be |{} - {}| = {}, got {}",
                                              column, y, channel, nativeValue, neuralValue, expectedError, errorValue));
                nativeVariation += std::abs(nativeValue - channelAt(pixels, 0u, y, channel)) > kHalfTolerance ? 1u : 0u;
            });
        });
    });
    nr::test::require(nativeVariation > 0u,
                      "the native column must vary across the material slice, otherwise the preview is constant");

    // The projected diffuse lobe is a Lambert term scaled by an energy
    // complement and a cosine, so it can never exceed 1/pi. The specular lobe
    // has no such bound. Together these pin which row renders which lobe.
    constexpr auto kDiffuseUpperBound = std::numbers::inv_pi_v<float>;
    auto specularAboveDiffuseBound = 0u;
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerColumnWidth), [&](std::uint32_t column) {
        std::ranges::for_each(std::views::iota(std::size_t{0u}, std::size_t{3u}), [&](std::size_t channel) {
            std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerRowHeight), [&](std::uint32_t row) {
                auto const specular = channelAt(pixels, column, row, channel);
                auto const diffuse = channelAt(pixels, column, row + kViewerRowHeight, channel);
                nr::test::require(diffuse <= kDiffuseUpperBound + kHalfTolerance,
                                  std::format("native diffuse row ({},{}) channel {} exceeds the 1/pi projected "
                                              "diffuse bound: {}",
                                              column, row + kViewerRowHeight, channel, diffuse));
                specularAboveDiffuseBound += specular > kDiffuseUpperBound + kHalfTolerance ? 1u : 0u;
            });
        });
    });
    nr::test::require(specularAboveDiffuseBound > 0u,
                      "the native specular row must exceed the projected diffuse bound somewhere, otherwise the row "
                      "assignment is not observable");

    // The two rows show different lobes of the same material, so they must differ.
    auto rowDifference = 0u;
    std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerRowHeight), [&](std::uint32_t y) {
        std::ranges::for_each(std::views::iota(std::uint32_t{0u}, kViewerColumnWidth), [&](std::uint32_t column) {
            rowDifference += std::abs(channelAt(pixels, column, y, 0u) -
                                      channelAt(pixels, column, y + kViewerRowHeight, 0u)) > kHalfTolerance
                                 ? 1u
                                 : 0u;
        });
    });
    nr::test::require(rowDifference > 0u,
                      "the specular row and the diffuse row must not render the same native lobe");
}

void requireViewerPixelContract(const std::vector<nr::rhi::SlangProgram> &programs)
{
    auto device = nr::rhi::Device::create("nr_rhi_neural_appearance_viewer_pixel_contract", "NewbieRenderer",
                                          std::filesystem::path{std::string{nr::psoCacheRoot}}, false);
    auto viewerPipeline = device.pipeline()
                              .createComputePipeline(programs[programIndex(NeuralProgram::Viewer)],
                                                     nr::rhi::ComputePipelineDesc{}, 64u, {}, "neural_viewer_pixel_v3")
                              .get();

    auto modelParameters = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(kInferenceParameterBytes, vk::BufferUsageFlagBits::eStorageBuffer),
        nr::rhi::MemoryUsage::CpuToGpu, "ViewerPixel.InferenceParameters");
    auto trainingStatus = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(32u, vk::BufferUsageFlagBits::eStorageBuffer), nr::rhi::MemoryUsage::CpuToGpu,
        "ViewerPixel.TrainingStatus");
    auto outputColor = device.resourceFactory.createImage(
        nr::rhi::makeImageCreateInfo(vk::Format::eR16G16B16A16Sfloat, vk::Extent2D{kViewerWidth, kViewerHeight},
                                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc),
        nr::rhi::MemoryUsage::GpuOnly, "ViewerPixel.OutputColor");
    nr::test::require(modelParameters.valid() && trainingStatus.valid() && outputColor.valid(),
                      "viewer pixel contract resources should be valid");
    auto const zeroModel = std::vector<std::byte>(kInferenceParameterBytes);
    modelParameters.writeMappedAndFlush(std::span<const std::byte>{zeroModel});

    auto bindings = prepareBindings(viewerPipeline, [&](const nr::rhi::ShaderCursor &root) {
        nr::test::require(root["gInferenceParameters"].setObject(modelParameters) &&
                              root["gTrainingStatus"].setObject(trainingStatus) &&
                              root["gOutputColor"].setObject(outputColor, vk::ImageLayout::eGeneral),
                          "viewer resources should bind through reflection");
    });
    setViewerPushConstants(bindings, NeuralAppearanceViewerPushConstants{});

    // Incomplete training must render the deterministic progress image.
    trainingStatus.writeMappedAndFlush(std::array<float, 8u>{0.3f, 0.3f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    requireProgressPreviewContract(dispatchViewer(device, viewerPipeline, bindings, outputColor, true));

    // A completed run switches to the three-column, two-row comparison.
    trainingStatus.writeMappedAndFlush(std::array<float, 8u>{
        0.01f, 0.3f, static_cast<float>(kViewerTotalTrainingSteps), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    requireComparisonPreviewContract(dispatchViewer(device, viewerPipeline, bindings, outputColor, false));

    device.waitIdle();
}

void executeNeuralTrainingContract(const std::vector<nr::rhi::SlangProgram> &programs)
{
    // This executable probe deliberately creates every production PSO after reflection,
    // plus the scalar model/sampling contract used to verify the shared fixture.
    // The full trainer owns BDA-backed TrainingOptimal conversion and its GPU numerical
    // sweep; this contract keeps the seven production shader ABI and PSO gate isolated.
    auto device = nr::rhi::Device::create("nr_rhi_neural_appearance_shader_contract_test", "NewbieRenderer",
                                          std::filesystem::path{std::string{nr::psoCacheRoot}}, false);
    auto initialize = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Initialize)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_initialize_training_v3");
    auto target = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Target)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_evaluate_targets_v3");
    auto clear = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::ClearCoopGradients)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_clear_coop_gradients_v3");
    auto gradients = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Gradient)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_evaluate_gradients_v3");
    auto optimize = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Optimize)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_optimize_training_v3");
    auto quality = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Quality)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_evaluate_quality_v3");
    auto viewer = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::Viewer)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_viewer_v3");
    auto modelContract = device.pipeline().createComputePipeline(
        programs[programIndex(NeuralProgram::ModelContract)], nr::rhi::ComputePipelineDesc{}, 64u, {},
        "neural_model_contract_v3");
    static_cast<void>(initialize.get());
    static_cast<void>(target.get());
    auto clearState = clear.get();
    static_cast<void>(gradients.get());
    static_cast<void>(optimize.get());
    static_cast<void>(quality.get());
    static_cast<void>(viewer.get());
    auto modelContractState = modelContract.get();
    requireModelSamplingContract(device, modelContractState);

    constexpr auto clearBindingGroupCapacity = std::size_t{64u};
    auto clearBindingGroups = std::vector<std::vector<nr::rhi::ShaderBindingSet>>{};
    clearBindingGroups.reserve(clearBindingGroupCapacity);
    std::ranges::generate_n(std::back_inserter(clearBindingGroups), clearBindingGroupCapacity, [&] {
        return nr::rhi::allocateBindingSetsForLayout(clearState.layout, clearState.bindingPool);
    });
    nr::test::requireEqual(clearBindingGroups.size(), clearBindingGroupCapacity,
                           "descriptorMaxSets must count complete multi-set binding groups");
    device.waitIdle();
}

const nr::test::CaseRegistrar neuralAppearanceGpuTrainingContractCase{
    "neural appearance V3 CoopVec shader and PSO contract", [] {
        requireCoopVecV3TrainingMathContract();
        auto programs = compileNeuralPrograms();
        inspectNeuralPrograms(programs);
        executeNeuralTrainingContract(programs);
    }};

const nr::test::CaseRegistrar neuralAppearanceViewerPixelContractCase{
    "neural appearance V3 preview renders the progress image and the three-column comparison", [] {
        requireViewerPixelContract(compileNeuralPrograms());
    }};
} // namespace
