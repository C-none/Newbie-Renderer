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
inline constexpr auto kImageThreadGroupSize = 8u;
inline constexpr auto kGradientThreadGroupSize = 32u;
inline constexpr auto kTrainingThreadGroupSize = 64u;
inline constexpr auto kTrainingPairSlotCount = 8u;
// Measured convergence: the EMA safe-log loss reaches its floor near step 4,096
// and then oscillates without trend for the rest of the run. The budget keeps a
// 4x margin over that floor instead of the former 131,072 steps.
inline constexpr auto kTotalTrainingStepCount = 16384u;
// Sample buffers are sized for the shader-side capacity so the active batch stays
// a host-side experiment knob rather than an ABI change.
inline constexpr auto kMaxTrainingBatchSize = 512u;
inline constexpr auto kTrainingBatchSize = 64u;
inline constexpr auto kHeldOutQualitySamplesPerStratum = 64u;
inline constexpr auto kHeldOutQualityStratumCount = 3u;
inline constexpr auto kHeldOutQualitySampleCount =
    kHeldOutQualitySamplesPerStratum * kHeldOutQualityStratumCount;
inline constexpr auto kAffineLayerCount = 8u;
inline constexpr auto kParameterChunkCount = 897u;
inline constexpr auto kInitialLearningRate = 0.001f;
inline constexpr auto kFinalLearningRate = 0.00001f;
inline constexpr auto kAdamBeta1 = 0.9f;
inline constexpr auto kAdamBeta2 = 0.999f;
inline constexpr auto kAdamEpsilon = 1.0e-7f;
// Per-element clamp on the batch-normalized gradient; zero disables clipping.
inline constexpr auto kGradientClip = 0.01f;
inline constexpr auto kTrainingSchemaMagic = 0x4E415433u;
inline constexpr auto kTrainingCheckpointMagic = 0x4E415443u;
inline constexpr auto kTrainingCheckpointVersion = 5u;

inline constexpr auto kParameterBufferBytes =
    static_cast<vk::DeviceSize>(kParameterChunkCount) * sizeof(std::array<float, 4u>);
inline constexpr auto kInferenceParameterBufferBytes = vk::DeviceSize{7360u};
inline constexpr auto kModelMomentBufferBytes = 2u * kParameterBufferBytes;
inline constexpr auto kSampleMetricBufferBytes =
    static_cast<vk::DeviceSize>(kMaxTrainingBatchSize) * sizeof(std::array<float, 4u>);
// Two records per sample: the projected diffuse lobe and the projected specular lobe.
inline constexpr auto kSampleTargetBufferBytes = 2u * kSampleMetricBufferBytes;
inline constexpr auto kTrainingStatusBufferBytes = 2u * sizeof(std::array<float, 4u>);
inline constexpr auto kTrainingControlBufferBytes = sizeof(std::array<std::uint32_t, 4u>);
inline constexpr auto kHeldOutQualitySampleBufferBytes =
    static_cast<vk::DeviceSize>(kHeldOutQualitySampleCount) * sizeof(std::array<float, 4u>);
inline constexpr auto kTrainingReadbackSyncPlan = nr::rhi::ops::ReadbackSyncPlan{
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

static_assert(kParameterBufferBytes == 14352u);
static_assert(kInferenceParameterBufferBytes == 7360u);
static_assert(kModelMomentBufferBytes == 28704u);
static_assert(kSampleMetricBufferBytes == 8192u);
static_assert(kSampleTargetBufferBytes == 16384u);
static_assert(kTrainingStatusBufferBytes == 32u);
static_assert(kTrainingControlBufferBytes == 16u);
static_assert(kHeldOutQualitySampleBufferBytes == 3072u);

struct NeuralAppearanceInitializePushConstants
{
    std::uint32_t trainingSeed = 0u;
};

struct NeuralAppearanceGradientPushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = 0u;
    std::uint32_t trainingSeed = 0u;
    std::array<std::uint32_t, kAffineLayerCount> optimalWeightOffsets{};
};

struct NeuralAppearanceTargetPushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = 0u;
    std::uint32_t trainingSeed = 0u;
};

struct NeuralAppearanceClearCoopGradientsPushConstants
{
    std::uint32_t optimalWeightGradientBytes = 0u;
    std::uint32_t biasGradientBytes = static_cast<std::uint32_t>(kInferenceParameterBufferBytes);
};

struct NeuralAppearanceOptimizePushConstants
{
    std::uint32_t trainingStep = 0u;
    std::uint32_t batchSize = 0u;
    std::uint32_t parameterChunkCount = kParameterChunkCount;
    std::uint32_t totalTrainingSteps = kTotalTrainingStepCount;
    std::uint32_t rowMajorGradientBaseOffset = 0u;
    float gradientClip = kGradientClip;
    float initialLearningRate = kInitialLearningRate;
    float finalLearningRate = kFinalLearningRate;
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
    std::uint32_t parameterChunkCount = kParameterChunkCount;
    std::uint32_t modelMomentChunkCount = 2u * kParameterChunkCount;
    std::uint32_t statusChunkCount = 2u;
    std::uint32_t controlWordCount = 4u;
    std::uint32_t totalTrainingStepCount = kTotalTrainingStepCount;
    std::uint32_t completedTrainingStep = 0u;
    // Persisted so a resumed run continues the same initialization and sample
    // stream instead of silently switching distributions.
    std::uint32_t trainingSeed = 0u;
};

static_assert(sizeof(NeuralAppearanceInitializePushConstants) == 4u);
static_assert(sizeof(NeuralAppearanceGradientPushConstants) == 44u);
static_assert(sizeof(NeuralAppearanceTargetPushConstants) == 12u);
static_assert(sizeof(NeuralAppearanceClearCoopGradientsPushConstants) == 8u);
static_assert(sizeof(NeuralAppearanceOptimizePushConstants) == 44u);
static_assert(sizeof(NeuralAppearancePushConstants) == 24u);
static_assert(sizeof(NeuralAppearanceTrainingCheckpointHeader) == 36u);
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
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> qualityPipeline{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> viewerPipeline{};

    nr::rhi::Buffer modelParameters{};
    nr::rhi::Buffer modelMoments{};
    nr::rhi::Buffer inferenceParameters{};
    nr::rhi::Buffer optimalWeightGradients{};
    nr::rhi::Buffer rowMajorWeightGradients{};
    nr::rhi::Buffer biasGradients{};
    nr::rhi::Buffer trainingStatus{};
    nr::rhi::Buffer trainingControl{};
    nr::rhi::Buffer heldOutQualitySamples{};

    nr::renderer::RetainedBufferState modelParameterState{};
    nr::renderer::RetainedBufferState modelMomentState{};
    nr::renderer::RetainedBufferState inferenceParameterState{};
    nr::renderer::RetainedBufferState optimalWeightGradientState{};
    nr::renderer::RetainedBufferState rowMajorWeightGradientState{};
    nr::renderer::RetainedBufferState biasGradientState{};
    nr::renderer::RetainedBufferState trainingStatusState{};
    nr::renderer::RetainedBufferState trainingControlState{};
    nr::renderer::RetainedBufferState heldOutQualitySampleState{};

    std::uint32_t nextTrainingStep = 1u;
    std::uint32_t trainingSeed = 0u;
    std::uint64_t lastDisplayOrdinal = 0u;
    std::array<nr::rhi::CooperativeVectorMatrixDesc, kAffineLayerCount> cooperativeGradientDescs{};
    std::array<nr::rhi::CooperativeVectorMatrixDesc, kAffineLayerCount> rowMajorGradientDescs{};
    std::array<nr::rhi::CooperativeVectorMatrixLayoutSize, kAffineLayerCount> cooperativeGradientLayoutSizes{};
    std::array<nr::rhi::CooperativeVectorMatrixLayoutSize, kAffineLayerCount> rowMajorGradientLayoutSizes{};
    std::array<vk::DeviceSize, kAffineLayerCount> cooperativeGradientOffsets{};
    vk::DeviceSize optimalWeightGradientBytes = 0u;
    vk::DeviceSize rowMajorWeightGradientBytes = 0u;
    std::uint32_t rowMajorGradientBasePadding = 0u;
};

struct NeuralAppearanceTrainingCheckpoint
{
    NeuralAppearanceTrainingCheckpointHeader header{};
    std::vector<std::byte> modelParameters{};
    std::vector<std::byte> modelMoments{};
    std::array<float, 8u> trainingStatus{};
    std::array<std::uint32_t, 4u> trainingControl{};
};

inline constexpr auto kTrainingCheckpointFileBytes =
    static_cast<std::uintmax_t>(sizeof(NeuralAppearanceTrainingCheckpointHeader)) + kParameterBufferBytes +
    kModelMomentBufferBytes + kTrainingStatusBufferBytes + kTrainingControlBufferBytes;

static_assert(kTrainingCheckpointFileBytes == 43140u);

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
    // Version 5 rejects checkpoints from the former direction distribution so a resumed run cannot mix rejection
    // sampling with the uniform-thetaH conditional-CDF profile. Step zero still requires initialization and cannot
    // be restored as a post-initialization snapshot.
    return header.magic == kTrainingCheckpointMagic && header.version == kTrainingCheckpointVersion &&
           header.parameterChunkCount == kParameterChunkCount &&
           header.modelMomentChunkCount == 2u * kParameterChunkCount && header.statusChunkCount == 2u &&
           header.controlWordCount == 4u && header.totalTrainingStepCount == kTotalTrainingStepCount &&
           header.completedTrainingStep > 0u && header.completedTrainingStep <= kTotalTrainingStepCount;
}

[[nodiscard]] bool trainingCheckpointPayloadValid(const NeuralAppearanceTrainingCheckpoint &checkpoint) noexcept
{
    auto const completedStep = checkpoint.header.completedTrainingStep;
    auto const controlFlagsValid = checkpoint.trainingControl[1] == 0u && checkpoint.trainingControl[3] == 1u;
    return trainingCheckpointHeaderValid(checkpoint.header) &&
           checkpoint.modelParameters.size() == kParameterBufferBytes &&
           checkpoint.modelMoments.size() == kModelMomentBufferBytes && finiteFp32Bytes(checkpoint.modelParameters) &&
           finiteFp32Bytes(checkpoint.modelMoments) &&
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
    if (!readBytes(input, checkpoint.modelParameters) || !readBytes(input, checkpoint.modelMoments) ||
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

// Rows are outputs and columns are inputs, matching the eight-affine topology
// E1(12->32), E2(32->32), E3(32->8), F(8->6), S(8->32), D(8->32), H(32->32),
// O(32->6) declared in shader/include/neuralAppearance/layout.slang.
[[nodiscard]] constexpr std::array<nr::rhi::CooperativeVectorMatrixDesc, kAffineLayerCount>
neuralAppearanceCooperativeGradientDescs() noexcept
{
    using Layout = nr::rhi::CooperativeVectorMatrixLayout;
    return {
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 12u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 32u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 8u, .columns = 32u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 6u, .columns = 8u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 8u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 8u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 32u, .layout = Layout::TrainingOptimal},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 6u, .columns = 32u, .layout = Layout::TrainingOptimal},
    };
}

[[nodiscard]] constexpr std::array<nr::rhi::CooperativeVectorMatrixDesc, kAffineLayerCount>
neuralAppearanceCooperativeRowMajorGradientDescs() noexcept
{
    using Layout = nr::rhi::CooperativeVectorMatrixLayout;
    return {
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 12u, .layout = Layout::RowMajor, .rowStrideBytes = 24u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 32u, .layout = Layout::RowMajor, .rowStrideBytes = 64u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 8u, .columns = 32u, .layout = Layout::RowMajor, .rowStrideBytes = 64u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 6u, .columns = 8u, .layout = Layout::RowMajor, .rowStrideBytes = 16u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 8u, .layout = Layout::RowMajor, .rowStrideBytes = 16u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 8u, .layout = Layout::RowMajor, .rowStrideBytes = 16u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 32u, .columns = 32u, .layout = Layout::RowMajor, .rowStrideBytes = 64u},
        nr::rhi::CooperativeVectorMatrixDesc{.rows = 6u, .columns = 32u, .layout = Layout::RowMajor, .rowStrideBytes = 64u},
    };
}

inline constexpr auto kRowMajorGradientOffsets =
    std::array<vk::DeviceSize, kAffineLayerCount>{0u, 832u, 2944u, 3520u, 3712u, 4288u, 4800u, 6912u};

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
    auto const queueFamilies = device.queueManager.familyIndices();
    return nr::rhi::ops::makeTransferUploadOwnershipPlan(queueFamilies.transfer, queueFamilies.compute,
                                                         nr::rhi::ops::QueueAccessScope{
                                                             .stages = vk::PipelineStageFlagBits2::eComputeShader,
                                                             .access = vk::AccessFlagBits2::eShaderStorageRead |
                                                                       vk::AccessFlagBits2::eShaderStorageWrite,
                                                         });
}

void synchronizeTrainingCheckpointUploads(nr::rhi::Device &device,
                                          std::span<const nr::rhi::ops::BufferUploadTicket> tickets)
{
    nr::nrAssert(
        !tickets.empty() &&
            std::ranges::all_of(tickets, [](const nr::rhi::ops::BufferUploadTicket &ticket) { return ticket.valid(); }),
        "NeuralAppearance checkpoint restore requires valid upload tickets.");

    auto &uploadReadback = device.uploadReadback();
    auto signalValue = std::uint64_t{0u};
    std::ranges::for_each(tickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
        signalValue = std::max(signalValue, ticket.signalValue);
    });
    nr::rhi::submitOneShot(device.device, device.queueManager.compute(),
                           nr::rhi::OneShotSyncPlan{
                               .waitSemaphore = *uploadReadback.uploadTimelineSemaphore(),
                               .waitStage = vk::PipelineStageFlagBits2::eComputeShader,
                               .waitValue = signalValue,
                           },
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               std::ranges::for_each(tickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
                                   uploadReadback.recordAcquireBarrier(commandBuffer, ticket);
                               });
                           });
    uploadReadback.reclaimCompletedUploads();
}

[[nodiscard]] std::shared_ptr<NeuralAppearanceRuntimeCache> makeRuntime(nr::rhi::Device &device,
                                                                        std::span<const nr::rhi::SlangProgram> programs,
                                                                        std::string_view runtimeName)
{
    nr::nrAssert(
        programs.size() == 7u,
        "NeuralAppearance runtime requires initialize, target, clear, gradient, optimize, quality, and viewer programs.");

    auto runtime = std::make_shared<NeuralAppearanceRuntimeCache>();
    runtime->initializePipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->targetPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->clearCoopGradientsPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->gradientPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->optimizePipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->qualityPipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
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
    runtime->qualityPipeline->initialize(device.pipeline().createComputePipeline(
        programs[5], {}, 64u, {}, std::format("{}.EvaluateQualityPipeline", runtimeName)));
    runtime->viewerPipeline->initialize(device.pipeline().createComputePipeline(
        programs[6], {}, 64u, {}, std::format("{}.ViewerPipeline", runtimeName)));

    runtime->modelParameters = createTrainingBuffer(device, kParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                    "NeuralAppearance.ModelParameters");
    runtime->modelMoments = createTrainingBuffer(device, kModelMomentBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
                                                 "NeuralAppearance.ModelMoments");
    runtime->inferenceParameters = createTrainingBuffer(
        device, kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly, "NeuralAppearance.InferenceParameters");
    runtime->cooperativeGradientDescs = neuralAppearanceCooperativeGradientDescs();
    runtime->rowMajorGradientDescs = neuralAppearanceCooperativeRowMajorGradientDescs();
    std::ranges::for_each(std::views::iota(std::size_t{0u}, runtime->cooperativeGradientDescs.size()),
                          [&](std::size_t index) {
                              auto const size = nr::rhi::queryCooperativeVectorMatrixLayoutSize(
                                  device.device, runtime->cooperativeGradientDescs[index]);
                              runtime->cooperativeGradientLayoutSizes[index] = size;
                              runtime->rowMajorGradientLayoutSizes[index] =
                                  nr::rhi::queryCooperativeVectorMatrixLayoutSize(
                                      device.device, runtime->rowMajorGradientDescs[index]);
                              runtime->cooperativeGradientOffsets[index] = alignUp(
                                  runtime->optimalWeightGradientBytes,
                                  nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment);
                              runtime->optimalWeightGradientBytes =
                                  runtime->cooperativeGradientOffsets[index] + size.byteSize;
                          });
    // The allocator only guarantees its own buffer alignment, so reserve one extra
    // alignment window and shift every matrix offset by the measured base
    // misalignment. Every TrainingOptimal device address then satisfies the
    // extension requirement regardless of how the allocation was suballocated.
    runtime->optimalWeightGradientBytes =
        alignUp(runtime->optimalWeightGradientBytes, nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment) +
        nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment;
    runtime->optimalWeightGradients = createTrainingBuffer(
        device, runtime->optimalWeightGradientBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.OptimalWeightGradients");
    runtime->rowMajorWeightGradientBytes =
        kInferenceParameterBufferBytes + nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment;
    runtime->rowMajorWeightGradients = createTrainingBuffer(
        device, runtime->rowMajorWeightGradientBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.RowMajorWeightGradients");
    runtime->biasGradients = createTrainingBuffer(
        device, kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.BiasGradients");
    auto const gradientBaseAddress = runtime->optimalWeightGradients.deviceAddress();
    auto const gradientBasePadding =
        (nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment -
         gradientBaseAddress % nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment) %
        nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment;
    std::ranges::for_each(runtime->cooperativeGradientOffsets,
                          [gradientBasePadding](vk::DeviceSize &offset) { offset += gradientBasePadding; });
    auto const rowMajorBaseAddress = runtime->rowMajorWeightGradients.deviceAddress();
    runtime->rowMajorGradientBasePadding = static_cast<std::uint32_t>(
        (nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment -
         rowMajorBaseAddress % nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment) %
        nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment);
    nr::nrAssert(std::ranges::all_of(runtime->cooperativeGradientOffsets,
                                     [&](vk::DeviceSize offset) {
                                         return (gradientBaseAddress + offset) %
                                                    nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment ==
                                                0u;
                                     }) &&
                     (rowMajorBaseAddress + runtime->rowMajorGradientBasePadding) %
                             nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment ==
                         0u,
                 "NeuralAppearance cooperative-vector gradient matrices must have 64-byte device address alignment.");
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
// The published numbers are the only quantitative record of how well a run fits
// the reflective base surface, so they are reported whether the gate passes or
// fails. A pass-only failure log would leave a successful run unmeasurable.
void logHeldOutQualityReport(const NeuralAppearanceQualityReport &report)
{
    nr::nrLog<nr::LogLevel::info, "NEURAL-QUALITY">(
        "held-out quality: emaSafeLogLoss={:.6f} initialSafeLogLoss={:.6f} outputsFinite={} outputsNonnegative={}",
        report.emaSafeLogLoss, report.initialSafeLogLoss, report.outputsFinite, report.outputsNonnegative);
    auto const strata = std::array{
        std::pair{std::string_view{"overall"}, std::cref(report.overall)},
        std::pair{std::string_view{"uniform"}, std::cref(report.uniform)},
        std::pair{std::string_view{"highlight"}, std::cref(report.highlight)},
        std::pair{std::string_view{"grazing"}, std::cref(report.grazing)},
    };
    std::ranges::for_each(strata, [](const auto &entry) {
        auto const &stratum = entry.second.get();
        nr::nrLog<nr::LogLevel::info, "NEURAL-QUALITY">(
            "held-out stratum {}: samples={} fp32Mean={:.6f} fp32P95={:.6f} fp16Mean={:.6f} fp16P95={:.6f} "
            "zeroMean={:.6f} zeroP95={:.6f} fp16OverFp32={:.4f} fp32OverZero={:.4f}",
            entry.first, stratum.fp32Master.sampleCount, stratum.fp32Master.meanSafeLogLoss,
            stratum.fp32Master.percentile95SafeLogLoss, stratum.fp16CooperativeVector.meanSafeLogLoss,
            stratum.fp16CooperativeVector.percentile95SafeLogLoss, stratum.zeroPrediction.meanSafeLogLoss,
            stratum.zeroPrediction.percentile95SafeLogLoss,
            stratum.fp32Master.meanSafeLogLoss > 0.0f
                ? stratum.fp16CooperativeVector.meanSafeLogLoss / stratum.fp32Master.meanSafeLogLoss
                : 0.0f,
            stratum.zeroPrediction.meanSafeLogLoss > 0.0f
                ? stratum.fp32Master.meanSafeLogLoss / stratum.zeroPrediction.meanSafeLogLoss
                : 0.0f);
    });
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
NeuralAppearanceNode::NeuralAppearanceNode(bool comparisonEnabled, std::uint32_t trainingSeed) noexcept
    : comparisonEnabled_(comparisonEnabled), trainingSeed_(trainingSeed)
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
            .sourcePath = std::filesystem::path{"renderer/neuralAppearance/evaluateQuality"},
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
    runtime_->trainingSeed = trainingSeed_;
}

void NeuralAppearanceNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->initializePipeline && runtime_->initializePipeline->valid() &&
                     runtime_->targetPipeline && runtime_->targetPipeline->valid() && runtime_->clearCoopGradientsPipeline &&
                     runtime_->clearCoopGradientsPipeline->valid() && runtime_->gradientPipeline &&
                     runtime_->gradientPipeline->valid() && runtime_->optimizePipeline &&
                     runtime_->optimizePipeline->valid() && runtime_->qualityPipeline &&
                     runtime_->qualityPipeline->valid() &&
                     runtime_->viewerPipeline && runtime_->viewerPipeline->valid(),
                 "NeuralAppearance async compute PSO construction failed.");
}

void NeuralAppearanceNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(runtime_ && runtime_->initializePipeline && runtime_->targetPipeline && runtime_->clearCoopGradientsPipeline && runtime_->gradientPipeline &&
                     runtime_->optimizePipeline && runtime_->qualityPipeline && runtime_->viewerPipeline,
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
    auto inferenceParameters = detail::importTrainingBuffer(
        context, runtime_->inferenceParameters, runtime_->inferenceParameterState,
        "NeuralAppearance.InferenceParameters",
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

    auto sampleMetrics =
        detail::makeTransientBuffer(context, "NeuralAppearance.SampleMetrics", detail::kSampleMetricBufferBytes);
    auto sampleTargets =
        detail::makeTransientBuffer(context, "NeuralAppearance.SampleTargets", detail::kSampleTargetBufferBytes);
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
        .storageBufferWrite("gTrainingStatus", trainingStatus, "NeuralAppearance.TrainingStatus")
        .storageBufferWrite("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
        .pushConstants("gInitialize", detail::NeuralAppearanceInitializePushConstants{
                                          .trainingSeed = runtime_->trainingSeed,
                                      })
        .record([](const nr::renderer::ComputePassRecordContext &computeContext) {
            computeContext.commandBuffer.dispatch(
                detail::divideRoundUp(
                    std::max(detail::kParameterChunkCount,
                             static_cast<std::uint32_t>(detail::kInferenceParameterBufferBytes / 4u)),
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
        activeTrainingPairCount = std::min(detail::kTrainingPairSlotCount, remainingTrainingSteps);
        runtime_->nextTrainingStep += activeTrainingPairCount;
    }

    auto const lastTrainingStep = std::min(runtime_->nextTrainingStep - 1u, detail::kTotalTrainingStepCount);
    auto trainingDispatches =
        std::array<detail::NeuralAppearanceGradientPushConstants, detail::kTrainingPairSlotCount>{};
    // Fixed pair slots preserve pass-binding owner ordinals; a zero batch makes an inactive slot a shader no-op.
    std::ranges::for_each(trainingDispatches,
                          [lastTrainingStep, seed = runtime_->trainingSeed](auto &dispatch) {
                              dispatch.trainingStep = lastTrainingStep;
                              dispatch.trainingSeed = seed;
                          });
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
            .trainingSeed = gradientPushConstants.trainingSeed,
        };
        auto targetPass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.EvaluateTargets.Pair{}", pairIndex), runtime_->targetPipeline};
        targetPass.storageBufferWrite("gSampleTargets", sampleTargets, "NeuralAppearance.SampleTargets")
            .pushConstants("gTraining", targetPushConstants)
            .record([batchSize = gradientPushConstants.batchSize](
                        const nr::renderer::ComputePassRecordContext &computeContext) {
                computeContext.commandBuffer.dispatch(
                    detail::divideRoundUp(batchSize, detail::kTrainingThreadGroupSize), 1u, 1u);
            });
        [[maybe_unused]] auto targetPassHandle = targetPass.build();

        auto clearPass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.ClearCoopGradients.Pair{}", pairIndex),
            runtime_->clearCoopGradientsPipeline};
        clearPass.storageBufferWrite("gOptimalWeightGradients", optimalWeightGradients,
                                     "NeuralAppearance.OptimalWeightGradients")
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
            .storageBufferWrite("gOptimalWeightGradients", optimalWeightGradients,
                                "NeuralAppearance.OptimalWeightGradients")
            .storageBufferWrite("gBiasGradients", biasGradients, "NeuralAppearance.BiasGradients")
            .storageBuffer("gSampleTargets", sampleTargets, "NeuralAppearance.SampleTargets")
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
                nr::nrAssert(recordContext.commandBuffer.has_value(),
                             "NeuralAppearance cooperative-vector conversion requires a command buffer.");
                std::ranges::for_each(
                    std::views::iota(std::size_t{0u}, runtime->rowMajorGradientDescs.size()), [&](std::size_t index) {
                        nr::rhi::recordCooperativeVectorMatrixConversion(
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
                                                 runtime->rowMajorGradientBasePadding +
                                                 detail::kRowMajorGradientOffsets[index],
                                .size = runtime->rowMajorGradientLayoutSizes[index].byteSize,
                            },
                            runtime->rowMajorGradientDescs[index], runtime->rowMajorGradientLayoutSizes[index]);
                    });
            }));

        auto const optimizePushConstants = detail::NeuralAppearanceOptimizePushConstants{
            .trainingStep = gradientPushConstants.trainingStep,
            .batchSize = gradientPushConstants.batchSize,
            .rowMajorGradientBaseOffset = runtime_->rowMajorGradientBasePadding,
        };
        auto optimizePass = nr::renderer::ComputePassBuilder{
            context, std::format("NeuralAppearance.OptimizeTraining.Pair{}", pairIndex), runtime_->optimizePipeline};
        optimizePass.storageBuffer("gSampleMetrics", sampleMetrics, "NeuralAppearance.SampleMetrics")
            .storageBufferReadWrite("gModelParameters", modelParameters, "NeuralAppearance.ModelParameters")
            .storageBufferReadWrite("gModelMoments", modelMoments, "NeuralAppearance.ModelMoments")
            .storageBufferReadWrite("gTrainingStatus", trainingStatus, "NeuralAppearance.TrainingStatus")
            .storageBufferReadWrite("gTrainingControl", trainingControl, "NeuralAppearance.TrainingControl")
            .storageBuffer("gRowMajorWeightGradients", rowMajorWeightGradients,
                           "NeuralAppearance.RowMajorWeightGradients")
            .storageBuffer("gBiasGradients", biasGradients, "NeuralAppearance.BiasGradients")
            .storageBufferWrite("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
            .pushConstants("gTraining", optimizePushConstants)
            .record([active = gradientPushConstants.batchSize >
                              0u](const nr::renderer::ComputePassRecordContext &computeContext) {
                auto const groupCount =
                    active ? detail::divideRoundUp(detail::kParameterChunkCount, detail::kTrainingThreadGroupSize) : 0u;
                computeContext.commandBuffer.dispatch(groupCount, 1u, 1u);
            });
        [[maybe_unused]] auto optimizePassHandle = optimizePass.build();
    });

    auto qualityPass =
        nr::renderer::ComputePassBuilder{context, "NeuralAppearance.EvaluateQuality", runtime_->qualityPipeline};
    qualityPass.storageBuffer("gModelParameters", modelParameters, "NeuralAppearance.ModelParameters")
        .storageBuffer("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
        .storageBufferWrite("gHeldOutQualitySamples", heldOutQualitySamples,
                            "NeuralAppearance.HeldOutQualitySamples")
        .record([trainingFinished = lastTrainingStep >= detail::kTotalTrainingStepCount](
                    const nr::renderer::ComputePassRecordContext &computeContext) {
            computeContext.commandBuffer.dispatch(
                trainingFinished ? detail::divideRoundUp(detail::kHeldOutQualitySampleCount,
                                                         detail::kTrainingThreadGroupSize)
                                 : 0u,
                1u, 1u);
        });
    [[maybe_unused]] auto qualityPassHandle = qualityPass.build();

    auto const viewerPushConstants = detail::NeuralAppearancePushConstants{
        .width = outputExtent.width,
        .height = outputExtent.height,
        .frameIndex = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(frameOrdinal, std::numeric_limits<std::uint32_t>::max())),
        .totalTrainingSteps = detail::kTotalTrainingStepCount,
        .comparisonEnabled = comparisonEnabled_ ? 1u : 0u,
    };
    auto viewerPass = nr::renderer::ComputePassBuilder{context, "NeuralAppearance.Viewer", runtime_->viewerPipeline};
    viewerPass.storageBuffer("gInferenceParameters", inferenceParameters, "NeuralAppearance.InferenceParameters")
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

std::uint32_t NeuralAppearanceNode::totalTrainingStepCount() noexcept
{
    return detail::kTotalTrainingStepCount;
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
    auto &readback = device.uploadReadback();
    auto modelParameterTicket = readback.readbackBuffer(runtime_->modelParameters, 0u, detail::kParameterBufferBytes,
                                                        nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto modelMomentTicket = readback.readbackBuffer(runtime_->modelMoments, 0u, detail::kModelMomentBufferBytes,
                                                     nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto statusTicket = readback.readbackBuffer(runtime_->trainingStatus, 0u, detail::kTrainingStatusBufferBytes,
                                                nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto controlTicket = readback.readbackBuffer(runtime_->trainingControl, 0u, detail::kTrainingControlBufferBytes,
                                                 nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);

    auto checkpoint = detail::NeuralAppearanceTrainingCheckpoint{};
    checkpoint.modelParameters = readback.readbackBytes(modelParameterTicket);
    checkpoint.modelMoments = readback.readbackBytes(modelMomentTicket);
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
    checkpoint.header.trainingSeed = runtime_->trainingSeed;
    // Checkpoints are the only periodic host-visible sample of training state, so
    // publishing the telemetry here yields a 512-step-granularity loss curve for
    // free instead of requiring an extra readback.
    nr::nrLog<nr::LogLevel::info, "NEURAL-TRAINING">(
        "training telemetry: step={} emaSafeLogLoss={:.6f} initialSafeLogLoss={:.6f} learningRate={:.3e} "
        "trainingLoss={:.6f} finite={}",
        checkpoint.header.completedTrainingStep, checkpoint.trainingStatus[0], checkpoint.trainingStatus[1],
        checkpoint.trainingStatus[4], checkpoint.trainingStatus[5], checkpoint.trainingStatus[3] > 0.5f);
    if (!detail::trainingCheckpointPayloadValid(checkpoint))
    {
        detail::reportCheckpointWarning(path, "GPU snapshot contains invalid values or an inconsistent completed step");
        return std::nullopt;
    }

    auto lock = std::scoped_lock{detail::trainingCheckpointFileMutex()};
    auto const slotPaths = detail::trainingCheckpointSlotPaths(path);
    auto slot0 = detail::readTrainingCheckpointSlot(slotPaths[0], false);
    auto slot1 = detail::readTrainingCheckpointSlot(slotPaths[1], false);
    auto const slot0IsNewer =
        slot0 && (!slot1 || slot0->header.completedTrainingStep > slot1->header.completedTrainingStep);
    auto const targetSlot = slot0IsNewer ? std::size_t{1u} : std::size_t{0u};
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
        auto const slot1IsNewer =
            slot1 && (!slot0 || slot1->header.completedTrainingStep > slot0->header.completedTrainingStep);
        checkpoint = slot1IsNewer ? std::move(slot1) : std::move(slot0);
    }
    if (!checkpoint)
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
    auto restoredInferenceParameters = detail::createTrainingBuffer(
        device, detail::kInferenceParameterBufferBytes, nr::rhi::MemoryUsage::GpuOnly,
        "NeuralAppearance.RestoredInferenceParameters");
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
    runtime_->inferenceParameters = std::move(restoredInferenceParameters);
    runtime_->trainingStatus = std::move(restoredTrainingStatus);
    runtime_->trainingControl = std::move(restoredTrainingControl);
    runtime_->heldOutQualitySamples = std::move(restoredHeldOutQualitySamples);
    runtime_->modelParameterState = {};
    runtime_->modelMomentState = {};
    runtime_->inferenceParameterState = {};
    runtime_->optimalWeightGradientState = {};
    runtime_->rowMajorWeightGradientState = {};
    runtime_->biasGradientState = {};
    runtime_->trainingStatusState = {};
    runtime_->trainingControlState = {};
    runtime_->heldOutQualitySampleState = {};
    runtime_->nextTrainingStep = checkpoint->header.completedTrainingStep + 1u;
    runtime_->trainingSeed = checkpoint->header.trainingSeed;
    runtime_->lastDisplayOrdinal = 0u;
    return true;
}

bool NeuralAppearanceNode::saveTrainingArtifact(nr::rhi::Device &device, const std::filesystem::path &path) const
{
    if (path.extension() != ".nart")
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refuses artifact '{}' because V3 production artifacts must use the .nart extension.",
            path.generic_string());
        return false;
    }
    if (!runtime_ || !trainingComplete())
    {
        return false;
    }

    device.waitIdle();
    auto &readback = device.uploadReadback();
    auto modelTicket = readback.readbackBuffer(runtime_->inferenceParameters, 0u, detail::kInferenceParameterBufferBytes,
                                               nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto statusTicket = readback.readbackBuffer(runtime_->trainingStatus, 0u, detail::kTrainingStatusBufferBytes,
                                                nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto controlTicket = readback.readbackBuffer(runtime_->trainingControl, 0u, detail::kTrainingControlBufferBytes,
                                                 nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto qualityTicket = readback.readbackBuffer(runtime_->heldOutQualitySamples, 0u,
                                                 detail::kHeldOutQualitySampleBufferBytes,
                                                 nr::rhi::QueueRole::Compute, detail::kTrainingReadbackSyncPlan);
    auto modelBytes = readback.readbackBytes(modelTicket);
    auto statusBytes = readback.readbackBytes(statusTicket);
    auto controlBytes = readback.readbackBytes(controlTicket);
    auto qualityBytes = readback.readbackBytes(qualityTicket);
    if (modelBytes.size() != detail::kInferenceParameterBufferBytes ||
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
    if (!qualityReport)
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refused artifact '{}' because held-out quality telemetry is malformed.",
            path.generic_string());
        return false;
    }
    detail::logHeldOutQualityReport(*qualityReport);
    auto const qualityGate = evaluateNeuralAppearanceQuality(*qualityReport);
    if (!qualityGate.passed)
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance refused artifact '{}' because the held-out quality gate failed: {}.",
            path.generic_string(), detail::formatNeuralAppearanceQualityViolations(qualityGate.violations));
        return false;
    }

    auto write = nr::neuralAppearance::writeArtifactV3(nr::neuralAppearance::ArtifactWriteRequest{
        .destination = path,
        .model = modelBytes,
        .completedSteps = completedTrainingStep,
        .batchSize = detail::kTrainingBatchSize,
        .sampleCount = static_cast<std::uint64_t>(detail::kTrainingBatchSize) * detail::kTotalTrainingStepCount,
    });
    if (!write)
    {
        nr::nrLog<nr::LogLevel::warning, "NEURAL-ARTIFACT">(
            "NeuralAppearance failed to publish V3 artifact '{}': {}", path.generic_string(), write.error());
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
            std::ref(runtime_->qualityPipeline),    std::ref(runtime_->viewerPipeline),
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
