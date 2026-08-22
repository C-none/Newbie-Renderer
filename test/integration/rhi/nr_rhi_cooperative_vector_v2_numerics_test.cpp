import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
inline constexpr auto kParameterBytes = std::size_t{3'968u};
inline constexpr auto kParameterHalfCount = kParameterBytes / sizeof(std::uint16_t);
inline constexpr auto kLayerCount = std::size_t{5u};
inline constexpr auto kContributionCount = std::uint32_t{8u};
inline constexpr auto kForwardWidth = std::size_t{32u};
inline constexpr auto kAdamScalarCount = std::size_t{16u};
inline constexpr auto kForwardRtol = 1.0e-2f;
inline constexpr auto kForwardAtol = 2.0e-3f;
inline constexpr auto kGradientRtol = 2.0e-2f;
inline constexpr auto kGradientAtol = 5.0e-3f;

struct Layer
{
    std::uint32_t rows = 0u;
    std::uint32_t columns = 0u;
    std::uint32_t weightOffsetBytes = 0u;
    std::uint32_t rowStrideBytes = 0u;
    std::optional<std::uint32_t> biasOffsetBytes{};
};

inline constexpr auto kLayers = std::array{
    Layer{12u, 8u, 0u, 16u, 192u},
    Layer{32u, 8u, 256u, 16u, 768u},
    Layer{32u, 12u, 832u, 24u, std::nullopt},
    Layer{32u, 32u, 1600u, 64u, 3648u},
    Layer{3u, 32u, 3712u, 64u, 3904u},
};

struct CoopVecV2NumericsPushConstants
{
    std::array<std::uint32_t, kLayerCount> optimalWeightOffsets{};
    std::uint32_t contributionCount = kContributionCount;
    std::array<std::uint32_t, 3u> padding{};
};

struct PreparedComputeBindings
{
    nr::rhi::ShaderCursor root{};
    std::vector<nr::rhi::ShaderBindingSet> sets{};
    nr::rhi::DescriptorWriteCache writeCache{};
    nr::rhi::ShaderBindingSnapshot pushConstantSnapshot{};
};

static_assert(sizeof(CoopVecV2NumericsPushConstants) == 36u);

[[nodiscard]] std::uint16_t floatToHalf(float value) noexcept
{
    auto const bits = std::bit_cast<std::uint32_t>(value);
    auto const sign = static_cast<std::uint16_t>((bits >> 16u) & 0x8000u);
    auto exponent = static_cast<std::int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
    auto mantissa = bits & 0x007f'ffffu;
    if (exponent <= 0)
    {
        if (exponent < -10)
        {
            return sign;
        }
        mantissa = (mantissa | 0x0080'0000u) >> static_cast<std::uint32_t>(1 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + 0x0000'1000u) >> 13u));
    }
    if (exponent >= 31)
    {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    mantissa += 0x0000'1000u;
    if ((mantissa & 0x0080'0000u) != 0u)
    {
        mantissa = 0u;
        ++exponent;
    }
    if (exponent >= 31)
    {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10u) | (mantissa >> 13u));
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

[[nodiscard]] float fp16(float value) noexcept
{
    return halfToFloat(floatToHalf(value));
}

[[nodiscard]] float inputValue(std::uint32_t layer, std::uint32_t column) noexcept
{
    return fp16(static_cast<float>(layer + column + 1u) * (1.0f / 32.0f));
}

[[nodiscard]] float outputGradient(std::uint32_t layer, std::uint32_t row) noexcept
{
    return fp16(static_cast<float>(layer + row + 1u) * (1.0f / 128.0f));
}

[[nodiscard]] float weightValue(std::uint32_t layer, std::uint32_t row, std::uint32_t column) noexcept
{
    return fp16(static_cast<float>(layer + 1u) * (1.0f / 128.0f) +
                static_cast<float>(row + 1u) * (1.0f / 256.0f) -
                static_cast<float>(column + 1u) * (1.0f / 512.0f));
}

[[nodiscard]] float biasValue(std::uint32_t layer, std::uint32_t row) noexcept
{
    return fp16(static_cast<float>(layer + row + 1u) * (1.0f / 256.0f));
}

[[nodiscard]] std::vector<std::uint16_t> makeParameters()
{
    auto values = std::vector<std::uint16_t>(kParameterHalfCount);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, kLayers.size()), [&](std::size_t layerIndex) {
        auto const &layer = kLayers[layerIndex];
        for (auto row = 0u; row < layer.rows; ++row)
        {
            for (auto column = 0u; column < layer.columns; ++column)
            {
                auto const byteOffset = layer.weightOffsetBytes + row * layer.rowStrideBytes + column * 2u;
                values[byteOffset / 2u] = floatToHalf(weightValue(static_cast<std::uint32_t>(layerIndex), row, column));
            }
        }
        if (layer.biasOffsetBytes)
        {
            for (auto row = 0u; row < layer.rows; ++row)
            {
                auto const byteOffset = *layer.biasOffsetBytes + row * 2u;
                values[byteOffset / 2u] = floatToHalf(biasValue(static_cast<std::uint32_t>(layerIndex), row));
            }
        }
    });
    return values;
}

[[nodiscard]] nr::rhi::Buffer createGpuBuffer(nr::rhi::Device &device, vk::DeviceSize size, std::string_view name)
{
    auto createInfo = vk::BufferCreateInfo{};
    createInfo.size = size;
    createInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
                       vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    auto buffer = device.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::GpuOnly, name);
    nr::test::require(buffer.valid(), std::format("{} should allocate a valid GPU buffer", name));
    return buffer;
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeComputeUploadPlan(const nr::rhi::Device &device)
{
    auto const queueFamilies = device.queueManager.familyIndices();
    return nr::rhi::ops::makeTransferUploadOwnershipPlan(queueFamilies.transfer, queueFamilies.compute,
                                                         nr::rhi::ops::QueueAccessScope{
                                                             .stages = vk::PipelineStageFlagBits2::eAllCommands,
                                                             .access = vk::AccessFlagBits2::eMemoryRead |
                                                                       vk::AccessFlagBits2::eMemoryWrite,
                                                         });
}

void acquireUploadsOnCompute(nr::rhi::Device &device, std::span<const nr::rhi::ops::BufferUploadTicket> tickets)
{
    nr::test::require(!tickets.empty() &&
                          std::ranges::all_of(tickets, [](const auto &ticket) { return ticket.valid(); }),
                      "CoopVec numerical test uploads must all succeed before their compute acquire");
    nr::rhi::submitOneShot(device.device, device.queueManager.compute(),
                           nr::rhi::OneShotSyncPlan{
                               .waitSemaphore = *device.uploadReadback().uploadTimelineSemaphore(),
                               .waitStage = vk::PipelineStageFlagBits2::eAllCommands,
                               .waitValue = tickets.back().signalValue,
                           },
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               std::ranges::for_each(tickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
                                   device.uploadReadback().recordAcquireBarrier(commandBuffer, ticket);
                               });
                           });
}

void bindBuffer(const nr::rhi::ShaderCursor &root, std::string_view name, const nr::rhi::Buffer &buffer)
{
    auto cursor = root[name];
    nr::test::require(cursor.valid() && cursor.setObject(buffer),
                      std::format("{} should bind through shader reflection", name));
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
        .root = std::move(root),
        .sets = std::move(sets),
        .writeCache = std::move(writeCache),
    };
}

template <typename T>
void setPushConstants(PreparedComputeBindings &bindings, std::string_view name, const T &pushConstants)
{
    bindings.root.beginRecording();
    nr::test::require(bindings.root[name].setData(pushConstants),
                      "CoopVec numerical push constants should bind through reflection");
    bindings.pushConstantSnapshot = bindings.root.takeSnapshot();
}

void recordDispatch(const vk::raii::CommandBuffer &commandBuffer,
                    const nr::rhi::PipelineState<nr::rhi::ComputePipeline> &pipeline,
                    const PreparedComputeBindings &bindings, std::uint32_t groupCount)
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline.raw());
    nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer, vk::PipelineBindPoint::eCompute, pipeline.layout,
                                                  bindings.sets);
    nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipeline.layout, bindings.pushConstantSnapshot);
    commandBuffer.dispatch(groupCount, 1u, 1u);
}

void recordComputeToConversionBarrier(const vk::raii::CommandBuffer &commandBuffer)
{
    auto barrier = vk::MemoryBarrier2{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV;
    barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
    auto dependencyInfo = vk::DependencyInfo{};
    dependencyInfo.memoryBarrierCount = 1u;
    dependencyInfo.pMemoryBarriers = std::addressof(barrier);
    commandBuffer.pipelineBarrier2(dependencyInfo);
}

template <typename T>
[[nodiscard]] std::vector<T> readbackBuffer(nr::rhi::Device &device, const nr::rhi::Buffer &buffer,
                                             nr::rhi::ops::ReadbackSyncScope preCopy)
{
    nr::test::require(buffer.size() % sizeof(T) == 0u, "typed CoopVec numerical readback must be exactly sized");
    auto ticket = device.uploadReadback().readbackBuffer(
        buffer, 0u, buffer.size(), nr::rhi::QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = preCopy,
            .postCopy = nr::rhi::ops::ReadbackSyncScope{
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            },
        });
    auto const bytes = device.uploadReadback().readbackBytes(ticket);
    auto values = std::vector<T>(bytes.size() / sizeof(T));
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

void requireClose(float actual, float expected, float rtol, float atol, std::string_view label)
{
    auto const tolerance = atol + rtol * std::abs(expected);
    nr::test::require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
                      std::format("{} mismatch: actual={}, expected={}, tolerance={}", label, actual, expected,
                                  tolerance));
}

void requireForwardAndInputGradients(const std::vector<float> &forward, const std::vector<float> &inputGradients)
{
    nr::test::requireEqual(forward.size(), kLayerCount * kForwardWidth);
    nr::test::requireEqual(inputGradients.size(), kLayerCount * kForwardWidth);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, kLayers.size()), [&](std::size_t layerIndex) {
        auto const &layer = kLayers[layerIndex];
        for (auto row = 0u; row < layer.rows; ++row)
        {
            auto sum = layer.biasOffsetBytes ? biasValue(static_cast<std::uint32_t>(layerIndex), row) : 0.0f;
            for (auto column = 0u; column < layer.columns; ++column)
            {
                sum += weightValue(static_cast<std::uint32_t>(layerIndex), row, column) *
                       inputValue(static_cast<std::uint32_t>(layerIndex), column);
            }
            requireClose(forward[layerIndex * kForwardWidth + row], fp16(sum), kForwardRtol, kForwardAtol,
                         std::format("{} forward row {}", std::array{"F", "S", "D", "H", "O"}[layerIndex], row));
        }
        for (auto column = 0u; column < layer.columns; ++column)
        {
            auto sum = 0.0f;
            for (auto row = 0u; row < layer.rows; ++row)
            {
                sum += outputGradient(static_cast<std::uint32_t>(layerIndex), row) *
                       weightValue(static_cast<std::uint32_t>(layerIndex), row, column);
            }
            requireClose(inputGradients[layerIndex * kForwardWidth + column], fp16(sum), kGradientRtol,
                         kGradientAtol,
                         std::format("{} transpose input-gradient column {}",
                                     std::array{"F", "S", "D", "H", "O"}[layerIndex], column));
        }
    });
}

void requireConvertedGradients(const std::vector<std::uint16_t> &rowMajor, const std::vector<std::uint16_t> &bias)
{
    nr::test::requireEqual(rowMajor.size(), kParameterHalfCount);
    nr::test::requireEqual(bias.size(), kParameterHalfCount);
    std::ranges::for_each(std::views::iota(std::size_t{0u}, kLayers.size()), [&](std::size_t layerIndex) {
        auto const &layer = kLayers[layerIndex];
        for (auto row = 0u; row < layer.rows; ++row)
        {
            for (auto column = 0u; column < layer.columns; ++column)
            {
                auto const byteOffset = layer.weightOffsetBytes + row * layer.rowStrideBytes + column * 2u;
                auto const expected = static_cast<float>(kContributionCount) *
                                      outputGradient(static_cast<std::uint32_t>(layerIndex), row) *
                                      inputValue(static_cast<std::uint32_t>(layerIndex), column);
                requireClose(halfToFloat(rowMajor[byteOffset / 2u]), fp16(expected), kGradientRtol, kGradientAtol,
                             std::format("{} TrainingOptimal outer-product row {} column {}",
                                         std::array{"F", "S", "D", "H", "O"}[layerIndex], row, column));
            }
            if (layer.biasOffsetBytes)
            {
                auto const byteOffset = *layer.biasOffsetBytes + row * 2u;
                auto const expected = static_cast<float>(kContributionCount) *
                                      outputGradient(static_cast<std::uint32_t>(layerIndex), row);
                requireClose(halfToFloat(bias[byteOffset / 2u]), fp16(expected), kGradientRtol, kGradientAtol,
                             std::format("{} bias reduce row {}", std::array{"F", "S", "D", "H", "O"}[layerIndex],
                                         row));
            }
        }
    });
}

void requireAdamAndFp16Mirror(const std::vector<float> &master, const std::vector<float> &firstMoment,
                              const std::vector<float> &secondMoment, const std::vector<std::uint16_t> &mirror)
{
    nr::test::requireEqual(master.size(), kAdamScalarCount);
    nr::test::requireEqual(firstMoment.size(), kAdamScalarCount);
    nr::test::requireEqual(secondMoment.size(), kAdamScalarCount);
    nr::test::requireEqual(mirror.size(), kAdamScalarCount);
    for (auto scalar = 0u; scalar < kAdamScalarCount; ++scalar)
    {
        auto const initialMaster = static_cast<float>(scalar + 1u) * (1.0f / 16.0f);
        auto const initialFirstMoment = static_cast<float>(scalar + 1u) * (1.0f / 2048.0f);
        auto const initialSecondMoment = static_cast<float>(scalar + 1u) * (1.0f / 32768.0f);
        auto const gradient = static_cast<float>(scalar + 1u) * (1.0f / 256.0f);
        auto const expectedFirst = 0.9f * initialFirstMoment + 0.1f * gradient;
        auto const expectedSecond = 0.999f * initialSecondMoment + 0.001f * gradient * gradient;
        auto const expectedMaster = initialMaster - 0.00390625f * (expectedFirst / 0.1f) /
            (std::sqrt(expectedSecond / 0.001f) + 1.0e-7f);
        requireClose(master[scalar], expectedMaster, kGradientRtol, kGradientAtol,
                     std::format("Adam master scalar {}", scalar));
        requireClose(firstMoment[scalar], expectedFirst, kGradientRtol, kGradientAtol,
                     std::format("Adam first moment scalar {}", scalar));
        requireClose(secondMoment[scalar], expectedSecond, kGradientRtol, kGradientAtol,
                     std::format("Adam second moment scalar {}", scalar));
        requireClose(halfToFloat(mirror[scalar]), fp16(expectedMaster), kGradientRtol, kGradientAtol,
                     std::format("Adam FP16 mirror scalar {}", scalar));
    }
}

const nr::test::CaseRegistrar cooperativeVectorV2NumericsCase{
    "rhi cooperative-vector V2 affine numerics agree with FP32 references", [] {
        auto device = nr::rhi::Device::create("nr_rhi_cooperative_vector_v2_numerics_test", "NewbieRenderer");
        auto const &capabilities = device.cooperativeVectorCapabilities();
        nr::test::require(capabilities.computeStage && capabilities.trainingFloat16Accumulation &&
                              capabilities.fullFloat16TupleWithTranspose,
                          "V2 CoopVec numerical acceptance requires the admitted FP16 compute/training/transpose tuple");

        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();
        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/neuralAppearance/coopVecV2Numerics"},
        });
        nr::test::require(program.valid() && program.entryPoint() != nullptr &&
                              program.entryPoint()->stage == SLANG_STAGE_COMPUTE,
                          "V2 CoopVec numerical shader must compile as a compute entry point");
        auto pipelineBuild = device.pipeline().createComputePipeline(program, {}, 64u, {}, "coopvec_v2_numerics");
        auto pipeline = pipelineBuild.get();

        auto rowMajorDescs = std::array<nr::rhi::CooperativeVectorMatrixDesc, kLayerCount>{};
        auto optimalDescs = std::array<nr::rhi::CooperativeVectorMatrixDesc, kLayerCount>{};
        auto rowMajorSizes = std::array<nr::rhi::CooperativeVectorMatrixLayoutSize, kLayerCount>{};
        auto optimalSizes = std::array<nr::rhi::CooperativeVectorMatrixLayoutSize, kLayerCount>{};
        auto optimalOffsets = std::array<vk::DeviceSize, kLayerCount>{};
        auto optimalBytes = vk::DeviceSize{0u};
        std::ranges::for_each(std::views::iota(std::size_t{0u}, kLayers.size()), [&](std::size_t layerIndex) {
            auto const &layer = kLayers[layerIndex];
            rowMajorDescs[layerIndex] = nr::rhi::CooperativeVectorMatrixDesc{
                .rows = layer.rows,
                .columns = layer.columns,
                .layout = nr::rhi::CooperativeVectorMatrixLayout::RowMajor,
                .rowStrideBytes = layer.rowStrideBytes,
            };
            optimalDescs[layerIndex] = nr::rhi::CooperativeVectorMatrixDesc{
                .rows = layer.rows,
                .columns = layer.columns,
                .layout = nr::rhi::CooperativeVectorMatrixLayout::TrainingOptimal,
            };
            rowMajorSizes[layerIndex] =
                nr::rhi::queryCooperativeVectorMatrixLayoutSize(device.device, rowMajorDescs[layerIndex]);
            optimalSizes[layerIndex] =
                nr::rhi::queryCooperativeVectorMatrixLayoutSize(device.device, optimalDescs[layerIndex]);
            optimalBytes = (optimalBytes + nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment - 1u) &
                           ~(nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment - 1u);
            optimalOffsets[layerIndex] = optimalBytes;
            optimalBytes += optimalSizes[layerIndex].byteSize;
        });

        auto parameters = makeParameters();
        auto const parameterBuffer = createGpuBuffer(device, kParameterBytes, "coopvec_v2_parameters");
        auto const optimalBuffer = createGpuBuffer(device, optimalBytes, "coopvec_v2_optimal_gradients");
        auto const rowMajorBuffer = createGpuBuffer(device, kParameterBytes, "coopvec_v2_row_major_gradients");
        auto const biasBuffer = createGpuBuffer(device, kParameterBytes, "coopvec_v2_bias_gradients");
        auto const forwardBuffer = createGpuBuffer(device, kLayerCount * kForwardWidth * sizeof(float),
                                                   "coopvec_v2_forward_results");
        auto const inputGradientBuffer = createGpuBuffer(device, kLayerCount * kForwardWidth * sizeof(float),
                                                         "coopvec_v2_input_gradients");
        auto const adamMasterBuffer = createGpuBuffer(device, kAdamScalarCount * sizeof(float), "coopvec_v2_adam_master");
        auto const adamFirstBuffer = createGpuBuffer(device, kAdamScalarCount * sizeof(float), "coopvec_v2_adam_first");
        auto const adamSecondBuffer = createGpuBuffer(device, kAdamScalarCount * sizeof(float), "coopvec_v2_adam_second");
        auto const adamMirrorBuffer = createGpuBuffer(device, kAdamScalarCount * sizeof(std::uint16_t),
                                                      "coopvec_v2_adam_fp16_mirror");

        auto zeroOptimal = std::vector<std::byte>(static_cast<std::size_t>(optimalBytes));
        auto zeroParameters = std::vector<std::uint16_t>(kParameterHalfCount);
        auto adamMaster = std::array<float, kAdamScalarCount>{};
        auto adamFirst = std::array<float, kAdamScalarCount>{};
        auto adamSecond = std::array<float, kAdamScalarCount>{};
        std::ranges::for_each(std::views::iota(std::size_t{0u}, kAdamScalarCount), [&](std::size_t scalar) {
            adamMaster[scalar] = static_cast<float>(scalar + 1u) * (1.0f / 16.0f);
            adamFirst[scalar] = static_cast<float>(scalar + 1u) * (1.0f / 2048.0f);
            adamSecond[scalar] = static_cast<float>(scalar + 1u) * (1.0f / 32768.0f);
        });

        auto const uploadPlan = makeComputeUploadPlan(device);
        auto uploads = std::vector<nr::rhi::ops::BufferUploadTicket>{};
        uploads.reserve(7u);
        uploads.push_back(device.uploadReadback().uploadBuffer(std::as_bytes(std::span{parameters}), parameterBuffer,
                                                               0u, uploadPlan));
        uploads.push_back(device.uploadReadback().uploadBuffer(std::span{zeroOptimal}, optimalBuffer, 0u, uploadPlan));
        uploads.push_back(device.uploadReadback().uploadBuffer(std::as_bytes(std::span{zeroParameters}), rowMajorBuffer,
                                                               0u, uploadPlan));
        uploads.push_back(device.uploadReadback().uploadBuffer(std::as_bytes(std::span{zeroParameters}), biasBuffer,
                                                               0u, uploadPlan));
        uploads.push_back(device.uploadReadback().uploadBuffer(std::as_bytes(std::span{adamMaster}), adamMasterBuffer,
                                                               0u, uploadPlan));
        uploads.push_back(device.uploadReadback().uploadBuffer(std::as_bytes(std::span{adamFirst}), adamFirstBuffer,
                                                               0u, uploadPlan));
        uploads.push_back(device.uploadReadback().uploadBuffer(std::as_bytes(std::span{adamSecond}), adamSecondBuffer,
                                                               0u, uploadPlan));
        acquireUploadsOnCompute(device, uploads);

        auto bindings = prepareBindings(pipeline, [&](const nr::rhi::ShaderCursor &root) {
            bindBuffer(root, "gParameters", parameterBuffer);
            bindBuffer(root, "gOptimalWeightGradients", optimalBuffer);
            bindBuffer(root, "gBiasGradients", biasBuffer);
            bindBuffer(root, "gForwardResults", forwardBuffer);
            bindBuffer(root, "gInputGradientResults", inputGradientBuffer);
            bindBuffer(root, "gAdamMaster", adamMasterBuffer);
            bindBuffer(root, "gAdamFirstMoment", adamFirstBuffer);
            bindBuffer(root, "gAdamSecondMoment", adamSecondBuffer);
            bindBuffer(root, "gAdamFp16Mirror", adamMirrorBuffer);
        });
        auto pushConstants = CoopVecV2NumericsPushConstants{};
        std::ranges::transform(optimalOffsets, pushConstants.optimalWeightOffsets.begin(), [](vk::DeviceSize offset) {
            return static_cast<std::uint32_t>(offset);
        });
        setPushConstants(bindings, "gNumerics", pushConstants);

        nr::rhi::submitOneShot(device.device, device.queueManager.compute(), {},
                               [&](const vk::raii::CommandBuffer &commandBuffer) {
                                   recordDispatch(commandBuffer, pipeline, bindings,
                                                  static_cast<std::uint32_t>(kLayerCount) * kContributionCount);
                                   recordComputeToConversionBarrier(commandBuffer);
                                   std::ranges::for_each(
                                       std::views::iota(std::size_t{0u}, kLayerCount), [&](std::size_t layerIndex) {
                                           nr::rhi::recordCooperativeVectorMatrixConversion(
                                               commandBuffer,
                                               nr::rhi::CooperativeVectorMatrixMemory{
                                                   .deviceAddress =
                                                       optimalBuffer.deviceAddress() + optimalOffsets[layerIndex],
                                                   .size = optimalSizes[layerIndex].byteSize,
                                               },
                                               optimalDescs[layerIndex], optimalSizes[layerIndex],
                                               nr::rhi::CooperativeVectorMatrixMemory{
                                                   .deviceAddress = rowMajorBuffer.deviceAddress() +
                                                                    kLayers[layerIndex].weightOffsetBytes,
                                                   .size = rowMajorSizes[layerIndex].byteSize,
                                               },
                                               rowMajorDescs[layerIndex], rowMajorSizes[layerIndex]);
                                       });
                               });

        auto const shaderWriteScope = nr::rhi::ops::ReadbackSyncScope{
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
        };
        auto const conversionWriteScope = nr::rhi::ops::ReadbackSyncScope{
            .stages = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV,
            .access = vk::AccessFlagBits2::eTransferWrite,
        };
        auto const forward = readbackBuffer<float>(device, forwardBuffer, shaderWriteScope);
        auto const inputGradients = readbackBuffer<float>(device, inputGradientBuffer, shaderWriteScope);
        auto const rowMajor = readbackBuffer<std::uint16_t>(device, rowMajorBuffer, conversionWriteScope);
        auto const bias = readbackBuffer<std::uint16_t>(device, biasBuffer, shaderWriteScope);
        auto const finalMaster = readbackBuffer<float>(device, adamMasterBuffer, shaderWriteScope);
        auto const finalFirst = readbackBuffer<float>(device, adamFirstBuffer, shaderWriteScope);
        auto const finalSecond = readbackBuffer<float>(device, adamSecondBuffer, shaderWriteScope);
        auto const mirror = readbackBuffer<std::uint16_t>(device, adamMirrorBuffer, shaderWriteScope);

        requireForwardAndInputGradients(forward, inputGradients);
        requireConvertedGradients(rowMajor, bias);
        requireAdamAndFp16Mirror(finalMaster, finalFirst, finalSecond, mirror);
        device.waitIdle();
    }};
} // namespace
