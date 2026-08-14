module nr.renderPasses;

import dependency.vulkan;
import :neuralAppearance;
import :neuralAppearanceQuality;
import nr.neuralAppearanceAsset;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
inline constexpr auto kLatentExtent = vk::Extent2D{64u, 64u};
inline constexpr auto kImageThreadGroupSize = 8u;
inline constexpr auto kGradientThreadGroupSize = 32u;
inline constexpr auto kTrainingThreadGroupSize = 64u;
inline constexpr auto kTrainingPairSlotCount = 8u;
inline constexpr auto kTotalTrainingStepCount = 32768u;
inline constexpr auto kTrainingBatchSize = 64u;
inline constexpr auto kHeldOutQualitySamplesPerStratum = 64u;
inline constexpr auto kHeldOutQualityStratumCount = 3u;
inline constexpr auto kHeldOutQualitySampleCount =
    kHeldOutQualitySamplesPerStratum * kHeldOutQualityStratumCount;
inline constexpr auto kParameterChunkCount = 484u;
inline constexpr auto kLatentChunkCount = kLatentExtent.width * kLatentExtent.height * 2u;
inline constexpr auto kLatentTexelCount = kLatentExtent.width * kLatentExtent.height;
inline constexpr auto kGradientChunksPerSample = 492u;
inline constexpr auto kMollificationSampleCount = 256u;
inline constexpr auto kMollificationStepCount = 2048u;
inline constexpr auto kLatentWarmupStepCount = 2048u;
inline constexpr auto kInitialLearningRate = 0.001f;
inline constexpr auto kFinalLearningRate = 0.0001f;
inline constexpr auto kLatentLearningRateScale = 0.1f;
inline constexpr auto kAdamBeta1 = 0.9f;
inline constexpr auto kAdamBeta2 = 0.999f;
inline constexpr auto kAdamEpsilon = 1.0e-7f;
inline constexpr auto kInitialMollificationAngleRadians = std::numbers::pi_v<float> / 18.0f;
inline constexpr auto kTrainingSchemaMagic = 0x4E415450u;
inline constexpr auto kTrainingCheckpointMagic = 0x4E415443u;
inline constexpr auto kTrainingCheckpointVersion = 2u;

inline constexpr auto kParameterBufferBytes =
    static_cast<vk::DeviceSize>(kParameterChunkCount) * sizeof(std::array<float, 4u>);
inline constexpr auto kInferenceParameterBufferBytes = vk::DeviceSize{3968u};
inline constexpr auto kLatentBufferBytes =
    static_cast<vk::DeviceSize>(kLatentChunkCount) * sizeof(std::array<float, 4u>);
inline constexpr auto kInferenceLatentBufferBytes = vk::DeviceSize{65536u};
inline constexpr auto kModelMomentBufferBytes = 2u * kParameterBufferBytes;
inline constexpr auto kLatentMomentBufferBytes = 2u * kLatentBufferBytes;
inline constexpr auto kSampleGradientBufferBytes =
    static_cast<vk::DeviceSize>(kTrainingBatchSize) * kGradientChunksPerSample * sizeof(std::array<float, 4u>);
inline constexpr auto kSampleTexelIndexBufferBytes =
    static_cast<vk::DeviceSize>(kTrainingBatchSize) * sizeof(std::array<std::uint32_t, 4u>);
inline constexpr auto kSampleMetricBufferBytes =
    static_cast<vk::DeviceSize>(kTrainingBatchSize) * sizeof(std::array<float, 4u>);
inline constexpr auto kSampleTargetBufferBytes =
    static_cast<vk::DeviceSize>(kTrainingBatchSize) * sizeof(std::array<float, 4u>);
inline constexpr auto kTrainingStatusBufferBytes = 2u * sizeof(std::array<float, 4u>);
inline constexpr auto kTrainingControlBufferBytes = sizeof(std::array<std::uint32_t, 4u>);
inline constexpr auto kHeldOutQualitySampleBufferBytes =
    static_cast<vk::DeviceSize>(kHeldOutQualitySampleCount) * sizeof(std::array<float, 4u>);

static_assert(kParameterBufferBytes == 7744u);
static_assert(kInferenceParameterBufferBytes == 3968u);
static_assert(kLatentBufferBytes == 131072u);
static_assert(kInferenceLatentBufferBytes == 65536u);
static_assert(kModelMomentBufferBytes == 15488u);
static_assert(kLatentMomentBufferBytes == 262144u);
static_assert(kSampleGradientBufferBytes == 503808u);
static_assert(kSampleTexelIndexBufferBytes == 1024u);
static_assert(kSampleMetricBufferBytes == 1024u);
static_assert(kSampleTargetBufferBytes == 1024u);
static_assert(kTrainingStatusBufferBytes == 32u);
static_assert(kTrainingControlBufferBytes == 16u);
static_assert(kHeldOutQualitySampleBufferBytes == 3072u);

struct NeuralAppearanceGradientPushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = 0u;
    std::uint32_t latentWidth = kLatentExtent.width;
    std::uint32_t latentHeight = kLatentExtent.height;
    std::uint32_t mollificationSampleCount = kMollificationSampleCount;
    std::uint32_t mollificationStepCount = kMollificationStepCount;
    float initialAngleRadians = kInitialMollificationAngleRadians;
    float padding = 0.0f;
    std::array<std::uint32_t, 5u> optimalWeightOffsets{};
};

struct NeuralAppearanceTargetPushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = 0u;
    std::uint32_t latentWidth = kLatentExtent.width;
    std::uint32_t latentHeight = kLatentExtent.height;
    std::uint32_t mollificationSampleCount = kMollificationSampleCount;
    std::uint32_t mollificationStepCount = kMollificationStepCount;
    float initialAngleRadians = kInitialMollificationAngleRadians;
    float padding = 0.0f;
};

struct NeuralAppearanceClearCoopGradientsPushConstants
{
    std::uint32_t optimalWeightGradientBytes = 0u;
    std::uint32_t rowMajorWeightGradientBytes = static_cast<std::uint32_t>(kInferenceParameterBufferBytes);
    std::uint32_t biasGradientBytes = static_cast<std::uint32_t>(kInferenceParameterBufferBytes);
    std::uint32_t padding = 0u;
};

struct NeuralAppearanceOptimizePushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = 0u;
    std::uint32_t parameterChunkCount = kParameterChunkCount;
    std::uint32_t latentTexelCount = kLatentTexelCount;
    std::uint32_t totalTrainingSteps = kTotalTrainingStepCount;
    std::uint32_t latentWarmupSteps = kLatentWarmupStepCount;
    float initialLearningRate = kInitialLearningRate;
    float finalLearningRate = kFinalLearningRate;
    float latentLearningRateScale = kLatentLearningRateScale;
    float beta1 = kAdamBeta1;
    float beta2 = kAdamBeta2;
    float epsilon = kAdamEpsilon;
};

struct NeuralAppearancePushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t frameIndex = 0u;
    std::uint32_t totalTrainingSteps = kTotalTrainingStepCount;
    std::uint32_t comparisonEnabled = 1u;
    float errorGain = 8.0f;
};

struct NeuralAppearanceTrainingCheckpointHeader
{
    std::uint32_t magic = kTrainingCheckpointMagic;
    std::uint32_t version = kTrainingCheckpointVersion;
    std::uint32_t latentWidth = kLatentExtent.width;
    std::uint32_t latentHeight = kLatentExtent.height;
    std::uint32_t latentChannelCount = 8u;
    std::uint32_t parameterChunkCount = kParameterChunkCount;
    std::uint32_t modelMomentChunkCount = 2u * kParameterChunkCount;
    std::uint32_t latentChunkCount = kLatentChunkCount;
    std::uint32_t latentMomentChunkCount = 2u * kLatentChunkCount;
    std::uint32_t statusChunkCount = 2u;
    std::uint32_t controlWordCount = 4u;
    std::uint32_t totalTrainingStepCount = kTotalTrainingStepCount;
    std::uint32_t completedTrainingStep = 0u;
};

static_assert(sizeof(NeuralAppearanceGradientPushConstants) == 52u);
static_assert(sizeof(NeuralAppearanceTargetPushConstants) == 32u);
static_assert(sizeof(NeuralAppearanceClearCoopGradientsPushConstants) == 16u);
static_assert(sizeof(NeuralAppearanceOptimizePushConstants) == 48u);
static_assert(sizeof(NeuralAppearancePushConstants) == 24u);
static_assert(sizeof(NeuralAppearanceTrainingCheckpointHeader) == 52u);
static_assert(sizeof(NeuralAppearanceGradientPushConstants) <= nr::rhi::kMaxPushConstantBytes);
static_assert(sizeof(NeuralAppearanceOptimizePushConstants) <= nr::rhi::kMaxPushConstantBytes);
static_assert(sizeof(NeuralAppearancePushConstants) <= nr::rhi::kMaxPushConstantBytes);

struct NeuralAppearanceRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> initializePipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> targetPipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> clearCoopGradientsPipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> gradientPipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> optimizePipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> packPipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> viewerPipeline{};

    nr::rhi::Buffer modelParameters{};
    nr::rhi::Buffer modelMoments{};
    nr::rhi::Buffer trainingLatent{};
    nr::rhi::Buffer latentMoments{};
    nr::rhi::Buffer inferenceParameters{};
    nr::rhi::Buffer inferenceLatent{};
    nr::rhi::Buffer optimalWeightGradients{};
    nr::rhi::Buffer rowMajorWeightGradients{};
    nr::rhi::Buffer biasGradients{};
    nr::rhi::Buffer trainingStatus{};
    nr::rhi::Buffer trainingControl{};
    nr::rhi::Buffer heldOutQualitySamples{};

    nr::renderer::RetainedBufferState modelParameterState{};
    nr::renderer::RetainedBufferState modelMomentState{};
    nr::renderer::RetainedBufferState trainingLatentState{};
    nr::renderer::RetainedBufferState latentMomentState{};
    nr::renderer::RetainedBufferState inferenceParameterState{};
    nr::renderer::RetainedBufferState inferenceLatentState{};
    nr::renderer::RetainedBufferState optimalWeightGradientState{};
    nr::renderer::RetainedBufferState rowMajorWeightGradientState{};
    nr::renderer::RetainedBufferState biasGradientState{};
    nr::renderer::RetainedBufferState trainingStatusState{};
    nr::renderer::RetainedBufferState trainingControlState{};
    nr::renderer::RetainedBufferState heldOutQualitySampleState{};

    std::uint32_t nextTrainingStep = 1u;
    std::uint64_t lastDisplayOrdinal = 0u;
    std::array<nr::rhi::CooperativeVectorMatrixDesc, 5u> cooperativeGradientDescs{};
    std::array<nr::rhi::CooperativeVectorMatrixDesc, 5u> rowMajorGradientDescs{};
    std::array<nr::rhi::CooperativeVectorMatrixLayoutSize, 5u> cooperativeGradientLayoutSizes{};
    std::array<nr::rhi::CooperativeVectorMatrixLayoutSize, 5u> rowMajorGradientLayoutSizes{};
    std::array<vk::DeviceSize, 5u> cooperativeGradientOffsets{};
    vk::DeviceSize optimalWeightGradientBytes = 0u;
};

struct NeuralAppearanceTrainingCheckpoint
{
    NeuralAppearanceTrainingCheckpointHeader header{};
    std::vector<std::byte> modelParameters{};
    std::vector<std::byte> modelMoments{};
    std::vector<std::byte> trainingLatent{};
    std::vector<std::byte> latentMoments{};
    std::array<float, 8u> trainingStatus{};
    std::array<std::uint32_t, 4u> trainingControl{};
};

inline constexpr auto kTrainingCheckpointFileBytes =
    static_cast<std::uintmax_t>(sizeof(NeuralAppearanceTrainingCheckpointHeader)) + kParameterBufferBytes +
    kModelMomentBufferBytes + kLatentBufferBytes + kLatentMomentBufferBytes + kTrainingStatusBufferBytes +
    kTrainingControlBufferBytes;

static_assert(kTrainingCheckpointFileBytes == 416548u);

[[nodiscard]] std::array<std::filesystem::path, 2u> trainingCheckpointSlotPaths(const std::filesystem::path &basePath)
{
    auto paths = std::array{basePath, basePath};
    paths[0] += ".0";
    paths[1] += ".1";
    return paths;
}

[[nodiscard]] std::mutex &trainingCheckpointFileMutex()
{
    static auto mutex = std::mutex{};
    return mutex;
}

[[nodiscard]] bool pathsReferToSameFile(const std::filesystem::path &lhs, const std::filesystem::path &rhs)
{
    if (rhs.empty())
    {
        return false;
    }
    if (lhs.lexically_normal() == rhs.lexically_normal())
    {
        return true;
    }

    auto error = std::error_code{};
    auto const equivalent = std::filesystem::equivalent(lhs, rhs, error);
    return !error && equivalent;
}

void reportCheckpointWarning(const std::filesystem::path &path, std::string_view detail)
{
    nr::nrLog<nr::LogLevel::warning, "NEURAL-CHECKPOINT">("NeuralAppearance checkpoint '{}': {}", path.generic_string(),
                                                          detail);
}

[[nodiscard]] bool readBytes(std::ifstream &input, std::span<std::byte> bytes)
{
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return input.good();
}

[[nodiscard]] bool writeBytes(std::ofstream &output, std::span<const std::byte> bytes)
{
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

[[nodiscard]] bool finiteFp32Bytes(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() % sizeof(float) != 0u)
    {
        return false;
    }
    return std::ranges::all_of(std::views::iota(std::size_t{0u}, bytes.size() / sizeof(float)),
                               [&](std::size_t scalarIndex) {
                                   auto value = float{};
                                   std::memcpy(&value, bytes.data() + scalarIndex * sizeof(float), sizeof(float));
                                   return std::isfinite(value);
                               });
}

[[nodiscard]] bool trainingCheckpointHeaderValid(const NeuralAppearanceTrainingCheckpointHeader &header) noexcept
{
    // Version 2 checkpoints are emitted only after the GPU completes at least one training step. Step zero still
    // requires the initialization pass and therefore cannot be restored as a post-initialization snapshot.
    return header.magic == kTrainingCheckpointMagic && header.version == kTrainingCheckpointVersion &&
           header.latentWidth == kLatentExtent.width && header.latentHeight == kLatentExtent.height &&
           header.latentChannelCount == 8u && header.parameterChunkCount == kParameterChunkCount &&
           header.modelMomentChunkCount == 2u * kParameterChunkCount && header.latentChunkCount == kLatentChunkCount &&
           header.latentMomentChunkCount == 2u * kLatentChunkCount && header.statusChunkCount == 2u &&
           header.controlWordCount == 4u && header.totalTrainingStepCount == kTotalTrainingStepCount &&
           header.completedTrainingStep > 0u && header.completedTrainingStep <= kTotalTrainingStepCount;
}

[[nodiscard]] bool trainingCheckpointPayloadValid(const NeuralAppearanceTrainingCheckpoint &checkpoint) noexcept
{
    auto const completedStep = checkpoint.header.completedTrainingStep;
    auto const controlFlagsValid = checkpoint.trainingControl[1] == 0u && checkpoint.trainingControl[3] == 1u;
    return trainingCheckpointHeaderValid(checkpoint.header) &&
           checkpoint.modelParameters.size() == kParameterBufferBytes &&
           checkpoint.modelMoments.size() == kModelMomentBufferBytes &&
           checkpoint.trainingLatent.size() == kLatentBufferBytes &&
           checkpoint.latentMoments.size() == kLatentMomentBufferBytes && finiteFp32Bytes(checkpoint.modelParameters) &&
           finiteFp32Bytes(checkpoint.modelMoments) && finiteFp32Bytes(checkpoint.trainingLatent) &&
           finiteFp32Bytes(checkpoint.latentMoments) &&
           std::ranges::all_of(checkpoint.trainingStatus, [](float value) { return std::isfinite(value); }) &&
           checkpoint.trainingStatus[2] == static_cast<float>(completedStep) && checkpoint.trainingStatus[3] > 0.5f &&
           checkpoint.trainingControl[0] == completedStep && checkpoint.trainingControl[2] == kTrainingSchemaMagic &&
           controlFlagsValid;
}

[[nodiscard]] std::optional<NeuralAppearanceTrainingCheckpoint> readTrainingCheckpointSlot(
    const std::filesystem::path &path, bool reportInvalid)
{
    auto error = std::error_code{};
    auto const exists = std::filesystem::exists(path, error);
    if (error)
    {
        if (reportInvalid)
        {
            reportCheckpointWarning(path, error.message());
        }
        return std::nullopt;
    }
    if (!exists)
    {
        return std::nullopt;
    }

    auto const fileBytes = std::filesystem::file_size(path, error);
    if (error || fileBytes != kTrainingCheckpointFileBytes)
    {
        if (reportInvalid)
        {
            reportCheckpointWarning(path, error ? error.message() : "file size does not match the checkpoint ABI");
        }
        return std::nullopt;
    }

    auto input = std::ifstream{path, std::ios::binary};
    if (!input.is_open())
    {
        if (reportInvalid)
        {
            reportCheckpointWarning(path, "failed to open for reading");
        }
        return std::nullopt;
    }

    auto checkpoint = NeuralAppearanceTrainingCheckpoint{};
    if (!readBytes(input, std::as_writable_bytes(std::span{&checkpoint.header, 1u})) ||
        !trainingCheckpointHeaderValid(checkpoint.header))
    {
        if (reportInvalid)
        {
            reportCheckpointWarning(path, "header magic, version, dimensions, or counts are invalid");
        }
        return std::nullopt;
    }

    checkpoint.modelParameters.resize(static_cast<std::size_t>(kParameterBufferBytes));
    checkpoint.modelMoments.resize(static_cast<std::size_t>(kModelMomentBufferBytes));
    checkpoint.trainingLatent.resize(static_cast<std::size_t>(kLatentBufferBytes));
    checkpoint.latentMoments.resize(static_cast<std::size_t>(kLatentMomentBufferBytes));
    if (!readBytes(input, checkpoint.modelParameters) || !readBytes(input, checkpoint.modelMoments) ||
        !readBytes(input, checkpoint.trainingLatent) || !readBytes(input, checkpoint.latentMoments) ||
        !readBytes(input, std::as_writable_bytes(std::span{checkpoint.trainingStatus})) ||
        !readBytes(input, std::as_writable_bytes(std::span{checkpoint.trainingControl})))
    {
        if (reportInvalid)
        {
            reportCheckpointWarning(path, "payload is truncated or unreadable");
        }
        return std::nullopt;
    }
    if (!trainingCheckpointPayloadValid(checkpoint))
    {
        if (reportInvalid)
        {
            reportCheckpointWarning(path, "payload values or GPU-completed step are invalid");
        }
        return std::nullopt;
    }
    return checkpoint;
}

[[nodiscard]] bool writeTrainingCheckpointSlot(const std::filesystem::path &path,
                                               const NeuralAppearanceTrainingCheckpoint &checkpoint)
{
    auto error = std::error_code{};
    auto const parentPath = path.parent_path();
    if (!parentPath.empty())
    {
        std::filesystem::create_directories(parentPath, error);
        if (error)
        {
            reportCheckpointWarning(path, error.message());
            return false;
        }
    }

    static auto temporarySequence = std::atomic_uint64_t{0u};
    auto temporaryPath = path;
    temporaryPath += std::format(".tmp.{}.{}.{}", std::chrono::steady_clock::now().time_since_epoch().count(),
                                 std::hash<std::thread::id>{}(std::this_thread::get_id()),
                                 temporarySequence.fetch_add(1u, std::memory_order_relaxed));

    auto output = std::ofstream{temporaryPath, std::ios::binary | std::ios::trunc};
    auto const writeSucceeded =
        output.is_open() && writeBytes(output, std::as_bytes(std::span{&checkpoint.header, 1u})) &&
        writeBytes(output, checkpoint.modelParameters) && writeBytes(output, checkpoint.modelMoments) &&
        writeBytes(output, checkpoint.trainingLatent) && writeBytes(output, checkpoint.latentMoments) &&
        writeBytes(output, std::as_bytes(std::span{checkpoint.trainingStatus})) &&
        writeBytes(output, std::as_bytes(std::span{checkpoint.trainingControl}));
    if (writeSucceeded)
    {
        output.flush();
    }
    auto const flushSucceeded = writeSucceeded && output.good();
    output.close();
    if (!flushSucceeded || output.fail())
    {
        reportCheckpointWarning(path, "failed to write the temporary sibling");
        std::filesystem::remove(temporaryPath, error);
        return false;
    }

    auto const targetExists = std::filesystem::exists(path, error);
    if (error)
    {
        reportCheckpointWarning(path, error.message());
        std::filesystem::remove(temporaryPath, error);
        return false;
    }
    if (targetExists)
    {
        std::filesystem::remove(path, error);
        if (error)
        {
            reportCheckpointWarning(path, error.message());
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
    }

    std::filesystem::rename(temporaryPath, path, error);
    if (error)
    {
        reportCheckpointWarning(path, error.message());
        auto removeError = std::error_code{};
        std::filesystem::remove(temporaryPath, removeError);
        return false;
    }
    return true;
}

[[nodiscard]] constexpr std::uint32_t divideRoundUp(std::uint32_t value, std::uint32_t divisor) noexcept
{
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] constexpr vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    return (value + alignment - 1u) / alignment * alignment;
}

[[nodiscard]] constexpr std::array<nr::rhi::CooperativeVectorMatrixDesc, 5u>
neuralAppearanceCooperativeGradientDescs() noexcept
{
    using Layout = nr::rhi::CooperativeVectorMatrixLayout;
    return {
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 12u, .columns = 8u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 8u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 12u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 32u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 3u, .columns = 32u, .layout = Layout::TrainingOptimal},
    };
}

[[nodiscard]] constexpr std::array<nr::rhi::CooperativeVectorMatrixDesc, 5u>
neuralAppearanceCooperativeRowMajorGradientDescs() noexcept
{
    using Layout = nr::rhi::CooperativeVectorMatrixLayout;
    return {
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 12u, .columns = 8u, .layout = Layout::RowMajor, .rowStrideBytes = 16u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 8u, .layout = Layout::RowMajor, .rowStrideBytes = 16u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 12u, .layout = Layout::RowMajor, .rowStrideBytes = 24u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 32u, .layout = Layout::RowMajor, .rowStrideBytes = 64u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 3u, .columns = 32u, .layout = Layout::RowMajor, .rowStrideBytes = 64u},
    };
}

inline constexpr auto kRowMajorGradientOffsets = std::array<vk::DeviceSize, 5u>{0u, 256u, 832u, 1600u, 3712u};

[[nodiscard]] nr::rhi::Buffer createTrainingBuffer(nr::rhi::Device &device, vk::DeviceSize size,
                                                   nr::rhi::MemoryUsage memoryUsage, std::string_view debugName)
{
    auto buffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(size, vk::BufferUsageFlagBits::eStorageBuffer |
                                                vk::BufferUsageFlagBits::eTransferSrc |
                                                vk::BufferUsageFlagBits::eTransferDst),
        memoryUsage, debugName);
    nr::nrAssert(buffer.valid(), "NeuralAppearance failed to allocate {}.", debugName);
    return buffer;
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeTrainingCheckpointUploadPlan(const nr::rhi::Device &device)
{
    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const computeQueueFamily = device.queueManager.compute().queueFamilyIndex();
    auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
    plan.releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
        transferQueueFamily, computeQueueFamily,
        nr::rhi::ops::QueueAccessScope{
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        },
        nr::rhi::ops::QueueAccessScope{
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        });
    return plan;
}

void synchronizeTrainingCheckpointUploads(nr::rhi::Device &device,
                                          std::span<const nr::rhi::ops::BufferUploadTicket> tickets)
{
    nr::nrAssert(
        !tickets.empty() &&
            std::ranges::all_of(tickets, [](const nr::rhi::ops::BufferUploadTicket &ticket) { return ticket.valid(); }),
        "NeuralAppearance checkpoint restore requires valid upload tickets.");

    auto &uploadReadback = device.uploadReadback();
    auto commandPool = nr::rhi::CommandPool{device.device, device.queueManager.compute().queueFamilyIndex(),
                                            vk::CommandPoolCreateFlagBits::eTransient};
    auto commandBuffers = commandPool.allocatePrimary(1u);
    auto &commandBuffer = commandBuffers.front();
    nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    std::ranges::for_each(tickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
        uploadReadback.recordAcquireBarrier(commandBuffer, ticket);
    });
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto signalValue = std::uint64_t{0u};
    std::ranges::for_each(tickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
        signalValue = std::max(signalValue, ticket.signalValue);
    });
    auto batch = nr::rhi::CommandBatch{};
    batch.addWait(uploadReadback.uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eComputeShader, signalValue);
    batch.addCommandBuffer(commandBuffer);
    device.queueManager.compute().submit(std::move(batch));
    device.queueManager.compute().waitIdle();
    uploadReadback.reclaimCompletedUploads();
}

[[nodiscard]] std::shared_ptr<NeuralAppearanceRuntimeCache> makeRuntime(nr::rhi::Device &device,
                                                                        std::span<const nr::rhi::SlangProgram> programs,
                                                                        std::string_view runtimeName)
{
    nr::nrAssert(
        programs.size() == 7u,
        "NeuralAppearance runtime requires initialize, target, clear, gradient, optimize, pack, and viewer programs.");

    auto viewerPipelineDesc = nr::rhi::ComputePipelineDesc{};
    auto viewerDescriptorLayout =
        nr::rhi::ShaderDescriptorLayout::create(programs[6], viewerPipelineDesc.descriptorBindingPolicy);
    nr::nrAssert(viewerDescriptorLayout.valid(), "NeuralAppearance viewer descriptor reflection failed.");

    auto const samplerDesc = nr::rhi::SlangSamplerDesc{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    auto latentSampler0 =
        viewerDescriptorLayout.rootCursor()["gLatentTexture0"].makeImmutableSamplerBinding(samplerDesc);
    auto latentSampler1 =
        viewerDescriptorLayout.rootCursor()["gLatentTexture1"].makeImmutableSamplerBinding(samplerDesc);
    nr::nrAssert(latentSampler0.has_value() && latentSampler1.has_value(),
                 "NeuralAppearance viewer latent textures must support immutable samplers.");

    auto runtime = std::make_shared<NeuralAppearanceRuntimeCache>();
    runtime->initializePipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->targetPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->clearCoopGradientsPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->gradientPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->optimizePipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->packPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->viewerPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();

    runtime->initializePipeline->initialize(device.pipeline().createComputePipeline(
        programs[0], {}, 64u, {}, std::format("{}.InitializeTrainingPipeline", runtimeName)));
    runtime->targetPipeline->initialize(device.pipeline().createComputePipeline(
        programs[1], {}, 128u, {}, std::format("{}.EvaluateTargetsPipeline", runtimeName)));
    runtime->clearCoopGradientsPipeline->initialize(device.pipeline().createComputePipeline(
        programs[2], {}, 64u, {}, std::format("{}.ClearCoopGradientsPipeline", runtimeName)));
    runtime->gradientPipeline->initialize(device.pipeline().createComputePipeline(
        programs[3], {}, 128u, {}, std::format("{}.EvaluateGradientsPipeline", runtimeName)));
    runtime->optimizePipeline->initialize(device.pipeline().createComputePipeline(
        programs[4], {}, 128u, {}, std::format("{}.OptimizeTrainingPipeline", runtimeName)));
    runtime->packPipeline->initialize(device.pipeline().createComputePipeline(
        programs[5], {}, 64u, {}, std::format("{}.PackLatentPipeline", runtimeName)));
    auto viewerImmutableSamplers = std::array{*latentSampler0, *latentSampler1};
    runtime->viewerPipeline->initialize(device.pipeline().createComputePipeline(
        programs[6], viewerPipelineDesc, 64u, viewerImmutableSamplers, std::format("{}.ViewerPipeline", runtimeName)));

    runtime->modelParameters = createTrainingBuffer(device, kParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                    "NeuralAppearance.ModelParameters");
    runtime->modelMoments = createTrainingBuffer(device, kModelMomentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                 "NeuralAppearance.ModelMoments");
    runtime->trainingLatent = createTrainingBuffer(device, kLatentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                   "NeuralAppearance.TrainingLatent");
    runtime->latentMoments = createTrainingBuffer(device, kLatentMomentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                  "NeuralAppearance.LatentMoments");
    runtime->inferenceParameters = createTrainingBuffer(
        device, kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly, "NeuralAppearance.InferenceParameters");
    runtime->inferenceLatent = createTrainingBuffer(
        device, kInferenceLatentBufferBytes, nr::rhi::MemoryUsage::GpuOnly, "NeuralAppearance.InferenceLatent");
    runtime->cooperativeGradientDescs = neuralAppearanceCooperativeGradientDescs();
    runtime->rowMajorGradientDescs = neuralAppearanceCooperativeRowMajorGradientDescs();
    std::ranges::for_each(std::views::iota(std::size_t{0u}, runtime->cooperativeGradientDescs.size()),
                          [&](std::size_t index) {
                              auto const size = device.cooperativeVectorMatrixLayoutSize(
                                  runtime->cooperativeGradientDescs[index]);
                              runtime->cooperativeGradientLayoutSizes[index] = size;
                              runtime->rowMajorGradientLayoutSizes[index] = device.cooperativeVectorMatrixLayoutSize(
                                  runtime->rowMajorGradientDescs[index]);
                              runtime->cooperativeGradientOffsets[index] = alignUp(
                                  runtime->optimalWeightGradientBytes,
                                  nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment);
                              runtime->optimalWeightGradientBytes =
                                  runtime->cooperativeGradientOffsets[index] + size.byteSize;
                          });
    runtime->optimalWeightGradientBytes = alignUp(
        runtime->optimalWeightGradientBytes, nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment);
    runtime->optimalWeightGradients = createTrainingBuffer(
        device, runtime->optimalWeightGradientBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.OptimalWeightGradients");
    runtime->rowMajorWeightGradients = createTrainingBuffer(
        device, kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.RowMajorWeightGradients");
    runtime->biasGradients = createTrainingBuffer(
        device, kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.BiasGradients");
    nr::nrAssert(runtime->optimalWeightGradients.deviceAddress() %
                         nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment ==
                     0u,
                 "NeuralAppearance TrainingOptimal gradient allocation must have a 64-byte device address alignment.");
    runtime->trainingStatus = createTrainingBuffer(device, kTrainingStatusBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                   "NeuralAppearance.TrainingStatus");
    runtime->trainingControl = createTrainingBuffer(device, kTrainingControlBufferBytes, nr::rhi::MemoryUsage::CpuToGpu,
                                                    "NeuralAppearance.TrainingControl");
    runtime->heldOutQualitySamples = createTrainingBuffer(
        device, kHeldOutQualitySampleBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.HeldOutQualitySamples");
    runtime->trainingControl.writeMappedAndFlush(std::array<std::uint32_t, 4u>{0u, 1u, kTrainingSchemaMagic, 0u});
    return runtime;
}

[[nodiscard]] nr::renderer::GraphResourceHandle importTrainingBuffer(
    nr::renderer::NodeBuildContext &context, const nr::rhi::Buffer &buffer, nr::renderer::RetainedBufferState &state,
    std::string_view debugName, std::initializer_list<nr::renderer::BufferUsageIntent> usageIntents)
{
    nr::nrAssert(buffer.valid(), "{} buffer is invalid.", debugName);
    return context.addResource(nr::renderer::GraphImportedBufferDesc{
        .debugName = std::string(debugName),
        .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
        .initialOwnership = nr::renderer::ResourceOwnershipDomain::Compute,
        .size = buffer.size(),
        .usageIntents = std::vector<nr::renderer::BufferUsageIntent>{usageIntents},
        .importedResource = std::cref(buffer),
        .retainedState = std::ref(state),
    });
}

[[nodiscard]] nr::renderer::GraphResourceHandle makeTransientBuffer(nr::renderer::NodeBuildContext &context,
                                                                    std::string_view debugName, vk::DeviceSize size)
{
    return context.addResource(nr::renderer::GraphTransientBufferDesc{
        .debugName = std::string(debugName),
        .size = size,
        .usageIntents =
            {
                nr::renderer::BufferUsageIntent::StorageWrite,
                nr::renderer::BufferUsageIntent::StorageRead,
            },
    });
}

[[nodiscard]] nr::renderer::GraphResourceHandle makeTransientImage(
    nr::renderer::NodeBuildContext &context, std::string_view debugName, vk::Extent2D extent,
    std::initializer_list<nr::renderer::ImageUsageIntent> usageIntents)
{
    return context.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = std::string(debugName),
        .extent = vk::Extent3D{extent.width, extent.height, 1u},
        .format = vk::Format::eR16G16B16A16Sfloat,
        .usageIntents = std::vector<nr::renderer::ImageUsageIntent>{usageIntents},
    });
}

enum NeuralAppearanceHeldOutQualityFlag : std::uint32_t
{
    NeuralAppearanceHeldOutQualityFp32Finite = 1u << 0u,
    NeuralAppearanceHeldOutQualityFp32Nonnegative = 1u << 1u,
    NeuralAppearanceHeldOutQualityFp16Finite = 1u << 2u,
    NeuralAppearanceHeldOutQualityFp16Nonnegative = 1u << 3u,
    NeuralAppearanceHeldOutQualityTargetFinite = 1u << 4u,
    NeuralAppearanceHeldOutQualityTargetNonnegative = 1u << 5u,
};

[[nodiscard]] NeuralAppearanceLossDistribution summarizeHeldOutQualityLosses(std::span<const float> losses)
{
    auto distribution = NeuralAppearanceLossDistribution{
        .sampleCount = static_cast<std::uint32_t>(losses.size()),
    };
    if (losses.empty())
    {
        return distribution;
    }
    if (!std::ranges::all_of(losses, [](float loss) { return std::isfinite(loss) && loss >= 0.0f; }))
    {
        distribution.meanSafeLogLoss = std::numeric_limits<float>::quiet_NaN();
        distribution.percentile95SafeLogLoss = std::numeric_limits<float>::quiet_NaN();
        return distribution;
    }

    distribution.meanSafeLogLoss =
        std::accumulate(losses.begin(), losses.end(), 0.0f) / static_cast<float>(losses.size());
    auto sortedLosses = std::vector<float>{losses.begin(), losses.end()};
    std::ranges::sort(sortedLosses);
    auto const percentileIndex = (sortedLosses.size() * 95u + 99u) / 100u - 1u;
    distribution.percentile95SafeLogLoss = sortedLosses[percentileIndex];
    return distribution;
}

[[nodiscard]] std::optional<NeuralAppearanceQualityReport> makeHeldOutQualityReport(
    std::span<const std::byte> sampleBytes, std::span<const float, 8u> status)
{
    if (sampleBytes.size() != kHeldOutQualitySampleBufferBytes)
    {
        return std::nullopt;
    }

    auto fp32Losses = std::array<std::vector<float>, kHeldOutQualityStratumCount>{};
    auto fp16Losses = std::array<std::vector<float>, kHeldOutQualityStratumCount>{};
    auto zeroLosses = std::array<std::vector<float>, kHeldOutQualityStratumCount>{};
    std::ranges::for_each(std::views::iota(std::size_t{0u}, fp32Losses.size()), [&](std::size_t stratum) {
        fp32Losses[stratum].reserve(kHeldOutQualitySamplesPerStratum);
        fp16Losses[stratum].reserve(kHeldOutQualitySamplesPerStratum);
        zeroLosses[stratum].reserve(kHeldOutQualitySamplesPerStratum);
    });

    auto outputsFinite = true;
    auto outputsNonnegative = true;
    constexpr auto requiredFiniteFlags = NeuralAppearanceHeldOutQualityFp32Finite |
                                         NeuralAppearanceHeldOutQualityFp16Finite |
                                         NeuralAppearanceHeldOutQualityTargetFinite;
    constexpr auto requiredNonnegativeFlags = NeuralAppearanceHeldOutQualityFp32Nonnegative |
                                              NeuralAppearanceHeldOutQualityFp16Nonnegative |
                                              NeuralAppearanceHeldOutQualityTargetNonnegative;
    for (auto sampleIndex = std::size_t{0u}; sampleIndex < kHeldOutQualitySampleCount; ++sampleIndex)
    {
        auto record = std::array<float, 4u>{};
        std::memcpy(record.data(), sampleBytes.data() + sampleIndex * sizeof(record), sizeof(record));
        auto const flagsValid = std::isfinite(record[3]) && record[3] >= 0.0f && record[3] <= 63.0f &&
                                std::floor(record[3]) == record[3];
        auto const flags = flagsValid ? static_cast<std::uint32_t>(record[3]) : 0u;
        outputsFinite = outputsFinite && (flags & requiredFiniteFlags) == requiredFiniteFlags;
        outputsNonnegative = outputsNonnegative && (flags & requiredNonnegativeFlags) == requiredNonnegativeFlags;

        auto const stratum = sampleIndex / kHeldOutQualitySamplesPerStratum;
        fp32Losses[stratum].push_back(record[0]);
        fp16Losses[stratum].push_back(record[1]);
        zeroLosses[stratum].push_back(record[2]);
    }

    auto makeStratum = [&](std::size_t stratum) {
        return NeuralAppearanceHeldOutStratumQuality{
            .fp32Master = summarizeHeldOutQualityLosses(fp32Losses[stratum]),
            .fp16CooperativeVector = summarizeHeldOutQualityLosses(fp16Losses[stratum]),
            .zeroPrediction = summarizeHeldOutQualityLosses(zeroLosses[stratum]),
        };
    };
    auto allFp32 = std::vector<float>{};
    auto allFp16 = std::vector<float>{};
    auto allZero = std::vector<float>{};
    allFp32.reserve(kHeldOutQualitySampleCount);
    allFp16.reserve(kHeldOutQualitySampleCount);
    allZero.reserve(kHeldOutQualitySampleCount);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, fp32Losses.size()), [&](std::size_t stratum) {
        allFp32.insert(allFp32.end(), fp32Losses[stratum].begin(), fp32Losses[stratum].end());
        allFp16.insert(allFp16.end(), fp16Losses[stratum].begin(), fp16Losses[stratum].end());
        allZero.insert(allZero.end(), zeroLosses[stratum].begin(), zeroLosses[stratum].end());
    });

    return NeuralAppearanceQualityReport{
        .outputsFinite = outputsFinite,
        .outputsNonnegative = outputsNonnegative,
        .emaSafeLogLoss = status[0],
        .initialSafeLogLoss = status[1],
        .overall = NeuralAppearanceHeldOutStratumQuality{
            .fp32Master = summarizeHeldOutQualityLosses(allFp32),
            .fp16CooperativeVector = summarizeHeldOutQualityLosses(allFp16),
            .zeroPrediction = summarizeHeldOutQualityLosses(allZero),
        },
        .uniform = makeStratum(0u),
        .highlight = makeStratum(1u),
        .grazing = makeStratum(2u),
    };
}

[[nodiscard]] std::string_view neuralAppearanceQualityViolationName(NeuralAppearanceQualityViolation violation) noexcept
{
    switch (violation)
    {
    case NeuralAppearanceQualityViolation::OutputsNotFinite:
        return "outputs-not-finite";
    case NeuralAppearanceQualityViolation::OutputsNegative:
        return "outputs-negative";
    case NeuralAppearanceQualityViolation::InvalidTrainingLossTelemetry:
        return "invalid-training-loss-telemetry";
    case NeuralAppearanceQualityViolation::EmaLossThreshold:
        return "ema-loss-threshold";
    case NeuralAppearanceQualityViolation::EmaImprovementThreshold:
        return "ema-improvement-threshold";
    case NeuralAppearanceQualityViolation::InvalidHeldOutDistribution:
        return "invalid-held-out-distribution";
    case NeuralAppearanceQualityViolation::HeldOutMeanDoesNotBeatZero:
        return "held-out-mean-does-not-beat-zero";
    case NeuralAppearanceQualityViolation::HeldOutPercentileDoesNotBeatZero:
        return "held-out-p95-does-not-beat-zero";
    case NeuralAppearanceQualityViolation::Fp16MeanExceedsFp32Budget:
        return "fp16-mean-exceeds-fp32-budget";
    }
    return "unknown";
}

[[nodiscard]] std::string formatNeuralAppearanceQualityViolations(
    std::span<const NeuralAppearanceQualityViolation> violations)
{
    auto result = std::string{};
    std::ranges::for_each(violations, [&](NeuralAppearanceQualityViolation violation) {
        if (!result.empty())
        {
            result += ", ";
        }
        result += neuralAppearanceQualityViolationName(violation);
    });
    return result;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
NeuralAppearanceNode::NeuralAppearanceNode(bool comparisonEnabled) noexcept : comparisonEnabled_(comparisonEnabled)
{
}

NeuralAppearanceNode::~NeuralAppearanceNode() = default;

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> NeuralAppearanceNode::shaderRequests() const
{
    return {
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/initializeTraining"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateTargets"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/clearCoopGradients"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateGradients"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/optimizeTraining"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/packLatent"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/viewer"},
        },
    };
}

void NeuralAppearanceNode::initialize(NodeInitContext &context)
{
    nr::nrAssert(context.shaderPrograms.size() == 7u &&
                     std::ranges::all_of(context.shaderPrograms,
                                         [](const nr::rhi::SlangProgram &program) {
                                             return program.entryPoint() != nullptr &&
                                                    program.entryPoint()->stage == SLANG_STAGE_COMPUTE;
                                         }),
                 "NeuralAppearance initialization requires seven ordered compute shaders.");
    runtime_ = detail::makeRuntime(context.device.get(), context.shaderPrograms, context.runtimeName);
}

void NeuralAppearanceNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->initializePipeline && runtime_->initializePipeline->valid() &&
                     runtime_->targetPipeline && runtime_->targetPipeline->valid() && runtime_->clearCoopGradientsPipeline &&
                     runtime_->clearCoopGradientsPipeline->valid() && runtime_->gradientPipeline &&
                     runtime_->gradientPipeline->valid() && runtime_->optimizePipeline &&
                     runtime_->optimizePipeline->valid() && runtime_->packPipeline && runtime_->packPipeline->valid() &&
                     runtime_->viewerPipeline && runtime_->viewerPipeline->valid(),
                 "NeuralAppearance async compute PSO construction failed.");
}

void NeuralAppearanceNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(runtime_ && runtime_->initializePipeline && runtime_->targetPipeline && runtime_->clearCoopGradientsPipeline && runtime_->gradientPipeline &&
                     runtime_->optimizePipeline && runtime_->packPipeline && runtime_->viewerPipeline,
                 "NeuralAppearance build stage requires initialized runtime state.");
    nr::nrAssert(context.queue == nr::renderer::QueueDomain::Compute,
                 "NeuralAppearance must run on the compute queue.");

    auto const outputExtent = frameParameters.swapchainExtent;
    nr::nrAssert(outputExtent.width > 0u && outputExtent.height > 0u,
                 "NeuralAppearance requires a non-zero swapchain extent.");

    auto modelParameters = detail::importTrainingBuffer(
        context, runtime_->modelParameters, runtime_->modelParameterState, "NeuralAppearance.ModelParameters",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageWrite,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        });
    auto modelMoments = detail::importTrainingBuffer(context, runtime_->modelMoments, runtime_->modelMomentState,
                                                     "NeuralAppearance.ModelMoments",
                                                     {
                                                         nr::renderer::BufferUsageIntent::StorageWrite,
                                                         nr::renderer::BufferUsageIntent::StorageReadWrite,
                                                     });
    auto trainingLatent = detail::importTrainingBuffer(context, runtime_->trainingLatent, runtime_->trainingLatentState,
                                                       "NeuralAppearance.TrainingLatent",
                                                       {
                                                           nr::renderer::BufferUsageIntent::StorageRead,
                                                           nr::renderer::BufferUsageIntent::StorageWrite,
                                                           nr::renderer::BufferUsageIntent::StorageReadWrite,
                                                       });
    auto latentMoments = detail::importTrainingBuffer(context, runtime_->latentMoments, runtime_->latentMomentState,
                                                      "NeuralAppearance.LatentMoments",
                                                      {
                                                          nr::renderer::BufferUsageIntent::StorageWrite,
                                                          nr::renderer::BufferUsageIntent::StorageReadWrite,
                                                      });
    auto inferenceParameters = detail::importTrainingBuffer(
        context, runtime_->inferenceParameters, runtime_->inferenceParameterState,
        "NeuralAppearance.InferenceParameters",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageWrite,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        });
    auto inferenceLatent = detail::importTrainingBuffer(
        context, runtime_->inferenceLatent, runtime_->inferenceLatentState,
        "NeuralAppearance.InferenceLatent",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageWrite,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        });
    auto optimalWeightGradients = detail::importTrainingBuffer(
        context, runtime_->optimalWeightGradients, runtime_->optimalWeightGradientState,
        "NeuralAppearance.OptimalWeightGradients",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageWrite,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
            nr::renderer::BufferUsageIntent::CooperativeVectorConvertRead,
        });
    auto rowMajorWeightGradients = detail::importTrainingBuffer(
        context, runtime_->rowMajorWeightGradients, runtime_->rowMajorWeightGradientState,
        "NeuralAppearance.RowMajorWeightGradients",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageWrite,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
            nr::renderer::BufferUsageIntent::CooperativeVectorConvertWrite,
        });
    auto biasGradients = detail::importTrainingBuffer(
        context, runtime_->biasGradients, runtime_->biasGradientState, "NeuralAppearance.BiasGradients",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageWrite,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        });
    auto trainingStatus = detail::importTrainingBuffer(context, runtime_->trainingStatus, runtime_->trainingStatusState,
                                                       "NeuralAppearance.TrainingStatus",
                                                       {
                                                           nr::renderer::BufferUsageIntent::StorageRead,
                                                           nr::renderer::BufferUsageIntent::StorageWrite,
                                                           nr::renderer::BufferUsageIntent::StorageReadWrite,
                                                       });
    auto trainingControl = detail::importTrainingBuffer(
        context, runtime_->trainingControl, runtime_->trainingControlState, "NeuralAppearance.TrainingControl",
        {
            nr::renderer::BufferUsageIntent::StorageRead,
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        });
    auto heldOutQualitySamples = detail::importTrainingBuffer(
        context, runtime_->heldOutQualitySamples, runtime_->heldOutQualitySampleState,
        "NeuralAppearance.HeldOutQualitySamples",
        {
            nr::renderer::BufferUsageIntent::StorageWrite,
        });

    auto sampleGradients =
        detail::makeTransientBuffer(context, "NeuralAppearance.SampleGradients", detail::kSampleGradientBufferBytes);
    auto sampleTexelIndices = detail::makeTransientBuffer(context, "NeuralAppearance.SampleTexelIndices",
                                                          detail::kSampleTexelIndexBufferBytes);
    auto sampleMetrics =
        detail::makeTransientBuffer(context, "NeuralAppearance.SampleMetrics", detail::kSampleMetricBufferBytes);
    auto sampleTargets =
        detail::makeTransientBuffer(context, "NeuralAppearance.SampleTargets", detail::kSampleTargetBufferBytes);
    auto latentTexture0 = detail::makeTransientImage(context, "NeuralAppearance.LatentTexture0", detail::kLatentExtent,
                                                     {
                                                         nr::renderer::ImageUsageIntent::StorageWrite,
                                                         nr::renderer::ImageUsageIntent::Sampled,
                                                     });
    auto latentTexture1 = detail::makeTransientImage(context, "NeuralAppearance.LatentTexture1", detail::kLatentExtent,
                                                     {
                                                         nr::renderer::ImageUsageIntent::StorageWrite,
                                                         nr::renderer::ImageUsageIntent::Sampled,
                                                     });
    auto outputColor = detail::makeTransientImage(context, "NeuralAppearance.OutputColor", outputExtent,
                                                  {
                                                      nr::renderer::ImageUsageIntent::StorageWrite,
                                                      nr::renderer::ImageUsageIntent::Sampled,
                                                      nr::renderer::ImageUsageIntent::TransferSrc,
                                                  });

    auto initializePass =
        nr::renderer::ComputePassBuilder{context, "NeuralAppearance.InitializeTraining", runtime_->initializePipeline};
    initializePass.storageBuffer("gTrainingControl", trainingControl, "NeuralAppearance.TrainingControl")
        .storageBufferWrite("gModelParameters", modelParameters, "NeuralAppearance.ModelParameters")
        .storageBufferWrite("gModelMoments", modelMoments, "NeuralAppearance.ModelMoments")
        .storageBufferWrite("gTrainingLatent", trainingLatent, "NeuralAppearance.TrainingLatent")
        .storageBufferWrite("gLatentMoments", latentMoments, "NeuralAppearance.LatentMoments")
        .storageBufferWrite("gTrainingStatus", trainingStatus, "NeuralAppearance.TrainingStatus")
        .storageBufferWrite("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
        .storageBufferWrite("gInferenceLatent", inferenceLatent, "NeuralAppearance.InferenceLatent")
        .record([](const nr::renderer::ComputePassRecordContext &computeContext) {
            computeContext.commandBuffer.dispatch(
                detail::divideRoundUp(std::max(detail::kParameterChunkCount, detail::kLatentChunkCount),
                                      detail::kTrainingThreadGroupSize),
                1u, 1u);
        });
    [[maybe_unused]] auto initializePassHandle = initializePass.build();

    auto const frameOrdinal = frameParameters.optionSnapshot.get().frameIndex;
    auto const firstActiveTrainingStep = runtime_->nextTrainingStep;
    auto activeTrainingPairCount = 0u;
    // A graph build may repeat or revisit an option snapshot, so only a newer display ordinal advances training.
    if (frameOrdinal > runtime_->lastDisplayOrdinal)
    {
        runtime_->lastDisplayOrdinal = frameOrdinal;
        auto const remainingTrainingSteps = runtime_->nextTrainingStep <= detail::kTotalTrainingStepCount
                                                ? detail::kTotalTrainingStepCount - runtime_->nextTrainingStep + 1u
                                                : 0u;
        auto const trainingStepBudget =
            runtime_->nextTrainingStep <= detail::kMollificationStepCount ? 1u : detail::kTrainingPairSlotCount;
        activeTrainingPairCount = std::min(trainingStepBudget, remainingTrainingSteps);
        runtime_->nextTrainingStep += activeTrainingPairCount;
    }

    auto const lastTrainingStep = std::min(runtime_->nextTrainingStep - 1u, detail::kTotalTrainingStepCount);
    auto trainingDispatches =
        std::array<detail::NeuralAppearanceGradientPushConstants, detail::kTrainingPairSlotCount>{};
    // Fixed pair slots preserve pass-binding owner ordinals; a zero batch makes an inactive slot a shader no-op.
    std::ranges::for_each(trainingDispatches,
                          [lastTrainingStep](auto &dispatch) { dispatch.trainingStep = lastTrainingStep; });
    std::ranges::for_each(trainingDispatches, [offsets = runtime_->cooperativeGradientOffsets](auto &dispatch) {
        std::ranges::transform(offsets, dispatch.optimalWeightOffsets.begin(), [](vk::DeviceSize offset) {
            return static_cast<std::uint32_t>(offset);
        });
    });
    std::ranges::for_each(std::views::iota(0u, activeTrainingPairCount), [&](std::uint32_t pairIndex) {
        auto &dispatch = trainingDispatches[pairIndex];
        dispatch.trainingStep = firstActiveTrainingStep + pairIndex;
        dispatch.batchSize = detail::kTrainingBatchSize;
    });

    std::ranges::for_each(std::views::iota(0u, detail::kTrainingPairSlotCount), [&](std::uint32_t pairIndex) {
        auto const &gradientPushConstants = trainingDispatches[pairIndex];
        auto const targetPushConstants = detail::NeuralAppearanceTargetPushConstants{
            .trainingStep = gradientPushConstants.trainingStep,
            .batchSize = gradientPushConstants.batchSize,
            .latentWidth = gradientPushConstants.latentWidth,
            .latentHeight = gradientPushConstants.latentHeight,
            .mollificationSampleCount = gradientPushConstants.mollificationSampleCount,
            .mollificationStepCount = gradientPushConstants.mollificationStepCount,
            .initialAngleRadians = gradientPushConstants.initialAngleRadians,
            .padding = gradientPushConstants.padding,
        };
        auto targetPass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.EvaluateTargets.Pair{}", pairIndex), runtime_->targetPipeline};
        targetPass.storageBufferWrite("gSampleTargets", sampleTargets, "NeuralAppearance.SampleTargets")
            .pushConstants("gTraining", targetPushConstants)
            .record([groupCount = gradientPushConstants.batchSize](
                        const nr::renderer::ComputePassRecordContext &computeContext) {
                computeContext.commandBuffer.dispatch(groupCount, 1u, 1u);
            });
        [[maybe_unused]] auto targetPassHandle = targetPass.build();

        auto clearPass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.ClearCoopGradients.Pair{}", pairIndex),
            runtime_->clearCoopGradientsPipeline};
        clearPass.storageBufferWrite("gOptimalWeightGradients", optimalWeightGradients,
                                     "NeuralAppearance.OptimalWeightGradients")
            .storageBufferWrite("gRowMajorWeightGradients", rowMajorWeightGradients,
                                "NeuralAppearance.RowMajorWeightGradients")
            .storageBufferWrite("gBiasGradients", biasGradients, "NeuralAppearance.BiasGradients")
            .pushConstants("gClear", detail::NeuralAppearanceClearCoopGradientsPushConstants{
                                         .optimalWeightGradientBytes = static_cast<std::uint32_t>(
                                             runtime_->optimalWeightGradientBytes),
                                     })
            .record([active = gradientPushConstants.batchSize > 0u,
                     clearWords = static_cast<std::uint32_t>(
                         std::max(runtime_->optimalWeightGradientBytes, detail::kInferenceParameterBufferBytes) / 4u)](
                        const nr::renderer::ComputePassRecordContext &computeContext) {
                computeContext.commandBuffer.dispatch(
                    active ? detail::divideRoundUp(clearWords, detail::kTrainingThreadGroupSize) : 0u, 1u, 1u);
            });
        [[maybe_unused]] auto clearPassHandle = clearPass.build();

        auto gradientPass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.EvaluateGradients.Pair{}", pairIndex), runtime_->gradientPipeline};
        gradientPass.storageBuffer("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
            .storageBuffer("gInferenceLatent", inferenceLatent, "NeuralAppearance.InferenceLatent")
            .storageBufferWrite("gOptimalWeightGradients", optimalWeightGradients,
                                "NeuralAppearance.OptimalWeightGradients")
            .storageBufferWrite("gBiasGradients", biasGradients, "NeuralAppearance.BiasGradients")
            .storageBuffer("gSampleTargets", sampleTargets, "NeuralAppearance.SampleTargets")
            .storageBufferWrite("gSampleGradients", sampleGradients, "NeuralAppearance.SampleGradients")
            .storageBufferWrite("gSampleTexelIndices", sampleTexelIndices, "NeuralAppearance.SampleTexelIndices")
            .storageBufferWrite("gSampleMetrics", sampleMetrics, "NeuralAppearance.SampleMetrics")
            .pushConstants("gTraining", gradientPushConstants)
            .record([batchSize = gradientPushConstants.batchSize](
                        const nr::renderer::ComputePassRecordContext &computeContext) {
                computeContext.commandBuffer.dispatch(
                    detail::divideRoundUp(batchSize, detail::kGradientThreadGroupSize), 1u, 1u);
            });
        [[maybe_unused]] auto gradientPassHandle = gradientPass.build();

        auto const conversionUses = std::array{
            nr::renderer::use::cooperativeVectorConvertRead(optimalWeightGradients),
            nr::renderer::use::cooperativeVectorConvertWrite(rowMajorWeightGradients),
        };
        static_cast<void>(context.addPass(
            conversionUses, std::format("NeuralAppearance.ConvertCoopGradients.Pair{}", pairIndex),
            [runtime = runtime_, active = gradientPushConstants.batchSize > 0u](
                const nr::renderer::PassRecordContext &recordContext) {
                if (!active)
                {
                    return;
                }
                nr::nrAssert(recordContext.commandBuffer.has_value() && recordContext.device.has_value(),
                             "NeuralAppearance cooperative-vector conversion requires command buffer and device.");
                std::ranges::for_each(
                    std::views::iota(std::size_t{0u}, runtime->rowMajorGradientDescs.size()), [&](std::size_t index) {
                        recordContext.device->get().recordCooperativeVectorMatrixConversion(
                            recordContext.commandBuffer->get(),
                            nr::rhi::CooperativeVectorMatrixMemory{
                                .deviceAddress = runtime->optimalWeightGradients.deviceAddress() +
                                                 runtime->cooperativeGradientOffsets[index],
                                .size = runtime->cooperativeGradientLayoutSizes[index].byteSize,
                            },
                            runtime->cooperativeGradientDescs[index],
                            runtime->cooperativeGradientLayoutSizes[index],
                            nr::rhi::CooperativeVectorMatrixMemory{
                                .deviceAddress = runtime->rowMajorWeightGradients.deviceAddress() +
                                                 detail::kRowMajorGradientOffsets[index],
                                .size = runtime->rowMajorGradientLayoutSizes[index].byteSize,
                            },
                            runtime->rowMajorGradientDescs[index], runtime->rowMajorGradientLayoutSizes[index]);
                    });
            }));

        auto const optimizePushConstants = detail::NeuralAppearanceOptimizePushConstants{
            .trainingStep = gradientPushConstants.trainingStep,
            .batchSize = gradientPushConstants.batchSize,
        };
        auto optimizePass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.OptimizeTraining.Pair{}", pairIndex), runtime_->optimizePipeline};
        optimizePass.storageBuffer("gSampleGradients", sampleGradients, "NeuralAppearance.SampleGradients")
            .storageBuffer("gSampleTexelIndices", sampleTexelIndices, "NeuralAppearance.SampleTexelIndices")
            .storageBuffer("gSampleMetrics", sampleMetrics, "NeuralAppearance.SampleMetrics")
            .storageBufferReadWrite("gModelParameters", modelParameters, "NeuralAppearance.ModelParameters")
            .storageBufferReadWrite("gModelMoments", modelMoments, "NeuralAppearance.ModelMoments")
            .storageBufferReadWrite("gTrainingLatent", trainingLatent, "NeuralAppearance.TrainingLatent")
            .storageBufferReadWrite("gLatentMoments", latentMoments, "NeuralAppearance.LatentMoments")
            .storageBufferReadWrite("gTrainingStatus", trainingStatus, "NeuralAppearance.TrainingStatus")
            .storageBufferReadWrite("gTrainingControl", trainingControl, "NeuralAppearance.TrainingControl")
            .storageBuffer("gRowMajorWeightGradients", rowMajorWeightGradients,
                           "NeuralAppearance.RowMajorWeightGradients")
            .storageBuffer("gBiasGradients", biasGradients, "NeuralAppearance.BiasGradients")
            .storageBufferWrite("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
            .storageBufferWrite("gInferenceLatent", inferenceLatent, "NeuralAppearance.InferenceLatent")
            .pushConstants("gTraining", optimizePushConstants)
            .record([active = gradientPushConstants.batchSize >
                              0u](const nr::renderer::ComputePassRecordContext &computeContext) {
                auto const groupCount =
                    active ? detail::divideRoundUp(std::max(detail::kParameterChunkCount, detail::kLatentTexelCount),
                                                   detail::kTrainingThreadGroupSize)
                           : 0u;
                computeContext.commandBuffer.dispatch(groupCount, 1u, 1u);
            });
        [[maybe_unused]] auto optimizePassHandle = optimizePass.build();
    });

    auto packPass = nr::renderer::ComputePassBuilder{context, "NeuralAppearance.PackLatent", runtime_->packPipeline};
    packPass.storageBuffer("gInferenceLatent", inferenceLatent, "NeuralAppearance.InferenceLatent")
        .storageBuffer("gModelParameters", modelParameters, "NeuralAppearance.ModelParameters")
        .storageBuffer("gTrainingLatent", trainingLatent, "NeuralAppearance.TrainingLatent")
        .storageBuffer("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
        .storageBufferWrite("gHeldOutQualitySamples", heldOutQualitySamples,
                            "NeuralAppearance.HeldOutQualitySamples")
        .storageImage("gLatentTexture0", latentTexture0, "NeuralAppearance.LatentTexture0")
        .storageImage("gLatentTexture1", latentTexture1, "NeuralAppearance.LatentTexture1")
        .record([trainingFinished = lastTrainingStep >= detail::kTotalTrainingStepCount](
                    const nr::renderer::ComputePassRecordContext &computeContext) {
            auto const groupCountX =
                trainingFinished ? detail::divideRoundUp(detail::kLatentExtent.width, detail::kImageThreadGroupSize)
                                 : 0u;
            auto const groupCountY =
                trainingFinished ? detail::divideRoundUp(detail::kLatentExtent.height, detail::kImageThreadGroupSize)
                                 : 0u;
            computeContext.commandBuffer.dispatch(groupCountX, groupCountY, 1u);
        });
    [[maybe_unused]] auto packPassHandle = packPass.build();

    auto const viewerPushConstants = detail::NeuralAppearancePushConstants{
        .width = outputExtent.width,
        .height = outputExtent.height,
        .frameIndex = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(frameOrdinal, std::numeric_limits<std::uint32_t>::max())),
        .totalTrainingSteps = detail::kTotalTrainingStepCount,
        .comparisonEnabled = comparisonEnabled_ ? 1u : 0u,
    };
    auto viewerPass = nr::renderer::ComputePassBuilder{context, "NeuralAppearance.Viewer", runtime_->viewerPipeline};
    viewerPass.sampledImage("gLatentTexture0", latentTexture0, "NeuralAppearance.LatentTexture0")
        .sampledImage("gLatentTexture1", latentTexture1, "NeuralAppearance.LatentTexture1")
        .storageBuffer("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
        .storageBuffer("gTrainingStatus", trainingStatus, "NeuralAppearance.TrainingStatus")
        .storageImage("gOutputColor", outputColor, "NeuralAppearance.OutputColor")
        .pushConstants("gNeuralAppearance", viewerPushConstants)
        .record([outputExtent](const nr::renderer::ComputePassRecordContext &computeContext) {
            computeContext.commandBuffer.dispatch(
                detail::divideRoundUp(outputExtent.width, detail::kImageThreadGroupSize),
                detail::divideRoundUp(outputExtent.height, detail::kImageThreadGroupSize), 1u);
        });
    [[maybe_unused]] auto viewerPassHandle = viewerPass.build();

    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, outputColor);
}

bool NeuralAppearanceNode::trainingComplete() const noexcept
{
    return lastScheduledTrainingStep() >= detail::kTotalTrainingStepCount;
}

std::uint32_t NeuralAppearanceNode::lastScheduledTrainingStep() const noexcept
{
    if (!runtime_)
    {
        return 0u;
    }
    return std::min(runtime_->nextTrainingStep - 1u, detail::kTotalTrainingStepCount);
}

bool NeuralAppearanceNode::trainingCheckpointExists(const std::filesystem::path &path)
{
    auto lock = std::scoped_lock{detail::trainingCheckpointFileMutex()};
    auto exists = false;
    std::ranges::for_each(detail::trainingCheckpointSlotPaths(path), [&](const std::filesystem::path &slotPath) {
        auto error = std::error_code{};
        auto const slotExists = std::filesystem::exists(slotPath, error);
        if (error)
        {
            detail::reportCheckpointWarning(slotPath, error.message());
            exists = true;
            return;
        }
        exists = exists || slotExists;
    });
    return exists;
}

bool NeuralAppearanceNode::removeTrainingCheckpoint(const std::filesystem::path &path,
                                                    const std::filesystem::path &preservePath)
{
    auto lock = std::scoped_lock{detail::trainingCheckpointFileMutex()};
    auto removed = true;
    std::ranges::for_each(detail::trainingCheckpointSlotPaths(path), [&](const std::filesystem::path &slotPath) {
        if (detail::pathsReferToSameFile(slotPath, preservePath))
        {
            return;
        }
        auto error = std::error_code{};
        static_cast<void>(std::filesystem::remove(slotPath, error));
        if (error)
        {
            detail::reportCheckpointWarning(slotPath, error.message());
            removed = false;
        }
    });
    return removed;
}

std::optional<std::uint32_t> NeuralAppearanceNode::saveTrainingCheckpoint(nr::rhi::Device &device,
                                                                          const std::filesystem::path &path) const
{
    if (!runtime_)
    {
        detail::reportCheckpointWarning(path, "runtime is not initialized");
        return std::nullopt;
    }

    device.waitIdle();
    auto const readbackSyncPlan = nr::rhi::ops::ReadbackSyncPlan{
        .preCopy =
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            },
        .postCopy =
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            },
    };
    auto &readback = device.uploadReadback();
    auto modelParameterTicket = readback.readbackBuffer(runtime_->modelParameters, 0u, detail::kParameterBufferBytes,
                                                        nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto modelMomentTicket = readback.readbackBuffer(runtime_->modelMoments, 0u, detail::kModelMomentBufferBytes,
                                                     nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto trainingLatentTicket = readback.readbackBuffer(runtime_->trainingLatent, 0u, detail::kLatentBufferBytes,
                                                        nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto latentMomentTicket = readback.readbackBuffer(runtime_->latentMoments, 0u, detail::kLatentMomentBufferBytes,
                                                      nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto statusTicket = readback.readbackBuffer(runtime_->trainingStatus, 0u, detail::kTrainingStatusBufferBytes,
                                                nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto controlTicket = readback.readbackBuffer(runtime_->trainingControl, 0u, detail::kTrainingControlBufferBytes,
                                                 nr::rhi::QueueRole::Compute, readbackSyncPlan);

    auto checkpoint = detail::NeuralAppearanceTrainingCheckpoint{};
    checkpoint.modelParameters = readback.readbackBytes(modelParameterTicket);
    checkpoint.modelMoments = readback.readbackBytes(modelMomentTicket);
    checkpoint.trainingLatent = readback.readbackBytes(trainingLatentTicket);
    checkpoint.latentMoments = readback.readbackBytes(latentMomentTicket);
    auto statusBytes = readback.readbackBytes(statusTicket);
    auto controlBytes = readback.readbackBytes(controlTicket);
    if (statusBytes.size() != detail::kTrainingStatusBufferBytes ||
        controlBytes.size() != detail::kTrainingControlBufferBytes)
    {
        detail::reportCheckpointWarning(path, "GPU status or control readback has an invalid size");
        return std::nullopt;
    }
    std::memcpy(checkpoint.trainingStatus.data(), statusBytes.data(), statusBytes.size());
    std::memcpy(checkpoint.trainingControl.data(), controlBytes.data(), controlBytes.size());
    checkpoint.header.completedTrainingStep = checkpoint.trainingControl[0];
    if (!detail::trainingCheckpointPayloadValid(checkpoint))
    {
        detail::reportCheckpointWarning(path, "GPU snapshot contains invalid values or an inconsistent completed step");
        return std::nullopt;
    }

    auto lock = std::scoped_lock{detail::trainingCheckpointFileMutex()};
    auto const slotPaths = detail::trainingCheckpointSlotPaths(path);
    auto slot0 = detail::readTrainingCheckpointSlot(slotPaths[0], false);
    auto slot1 = detail::readTrainingCheckpointSlot(slotPaths[1], false);
    auto const targetSlot = !slot0.has_value()   ? std::size_t{0u}
                            : !slot1.has_value() ? std::size_t{1u}
                            : slot0->header.completedTrainingStep <= slot1->header.completedTrainingStep
                                ? std::size_t{0u}
                                : std::size_t{1u};
    if (!detail::writeTrainingCheckpointSlot(slotPaths[targetSlot], checkpoint))
    {
        return std::nullopt;
    }
    return checkpoint.header.completedTrainingStep;
}

bool NeuralAppearanceNode::loadTrainingCheckpoint(nr::rhi::Device &device, const std::filesystem::path &path)
{
    if (!runtime_)
    {
        detail::reportCheckpointWarning(path, "runtime is not initialized");
        return false;
    }

    auto checkpoint = std::optional<detail::NeuralAppearanceTrainingCheckpoint>{};
    {
        auto lock = std::scoped_lock{detail::trainingCheckpointFileMutex()};
        auto const slotPaths = detail::trainingCheckpointSlotPaths(path);
        auto slot0 = detail::readTrainingCheckpointSlot(slotPaths[0], true);
        auto slot1 = detail::readTrainingCheckpointSlot(slotPaths[1], true);
        if (slot0.has_value() && slot1.has_value())
        {
            checkpoint = slot0->header.completedTrainingStep >= slot1->header.completedTrainingStep ? std::move(slot0)
                                                                                                    : std::move(slot1);
        }
        else if (slot0.has_value())
        {
            checkpoint = std::move(slot0);
        }
        else if (slot1.has_value())
        {
            checkpoint = std::move(slot1);
        }
    }
    if (!checkpoint.has_value())
    {
        detail::reportCheckpointWarning(path, "no valid checkpoint slot was found");
        return false;
    }

    device.waitIdle();
    auto restoredModelParameters =
        detail::createTrainingBuffer(device, detail::kParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                     "NeuralAppearance.RestoredModelParameters");
    auto restoredModelMoments =
        detail::createTrainingBuffer(device, detail::kModelMomentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                     "NeuralAppearance.RestoredModelMoments");
    auto restoredTrainingLatent = detail::createTrainingBuffer(
        device, detail::kLatentBufferBytes, nr::rhi::MemoryUsage::GpuOnly, "NeuralAppearance.RestoredTrainingLatent");
    auto restoredLatentMoments =
        detail::createTrainingBuffer(device, detail::kLatentMomentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                     "NeuralAppearance.RestoredLatentMoments");
    auto restoredInferenceParameters = detail::createTrainingBuffer(
        device, detail::kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.RestoredInferenceParameters");
    auto restoredInferenceLatent = detail::createTrainingBuffer(
        device, detail::kInferenceLatentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.RestoredInferenceLatent");
    auto restoredTrainingStatus =
        detail::createTrainingBuffer(device, detail::kTrainingStatusBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                     "NeuralAppearance.RestoredTrainingStatus");
    auto restoredTrainingControl =
        detail::createTrainingBuffer(device, detail::kTrainingControlBufferBytes, nr::rhi::MemoryUsage::CpuToGpu,
                                     "NeuralAppearance.RestoredTrainingControl");
    auto restoredHeldOutQualitySamples = detail::createTrainingBuffer(
        device, detail::kHeldOutQualitySampleBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.RestoredHeldOutQualitySamples");

    auto &uploadReadback = device.uploadReadback();
    auto const uploadPlan = detail::makeTrainingCheckpointUploadPlan(device);
    auto uploadTickets = std::array{
        uploadReadback.uploadBuffer(checkpoint->modelParameters, restoredModelParameters, 0u, uploadPlan),
        uploadReadback.uploadBuffer(checkpoint->modelMoments, restoredModelMoments, 0u, uploadPlan),
        uploadReadback.uploadBuffer(checkpoint->trainingLatent, restoredTrainingLatent, 0u, uploadPlan),
        uploadReadback.uploadBuffer(checkpoint->latentMoments, restoredLatentMoments, 0u, uploadPlan),
        uploadReadback.uploadBuffer(std::as_bytes(std::span{checkpoint->trainingStatus}), restoredTrainingStatus, 0u,
                                    uploadPlan),
    };
    detail::synchronizeTrainingCheckpointUploads(device, uploadTickets);

    auto restoredControl = checkpoint->trainingControl;
    // `2` requests the initialization shader's mirror-only path. It rebuilds
    // FP16 QAT mirrors from restored FP32 masters without resetting Adam.
    restoredControl[1] = 2u;
    restoredTrainingControl.writeMappedAndFlush(restoredControl);

    runtime_->modelParameters = std::move(restoredModelParameters);
    runtime_->modelMoments = std::move(restoredModelMoments);
    runtime_->trainingLatent = std::move(restoredTrainingLatent);
    runtime_->latentMoments = std::move(restoredLatentMoments);
    runtime_->inferenceParameters = std::move(restoredInferenceParameters);
    runtime_->inferenceLatent = std::move(restoredInferenceLatent);
    runtime_->trainingStatus = std::move(restoredTrainingStatus);
    runtime_->trainingControl = std::move(restoredTrainingControl);
    runtime_->heldOutQualitySamples = std::move(restoredHeldOutQualitySamples);
    runtime_->modelParameterState = {};
    runtime_->modelMomentState = {};
    runtime_->trainingLatentState = {};
    runtime_->latentMomentState = {};
    runtime_->inferenceParameterState = {};
    runtime_->inferenceLatentState = {};
    runtime_->optimalWeightGradientState = {};
    runtime_->rowMajorWeightGradientState = {};
    runtime_->biasGradientState = {};
    runtime_->trainingStatusState = {};
    runtime_->trainingControlState = {};
    runtime_->heldOutQualitySampleState = {};
    runtime_->nextTrainingStep = checkpoint->header.completedTrainingStep + 1u;
    runtime_->lastDisplayOrdinal = 0u;
    return true;
}

bool NeuralAppearanceNode::saveTrainingArtifact(nr::rhi::Device &device, const std::filesystem::path &path) const
{
    if (path.extension() != ".nart")
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refuses artifact '{}' because V2 production artifacts must use the .nart extension.",
            path.generic_string());
        return false;
    }
    if (!runtime_ || !trainingComplete())
    {
        return false;
    }

    device.waitIdle();
    auto const readbackSyncPlan = nr::rhi::ops::ReadbackSyncPlan{
        .preCopy =
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            },
        .postCopy =
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            },
    };
    auto &readback = device.uploadReadback();
    auto modelTicket = readback.readbackBuffer(runtime_->inferenceParameters, 0u, detail::kInferenceParameterBufferBytes,
                                               nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto latentTicket = readback.readbackBuffer(runtime_->inferenceLatent, 0u, detail::kInferenceLatentBufferBytes,
                                                nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto statusTicket = readback.readbackBuffer(runtime_->trainingStatus, 0u, detail::kTrainingStatusBufferBytes,
                                                nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto controlTicket = readback.readbackBuffer(runtime_->trainingControl, 0u, detail::kTrainingControlBufferBytes,
                                                 nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto qualityTicket = readback.readbackBuffer(runtime_->heldOutQualitySamples, 0u,
                                                 detail::kHeldOutQualitySampleBufferBytes,
                                                 nr::rhi::QueueRole::Compute, readbackSyncPlan);
    auto modelBytes = readback.readbackBytes(modelTicket);
    auto latentBytes = readback.readbackBytes(latentTicket);
    auto statusBytes = readback.readbackBytes(statusTicket);
    auto controlBytes = readback.readbackBytes(controlTicket);
    auto qualityBytes = readback.readbackBytes(qualityTicket);
    if (modelBytes.size() != detail::kInferenceParameterBufferBytes || latentBytes.size() != detail::kInferenceLatentBufferBytes ||
        statusBytes.size() != detail::kTrainingStatusBufferBytes ||
        controlBytes.size() != detail::kTrainingControlBufferBytes ||
        qualityBytes.size() != detail::kHeldOutQualitySampleBufferBytes)
    {
        return false;
    }

    auto status = std::array<float, 8u>{};
    auto control = std::array<std::uint32_t, 4u>{};
    std::memcpy(status.data(), statusBytes.data(), statusBytes.size());
    std::memcpy(control.data(), controlBytes.data(), controlBytes.size());
    auto const completedTrainingStep = control[0];
    auto const snapshotValid = std::ranges::all_of(status, [](float value) { return std::isfinite(value); }) &&
                               completedTrainingStep == detail::kTotalTrainingStepCount && control[1] == 0u &&
                               control[2] == detail::kTrainingSchemaMagic && control[3] == 1u &&
                               status[2] == static_cast<float>(completedTrainingStep) && status[3] > 0.5f;
    if (!snapshotValid)
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refused artifact '{}' because the GPU-published completion state is invalid.",
            path.generic_string());
        return false;
    }

    auto const qualityReport = detail::makeHeldOutQualityReport(qualityBytes, status);
    if (!qualityReport.has_value())
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refused artifact '{}' because held-out quality telemetry is malformed.",
            path.generic_string());
        return false;
    }
    auto const qualityGate = evaluateNeuralAppearanceQuality(*qualityReport);
    if (!qualityGate.passed)
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refused artifact '{}' because the held-out quality gate failed: {}.",
            path.generic_string(), detail::formatNeuralAppearanceQualityViolations(qualityGate.violations));
        return false;
    }

    auto latentPlanes = std::array{
        std::vector<std::byte>(nr::neuralAppearance::v2LatentPlaneBytes),
        std::vector<std::byte>(nr::neuralAppearance::v2LatentPlaneBytes),
    };
    for (auto texel = std::size_t{0u}; texel < detail::kLatentTexelCount; ++texel)
    {
        auto const sourceBase = texel * 16u;
        std::memcpy(latentPlanes[0].data() + texel * 8u, latentBytes.data() + sourceBase, 8u);
        std::memcpy(latentPlanes[1].data() + texel * 8u, latentBytes.data() + sourceBase + 8u, 8u);
    }
    auto binding = nr::neuralAppearance::makeArtifactBindingContract(
        "glTF-Sample-Assets/Models/BoxTextured/glTF/BoxTextured.gltf");
    if (!binding.has_value())
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance could not construct the V2 artifact binding for '{}': {}", path.generic_string(),
            binding.error());
        return false;
    }
    auto write = nr::neuralAppearance::writeArtifactV2(nr::neuralAppearance::ArtifactWriteRequest{
        .destination = path,
        .bindingContract = *binding,
        .model = modelBytes,
        .latentPlanes = {std::span<const std::byte>{latentPlanes[0]}, std::span<const std::byte>{latentPlanes[1]}},
        .completedSteps = completedTrainingStep,
        .batchSize = detail::kTrainingBatchSize,
        .sampleCount = static_cast<std::uint64_t>(detail::kTrainingBatchSize) * detail::kTotalTrainingStepCount,
    });
    if (!write.has_value())
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance failed to publish V2 artifact '{}': {}", path.generic_string(), write.error());
        return false;
    }
    return true;
}

void NeuralAppearanceNode::shutdown(NodeShutdownContext &)
{
    if (runtime_)
    {
        auto pipelines = std::array{
            std::ref(runtime_->initializePipeline), std::ref(runtime_->targetPipeline),
            std::ref(runtime_->clearCoopGradientsPipeline), std::ref(runtime_->gradientPipeline),
            std::ref(runtime_->optimizePipeline),
            std::ref(runtime_->packPipeline),       std::ref(runtime_->viewerPipeline),
        };
        std::ranges::for_each(pipelines, [](auto pipeline) {
            if (pipeline.get())
            {
                pipeline.get()->clearBindingSets();
            }
        });
    }
    runtime_.reset();
}
} // namespace nr::renderPasses
