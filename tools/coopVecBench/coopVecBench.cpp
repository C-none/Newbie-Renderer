import std;
import dependency.assets;
import dependency.vulkan;
import nr.rhi;

// Cooperative-vector inference benchmark. Measures GPU time of an eight-layer
// square MLP for two widths and two matrix component types. Weights are random;
// only the cooperative-vector multiply differs between variants.

namespace
{
inline constexpr auto kLayerCount = std::size_t{8u};
inline constexpr auto kThreadsPerGroup = std::uint32_t{64u};
inline constexpr auto kElementCount = std::uint32_t{262'144u};
inline constexpr auto kIterationCount = std::uint32_t{8u};
inline constexpr auto kWarmupRuns = std::uint32_t{3u};
inline constexpr auto kMeasuredRuns = std::uint32_t{10u};
inline constexpr auto kMatrixAlignment = nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment;

struct BenchPushConstants
{
    std::array<std::uint32_t, kLayerCount> weightOffsets{};
    std::array<std::uint32_t, kLayerCount> biasOffsets{};
    std::uint32_t elementCount = kElementCount;
    std::uint32_t iterationCount = kIterationCount;
};

static_assert(sizeof(BenchPushConstants) == 72u);

struct Variant
{
    std::string_view name;
    std::string_view shaderPath;
    std::uint32_t width = 0u;
    nr::rhi::CooperativeVectorComponentType componentType = nr::rhi::CooperativeVectorComponentType::Float16;
};

inline constexpr auto kVariants = std::array{
    Variant{"width32 fp16", "renderer/coopVecBench/width32Float16", 32u,
            nr::rhi::CooperativeVectorComponentType::Float16},
    Variant{"width32 e4m3", "renderer/coopVecBench/width32FloatE4M3", 32u,
            nr::rhi::CooperativeVectorComponentType::FloatE4M3},
    Variant{"width64 fp16", "renderer/coopVecBench/width64Float16", 64u,
            nr::rhi::CooperativeVectorComponentType::Float16},
    Variant{"width64 e4m3", "renderer/coopVecBench/width64FloatE4M3", 64u,
            nr::rhi::CooperativeVectorComponentType::FloatE4M3},
};

struct VariantResult
{
    std::string_view name{};
    double bestMilliseconds = 0.0;
    double medianMilliseconds = 0.0;
    double teraFlopsPerSecond = 0.0;
    float sampleValue = 0.0f;
};

[[nodiscard]] vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

[[nodiscard]] std::uint16_t encodeFloat16(float value) noexcept
{
    return nr::dependency::imath::Half{value}.bits();
}

// E4M3 as defined by VK_COMPONENT_TYPE_FLOAT_E4M3_NV: four exponent bits with a
// bias of seven, three mantissa bits, no infinities, and 448 as the largest
// finite magnitude.
[[nodiscard]] std::uint8_t encodeFloatE4M3(float value) noexcept
{
    auto const bits = std::bit_cast<std::uint32_t>(value);
    auto const sign = static_cast<std::uint8_t>((bits >> 24u) & 0x80u);
    if (!std::isfinite(value))
    {
        return static_cast<std::uint8_t>(sign | 0x7fu);
    }

    auto exponent = static_cast<std::int32_t>((bits >> 23u) & 0xffu) - 127 + 7;
    auto mantissa = bits & 0x007f'ffffu;
    if (exponent <= 0)
    {
        if (exponent < -3)
        {
            return sign;
        }
        auto const subnormal = (mantissa | 0x0080'0000u) >> static_cast<std::uint32_t>(1 - exponent);
        auto const rounded = (subnormal + 0x000f'ffffu + ((subnormal >> 20u) & 1u)) >> 20u;
        return static_cast<std::uint8_t>(sign | rounded);
    }

    mantissa += 0x000f'ffffu + ((mantissa >> 20u) & 1u);
    if ((mantissa & 0x0080'0000u) != 0u)
    {
        mantissa = 0u;
        ++exponent;
    }
    if (exponent > 15 || (exponent == 15 && (mantissa >> 20u) >= 7u))
    {
        return static_cast<std::uint8_t>(sign | 0x7eu);
    }
    return static_cast<std::uint8_t>(sign | (static_cast<std::uint32_t>(exponent) << 3u) | (mantissa >> 20u));
}

[[nodiscard]] bool supportsInferenceTuple(const nr::rhi::CooperativeVectorCapabilitySnapshot &capabilities,
                                          vk::ComponentTypeKHR interpretation)
{
    return std::ranges::any_of(capabilities.supportedTuples, [interpretation](
                                                                 const vk::CooperativeVectorPropertiesNV &properties) {
        return properties.inputType == vk::ComponentTypeKHR::eFloat16 &&
               properties.inputInterpretation == interpretation &&
               properties.matrixInterpretation == interpretation &&
               properties.biasInterpretation == vk::ComponentTypeKHR::eFloat16 &&
               properties.resultType == vk::ComponentTypeKHR::eFloat16;
    });
}

[[nodiscard]] nr::rhi::Buffer createStorageBuffer(nr::rhi::Device &device, vk::DeviceSize size, std::string_view name)
{
    auto createInfo = vk::BufferCreateInfo{};
    createInfo.size = size;
    createInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
                       vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    return device.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::GpuOnly, name);
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

struct VariantParameters
{
    std::vector<std::byte> rowMajorBytes{};
    std::vector<std::byte> parameterBytes{};
    std::array<vk::DeviceSize, kLayerCount> rowMajorOffsets{};
    BenchPushConstants pushConstants{};
    nr::rhi::CooperativeVectorMatrixDesc rowMajorDesc{};
    nr::rhi::CooperativeVectorMatrixDesc optimalDesc{};
    nr::rhi::CooperativeVectorMatrixLayoutSize rowMajorSize{};
    nr::rhi::CooperativeVectorMatrixLayoutSize optimalSize{};
};

// Row-major random weights with He scaling keep activations bounded across the
// eight layers, which also keeps every value inside the narrow E4M3 range.
[[nodiscard]] VariantParameters makeVariantParameters(const nr::rhi::Device &device, const Variant &variant)
{
    auto const width = variant.width;
    auto const isFloat8 = variant.componentType == nr::rhi::CooperativeVectorComponentType::FloatE4M3;
    auto const componentBytes = vk::DeviceSize{isFloat8 ? 1u : 2u};

    auto parameters = VariantParameters{};
    parameters.rowMajorDesc = nr::rhi::CooperativeVectorMatrixDesc{
        .rows = width,
        .columns = width,
        .layout = nr::rhi::CooperativeVectorMatrixLayout::RowMajor,
        .rowStrideBytes = width * componentBytes,
        .componentType = variant.componentType,
    };
    parameters.optimalDesc = nr::rhi::CooperativeVectorMatrixDesc{
        .rows = width,
        .columns = width,
        .layout = nr::rhi::CooperativeVectorMatrixLayout::InferencingOptimal,
        .componentType = variant.componentType,
    };
    parameters.rowMajorSize =
        nr::rhi::queryCooperativeVectorMatrixLayoutSize(device.device, parameters.rowMajorDesc);
    parameters.optimalSize = nr::rhi::queryCooperativeVectorMatrixLayoutSize(device.device, parameters.optimalDesc);

    auto rowMajorTotal = vk::DeviceSize{0u};
    auto parameterTotal = vk::DeviceSize{0u};
    for (auto layer = std::size_t{0u}; layer < kLayerCount; ++layer)
    {
        parameters.rowMajorOffsets[layer] = rowMajorTotal;
        rowMajorTotal = alignUp(rowMajorTotal + parameters.rowMajorSize.byteSize, kMatrixAlignment);
        parameters.pushConstants.weightOffsets[layer] = static_cast<std::uint32_t>(parameterTotal);
        parameterTotal = alignUp(parameterTotal + parameters.optimalSize.byteSize, kMatrixAlignment);
    }
    for (auto layer = std::size_t{0u}; layer < kLayerCount; ++layer)
    {
        parameters.pushConstants.biasOffsets[layer] = static_cast<std::uint32_t>(parameterTotal);
        parameterTotal = alignUp(parameterTotal + width * sizeof(std::uint16_t), kMatrixAlignment);
    }

    parameters.rowMajorBytes.resize(static_cast<std::size_t>(rowMajorTotal));
    parameters.parameterBytes.resize(static_cast<std::size_t>(parameterTotal));

    auto engine = std::mt19937{0x5eed'1234u};
    auto weightDistribution = std::normal_distribution<float>{0.0f, std::sqrt(2.0f / static_cast<float>(width))};
    auto biasDistribution = std::normal_distribution<float>{0.0f, 0.05f};
    auto const writeFloat16 = [](std::vector<std::byte> &destination, std::size_t offset, float value) {
        auto const encoded = encodeFloat16(value);
        destination[offset] = static_cast<std::byte>(encoded & 0xffu);
        destination[offset + 1u] = static_cast<std::byte>((encoded >> 8u) & 0xffu);
    };
    for (auto layer = std::size_t{0u}; layer < kLayerCount; ++layer)
    {
        auto const weightBase = static_cast<std::size_t>(parameters.rowMajorOffsets[layer]);
        for (auto row = std::uint32_t{0u}; row < width; ++row)
        {
            for (auto column = std::uint32_t{0u}; column < width; ++column)
            {
                auto const weight = weightDistribution(engine);
                auto const offset =
                    weightBase + (static_cast<std::size_t>(row) * width + column) * componentBytes;
                if (isFloat8)
                {
                    parameters.rowMajorBytes[offset] = static_cast<std::byte>(encodeFloatE4M3(weight));
                    continue;
                }
                writeFloat16(parameters.rowMajorBytes, offset, weight);
            }
        }

        auto const biasBase = static_cast<std::size_t>(parameters.pushConstants.biasOffsets[layer]);
        for (auto row = std::uint32_t{0u}; row < width; ++row)
        {
            writeFloat16(parameters.parameterBytes, biasBase + row * sizeof(std::uint16_t),
                         biasDistribution(engine));
        }
    }
    return parameters;
}

void recordConversionBarrier(const vk::raii::CommandBuffer &commandBuffer)
{
    auto barrier = vk::MemoryBarrier2{};
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV;
    barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
    auto dependencyInfo = vk::DependencyInfo{};
    dependencyInfo.memoryBarrierCount = 1u;
    dependencyInfo.pMemoryBarriers = std::addressof(barrier);
    commandBuffer.pipelineBarrier2(dependencyInfo);
}

[[nodiscard]] double timestampMilliseconds(const nr::rhi::Device &device, std::uint64_t begin, std::uint64_t end)
{
    auto const queueFamilyIndex = device.queueManager.compute().queueFamilyIndex();
    auto const validBits =
        device.physicalDevice.getQueueFamilyProperties()[queueFamilyIndex].timestampValidBits;
    auto const mask = validBits >= 64u ? ~std::uint64_t{0u} : ((std::uint64_t{1u} << validBits) - 1u);
    auto const ticks = (end & mask) - (begin & mask);
    auto const period = static_cast<double>(device.physicalDevice.getProperties().limits.timestampPeriod);
    return static_cast<double>(ticks) * period / 1'000'000.0;
}

[[nodiscard]] VariantResult runVariant(nr::rhi::Device &device, const Variant &variant)
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{variant.shaderPath},
    });
    if (!program.valid())
    {
        std::println(std::cerr, "Failed to compile {}", variant.shaderPath);
        return {};
    }
    auto pipelineBuild = device.pipeline().createComputePipeline(program, {}, 64u, {}, "coopvec_bench");
    auto pipeline = pipelineBuild.get();

    auto const parameters = makeVariantParameters(device, variant);
    auto const rowMajorBuffer =
        createStorageBuffer(device, parameters.rowMajorBytes.size(), "coopvec_bench_row_major");
    auto const parameterBuffer =
        createStorageBuffer(device, parameters.parameterBytes.size(), "coopvec_bench_parameters");
    auto const resultBuffer = createStorageBuffer(device, kElementCount * sizeof(float), "coopvec_bench_results");

    auto const uploadPlan = makeComputeUploadPlan(device);
    auto uploads = std::array{
        device.uploadReadback().uploadBuffer(std::span{parameters.rowMajorBytes}, rowMajorBuffer, 0u, uploadPlan),
        device.uploadReadback().uploadBuffer(std::span{parameters.parameterBytes}, parameterBuffer, 0u, uploadPlan),
    };

    nr::rhi::submitOneShot(device.device, device.queueManager.compute(),
                           nr::rhi::OneShotSyncPlan{
                               .waitSemaphore = *device.uploadReadback().uploadTimelineSemaphore(),
                               .waitStage = vk::PipelineStageFlagBits2::eAllCommands,
                               .waitValue = uploads.back().signalValue,
                           },
                           [&](const vk::raii::CommandBuffer &commandBuffer) {
                               for (const auto &ticket : uploads)
                               {
                                   device.uploadReadback().recordAcquireBarrier(commandBuffer, ticket);
                               }
                               for (auto layer = std::size_t{0u}; layer < kLayerCount; ++layer)
                               {
                                   nr::rhi::recordCooperativeVectorMatrixConversion(
                                       commandBuffer,
                                       nr::rhi::CooperativeVectorMatrixMemory{
                                           .deviceAddress =
                                               rowMajorBuffer.deviceAddress() + parameters.rowMajorOffsets[layer],
                                           .size = parameters.rowMajorSize.byteSize,
                                       },
                                       parameters.rowMajorDesc, parameters.rowMajorSize,
                                       nr::rhi::CooperativeVectorMatrixMemory{
                                           .deviceAddress = parameterBuffer.deviceAddress() +
                                                            parameters.pushConstants.weightOffsets[layer],
                                           .size = parameters.optimalSize.byteSize,
                                       },
                                       parameters.optimalDesc, parameters.optimalSize);
                               }
                               recordConversionBarrier(commandBuffer);
                           });

    auto root = pipeline.descriptorLayout.rootCursor();
    root.beginRecording();
    static_cast<void>(root["gParameters"].setObject(parameterBuffer));
    static_cast<void>(root["gResults"].setObject(resultBuffer));
    auto const descriptorSnapshot = root.takeSnapshot();
    auto sets = nr::rhi::allocateBindingSetsForLayout(pipeline.layout, pipeline.bindingPool);
    auto writeCache = nr::rhi::DescriptorWriteCache{};
    nr::rhi::updateResourcesForBindingSnapshot(pipeline.bindingPool, sets, writeCache, descriptorSnapshot, {});
    root.beginRecording();
    static_cast<void>(root["gBench"].setData(parameters.pushConstants));
    auto const pushConstantSnapshot = root.takeSnapshot();

    auto queryPoolCreateInfo = vk::QueryPoolCreateInfo{};
    queryPoolCreateInfo.queryType = vk::QueryType::eTimestamp;
    queryPoolCreateInfo.queryCount = 2u;
    auto queryPool = vk::raii::QueryPool{device.device, queryPoolCreateInfo};

    auto commandPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.compute().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
    };
    auto commandBuffers = commandPool.allocate<vk::CommandBufferLevel::ePrimary>(1u);
    auto const &commandBuffer = commandBuffers.front();

    auto const groupCount = (kElementCount + kThreadsPerGroup - 1u) / kThreadsPerGroup;
    auto const recordAndSubmit = [&] {
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        commandBuffer.resetQueryPool(*queryPool, 0u, 2u);
        commandBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, *queryPool, 0u);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.pipeline.raw());
        nr::rhi::bindPreparedResourcesToCommandBuffer(commandBuffer, vk::PipelineBindPoint::eCompute, pipeline.layout,
                                                      sets);
        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, pipeline.layout, pushConstantSnapshot);
        commandBuffer.dispatch(groupCount, 1u, 1u);
        commandBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, *queryPool, 1u);
        commandBuffer.end();
        device.queueManager.compute().submit(commandBuffer);
        device.queueManager.compute().waitIdle();
    };

    for (auto warmup = std::uint32_t{0u}; warmup < kWarmupRuns; ++warmup)
    {
        recordAndSubmit();
    }

    auto samples = std::vector<double>{};
    samples.reserve(kMeasuredRuns);
    for (auto run = std::uint32_t{0u}; run < kMeasuredRuns; ++run)
    {
        recordAndSubmit();
        auto const [result, timestamps] =
            queryPool.getResults<std::uint64_t>(0u, 2u, 2u * sizeof(std::uint64_t), sizeof(std::uint64_t),
                                                vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result != vk::Result::eSuccess)
        {
            std::println(std::cerr, "Timestamp query failed for {}", variant.name);
            return {};
        }
        samples.push_back(timestampMilliseconds(device, timestamps[0], timestamps[1]));
    }
    std::ranges::sort(samples);

    auto readback = device.uploadReadback().readbackBuffer(
        resultBuffer, 0u, resultBuffer.size(), nr::rhi::QueueRole::Compute,
        nr::rhi::ops::ReadbackSyncPlan{
            .preCopy = {vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite},
            .postCopy = {vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite},
        });
    auto const resultBytes = device.uploadReadback().readbackBytes(readback);
    auto sampleValue = 0.0f;
    std::memcpy(std::addressof(sampleValue), resultBytes.data(), sizeof(float));

    auto const multiplyAccumulates = static_cast<double>(kElementCount) * kIterationCount * kLayerCount *
                                     static_cast<double>(variant.width) * variant.width;
    auto const bestMilliseconds = samples.front();
    return VariantResult{
        .name = variant.name,
        .bestMilliseconds = bestMilliseconds,
        .medianMilliseconds = samples[samples.size() / 2u],
        .teraFlopsPerSecond = 2.0 * multiplyAccumulates / (bestMilliseconds * 1.0e-3) / 1.0e12,
        .sampleValue = sampleValue,
    };
}
} // namespace

int main()
{
    auto device = nr::rhi::Device::create("coopVecBench", "NewbieRenderer");
    auto const &capabilities = device.cooperativeVectorCapabilities();
    std::println("cooperative-vector tuples reported by the device: {}", capabilities.supportedTuples.size());
    auto const float16Supported = supportsInferenceTuple(capabilities, vk::ComponentTypeKHR::eFloat16);
    auto const float8Supported = supportsInferenceTuple(capabilities, vk::ComponentTypeKHR::eFloatE4M3);
    std::println("fp16 inference tuple: {}, e4m3 inference tuple: {}", float16Supported, float8Supported);
    if (!float16Supported || !float8Supported)
    {
        std::println(std::cerr, "Both the FP16 and the E4M3 inference tuples are required for this benchmark.");
        return 1;
    }

    nr::rhi::ShaderService::instance().configure();

    auto results = std::vector<VariantResult>{};
    results.reserve(kVariants.size());
    for (const auto &variant : kVariants)
    {
        results.push_back(runVariant(device, variant));
    }
    device.waitIdle();

    std::println("");
    std::println("{} inferences per dispatch, {} chained forward passes each, 8 layers per pass", kElementCount,
                 kIterationCount);
    std::println("{:<14}{:>12}{:>12}{:>14}{:>14}", "variant", "best ms", "median ms", "TFLOP/s", "sample");
    for (const auto &result : results)
    {
        std::println("{:<14}{:>12.4f}{:>12.4f}{:>14.2f}{:>14.4f}", result.name, result.bestMilliseconds,
                     result.medianMilliseconds, result.teraFlopsPerSecond, result.sampleValue);
    }
    std::println("");
    std::println("e4m3 speedup over fp16: width32 {:.3f}x, width64 {:.3f}x",
                 results[0].bestMilliseconds / results[1].bestMilliseconds,
                 results[2].bestMilliseconds / results[3].bestMilliseconds);
    return 0;}
