import std;
import nr.rhi;

namespace
{
struct CameraData
{
    std::array<float, 4> origin{};
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> forward{};
};

struct BlasGeometryUpload
{
    std::array<std::array<float, 3>, 3> positions{};
    std::array<std::uint32_t, 3> indices{};
};

[[nodiscard]] std::uint64_t alignUpAddress(std::uint64_t value, std::uint64_t alignment)
{
    if (alignment <= 1)
    {
        return value;
    }

    auto remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    return value + (alignment - remainder);
}

[[nodiscard]] bool runMinimalRtPipelineTest()
{
    nr::rhi::Device device;
    device.initialize("nr_rt_minimal_pipeline_test", "nrrhi_test");

    auto &shaderService = device.shaderCompiler();
    shaderService.configure();

    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("test/rt/minimalRtTriangle"),
    });
    if (!program.valid())
    {
        std::println("[error] failed to compile shader module test/rt/minimalRtTriangle.");
        return false;
    }

    nr::rhi::RayTracingPipelineDesc rtPipelineDesc{};
    rtPipelineDesc.entryPointNames = {"rgMain", "msMain", "chMain"};
    rtPipelineDesc.maxRayRecursionDepth = 1;

    auto rtState = device.pipeline().createRayTracingPipeline(program, rtPipelineDesc);
    if (!rtState.pipeline.valid())
    {
        std::println("[error] failed to create ray tracing pipeline.");
        return false;
    }

    auto swapchainExtent = device.presentationContext.swapchainExtent();
    auto swapchainFormat = device.presentationContext.swapchainFormat();

    auto chooseRtOutputFormat = [&]() -> std::optional<vk::Format> {
        auto candidates = std::array{
            swapchainFormat,
            vk::Format::eB8G8R8A8Unorm,
            vk::Format::eR8G8B8A8Unorm,
            vk::Format::eR16G16B16A16Sfloat,
        };

        constexpr auto requiredOutputFeatures = vk::FormatFeatureFlagBits::eStorageImage | vk::FormatFeatureFlagBits::eTransferSrc;

        auto it = std::ranges::find_if(candidates, [&](vk::Format candidate) {
            auto props = device.physicalDevice.getFormatProperties(candidate);
            return (props.optimalTilingFeatures & requiredOutputFeatures) == requiredOutputFeatures;
        });

        if (it == candidates.end())
        {
            return std::nullopt;
        }
        return *it;
    };

    auto outputFormat = chooseRtOutputFormat();
    if (!outputFormat.has_value())
    {
        std::println("[error] no suitable RT output format with storage + transfer-src support.");
        return false;
    }

    auto swapchainFormatProperties = device.physicalDevice.getFormatProperties(swapchainFormat);
    auto requiredSwapchainFeatures = vk::FormatFeatureFlagBits::eTransferDst;
    if ((swapchainFormatProperties.optimalTilingFeatures & requiredSwapchainFeatures) != requiredSwapchainFeatures)
    {
        std::println(
            "[error] swapchain format {} does not support transfer-dst feature needed by the minimal RT test.",
            vk::to_string(swapchainFormat));
        return false;
    }

    vk::ImageCreateInfo outputImageInfo{};
    outputImageInfo.imageType = vk::ImageType::e2D;
    outputImageInfo.format = *outputFormat;
    outputImageInfo.extent = vk::Extent3D{swapchainExtent.width, swapchainExtent.height, 1};
    outputImageInfo.mipLevels = 1;
    outputImageInfo.arrayLayers = 1;
    outputImageInfo.samples = vk::SampleCountFlagBits::e1;
    outputImageInfo.tiling = vk::ImageTiling::eOptimal;
    outputImageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    outputImageInfo.sharingMode = vk::SharingMode::eExclusive;
    outputImageInfo.initialLayout = vk::ImageLayout::eUndefined;
    auto outputImage = device.resourceFactory.createImage(outputImageInfo, nr::rhi::MemoryUsage::GpuOnly, "rt_test_output");

    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    auto const computeQueueFamily = device.queueManager.compute().queueFamilyIndex();

    if (transferQueueFamily == graphicsQueueFamily)
    {
        std::println("[error] transfer and graphics queue families must be distinct for this test.");
        return false;
    }

    BlasGeometryUpload geometryUpload{};
    geometryUpload.positions = {
        std::array<float, 3>{-0.8f, -0.6f, 0.0f},
        std::array<float, 3>{0.8f, -0.6f, 0.0f},
        std::array<float, 3>{0.0f, 0.8f, 0.0f},
    };
    geometryUpload.indices = {0u, 1u, 2u};

    CameraData cameraData{};
    cameraData.origin = {0.0f, 0.0f, -2.5f, 1.0f};
    cameraData.right = {1.25f, 0.0f, 0.0f, 0.0f};
    cameraData.up = {0.0f, 1.25f, 0.0f, 0.0f};
    cameraData.forward = {0.0f, 0.0f, 1.0f, 0.0f};

    vk::BufferCreateInfo geometryBufferInfo{};
    geometryBufferInfo.size = sizeof(BlasGeometryUpload);
    geometryBufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
    geometryBufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto geometryBuffer = device.resourceFactory.createBuffer(geometryBufferInfo, nr::rhi::MemoryUsage::GpuOnly, "rt_geometry_gpu");

    vk::BufferCreateInfo cameraBufferInfo{};
    cameraBufferInfo.size = sizeof(CameraData);
    cameraBufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    cameraBufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto cameraBuffer = device.resourceFactory.createBuffer(cameraBufferInfo, nr::rhi::MemoryUsage::CpuToGpu, "rt_camera_upload");
    cameraBuffer.write(std::as_bytes(std::span{&cameraData, 1}));
    cameraBuffer.flush();

    nr::rhi::AsBuildOptions buildOptions{};
    buildOptions.buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

    nr::rhi::BlasGeometryLayout blasLayout{};
    blasLayout.vertexFormat = vk::Format::eR32G32B32Sfloat;
    blasLayout.vertexStride = sizeof(geometryUpload.positions[0]);
    blasLayout.indexType = vk::IndexType::eUint32;
    blasLayout.maxVertex = static_cast<std::uint32_t>(geometryUpload.positions.size());
    blasLayout.geometryFlags = vk::GeometryFlagBitsKHR::eOpaque;

    auto blasSizes = nr::rhi::queryBlasBuildSizes(device.device, blasLayout, 1u, buildOptions);
    auto tlasSizes = nr::rhi::queryTlasBuildSizes(
        device.device,
        nr::rhi::TlasBuildInput{.instanceCount = 1u, .arrayOfPointers = false},
        buildOptions);

    vk::BufferCreateInfo blasStorageInfo{};
    blasStorageInfo.size = blasSizes.accelerationStructureSize;
    blasStorageInfo.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
    blasStorageInfo.sharingMode = vk::SharingMode::eExclusive;
    auto blasStorage = device.resourceFactory.createBuffer(blasStorageInfo, nr::rhi::MemoryUsage::GpuOnly, "rt_blas_storage");
    auto blas = nr::rhi::AccelerationStructureResource::create(
        device.device,
        blasStorage,
        0,
        blasSizes.accelerationStructureSize,
        vk::AccelerationStructureTypeKHR::eBottomLevel,
        "rt_blas");

    vk::AccelerationStructureInstanceKHR tlasInstance{};
    tlasInstance.transform.matrix[0][0] = 1.0f;
    tlasInstance.transform.matrix[1][1] = 1.0f;
    tlasInstance.transform.matrix[2][2] = 1.0f;
    tlasInstance.instanceCustomIndex = 0;
    tlasInstance.mask = 0xFF;
    tlasInstance.instanceShaderBindingTableRecordOffset = 0;
    tlasInstance.flags = static_cast<std::uint32_t>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
    tlasInstance.accelerationStructureReference = blas.deviceAddress();

    vk::BufferCreateInfo instanceBufferInfo{};
    instanceBufferInfo.size = sizeof(vk::AccelerationStructureInstanceKHR);
    instanceBufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
    instanceBufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto instanceBuffer = device.resourceFactory.createBuffer(instanceBufferInfo, nr::rhi::MemoryUsage::GpuOnly, "rt_instance_gpu");

    vk::BufferCreateInfo tlasStorageInfo{};
    tlasStorageInfo.size = tlasSizes.accelerationStructureSize;
    tlasStorageInfo.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
    tlasStorageInfo.sharingMode = vk::SharingMode::eExclusive;
    auto tlasStorage = device.resourceFactory.createBuffer(tlasStorageInfo, nr::rhi::MemoryUsage::GpuOnly, "rt_tlas_storage");
    auto tlas = nr::rhi::AccelerationStructureResource::create(
        device.device,
        tlasStorage,
        0,
        tlasSizes.accelerationStructureSize,
        vk::AccelerationStructureTypeKHR::eTopLevel,
        "rt_tlas");

    auto asLimits = nr::rhi::queryAsBuildLimits(device.physicalDevice);
    auto scratchAlignment = std::max<vk::DeviceSize>(asLimits.minScratchAlignment, 1u);
    auto requiredScratchSize = std::max(blasSizes.buildScratchSize, tlasSizes.buildScratchSize);

    vk::BufferCreateInfo scratchInfo{};
    scratchInfo.size = requiredScratchSize + scratchAlignment;
    scratchInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    scratchInfo.sharingMode = vk::SharingMode::eExclusive;
    auto scratchBuffer = device.resourceFactory.createBuffer(scratchInfo, nr::rhi::MemoryUsage::GpuOnly, "rt_build_scratch");

    auto scratchBaseAddress = static_cast<std::uint64_t>(scratchBuffer.deviceAddress());
    auto scratchAddress = alignUpAddress(scratchBaseAddress, static_cast<std::uint64_t>(scratchAlignment));
    auto scratchOffset = static_cast<vk::DeviceSize>(scratchAddress - scratchBaseAddress);
    if (scratchOffset + requiredScratchSize > scratchBuffer.size())
    {
        std::println("[error] scratch alignment overflow: offset={}, required={}, size={}", scratchOffset, requiredScratchSize, scratchBuffer.size());
        return false;
    }

    auto &uploadContext = device.uploadReadback();
    auto geometryUploadTicket = uploadContext.uploadBuffer(
        std::as_bytes(std::span{&geometryUpload, 1}),
        geometryBuffer,
        0,
        nr::rhi::ops::BufferUploadOwnershipPlan{
            .releaseToDestination = nr::rhi::ops::QueueOwnershipTransfer{
                .release = nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = transferQueueFamily,
                    .dstQueueFamilyIndex = graphicsQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferWrite,
                },
                .acquire = nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = transferQueueFamily,
                    .dstQueueFamilyIndex = graphicsQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
                },
            },
        });
    auto instanceUploadTicket = uploadContext.uploadBuffer(
        std::as_bytes(std::span{&tlasInstance, 1}),
        instanceBuffer,
        0,
        nr::rhi::ops::BufferUploadOwnershipPlan{
            .releaseToDestination = nr::rhi::ops::QueueOwnershipTransfer{
                .release = nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = transferQueueFamily,
                    .dstQueueFamilyIndex = graphicsQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferWrite,
                },
                .acquire = nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = transferQueueFamily,
                    .dstQueueFamilyIndex = graphicsQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
                },
            },
        });
    uploadContext.waitUploadComplete();

    nr::rhi::CommandPool setupGraphicsPool(device.device, graphicsQueueFamily, vk::CommandPoolCreateFlagBits::eTransient);
    auto setupGraphicsBuffers = setupGraphicsPool.allocatePrimary(1);
    auto &setupGraphics = setupGraphicsBuffers.front();

    auto constexpr vertexDataOffset = vk::DeviceSize{0};
    auto const indexDataOffset = static_cast<vk::DeviceSize>(sizeof(geometryUpload.positions));

    nr::rhi::CommandRecorder::beginPrimary(setupGraphics, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *setupGraphics;

        nr::rhi::ops::BarrierBatch transferAcquireBarriers{};
        transferAcquireBarriers.add(uploadContext.makeAcquireBarrier(geometryUploadTicket));
        transferAcquireBarriers.add(uploadContext.makeAcquireBarrier(instanceUploadTicket));
        nr::rhi::ops::pipelineBarrier(raw, transferAcquireBarriers);

        nr::rhi::BlasBuildRecordInfo blasBuildRecord{};
        blasBuildRecord.dst = &blas;
        blasBuildRecord.geometryBuffer = &geometryBuffer;
        blasBuildRecord.scratchBuffer = &scratchBuffer;
        blasBuildRecord.scratchAddress = scratchAddress;
        blasBuildRecord.geometryLayout = blasLayout;
        blasBuildRecord.geometryInput = nr::rhi::BlasGeometryInput{
            .vertexAddress = geometryBuffer.deviceAddress() + vertexDataOffset,
            .indexAddress = geometryBuffer.deviceAddress() + indexDataOffset,
            .transformAddress = 0,
            .primitiveCount = 1,
            .firstVertex = 0,
            .primitiveOffset = 0,
        };
        blasBuildRecord.options = buildOptions;
        nr::rhi::recordBuildBlas(setupGraphics, blasBuildRecord, scratchAlignment);

        nr::rhi::ops::BarrierBatch asBuildDependency{};
        asBuildDependency.add(vk::MemoryBarrier2{
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            nullptr,
        });
        nr::rhi::ops::pipelineBarrier(raw, asBuildDependency);

        nr::rhi::TlasBuildRecordInfo tlasBuildRecord{};
        tlasBuildRecord.dst = &tlas;
        tlasBuildRecord.instanceBuffer = &instanceBuffer;
        tlasBuildRecord.scratchBuffer = &scratchBuffer;
        tlasBuildRecord.scratchAddress = scratchAddress;
        tlasBuildRecord.buildInput = nr::rhi::TlasBuildInput{
            .instancesAddress = instanceBuffer.deviceAddress(),
            .instanceCount = 1,
            .arrayOfPointers = false,
        };
        tlasBuildRecord.options = buildOptions;
        nr::rhi::recordBuildTlas(setupGraphics, tlasBuildRecord, scratchAlignment);

        nr::rhi::ops::BarrierBatch asToTraceBarrier{};
        asToTraceBarrier.add(vk::MemoryBarrier2{
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            nullptr,
        });
        nr::rhi::ops::pipelineBarrier(raw, asToTraceBarrier);
    }
    nr::rhi::CommandRecorder::end(setupGraphics);

    nr::rhi::CommandBatch setupGraphicsBatch{};
    setupGraphicsBatch.addCommandBuffer(setupGraphics);
    setupGraphicsBatch.addWait(
        uploadContext.uploadTimelineSemaphore(),
        geometryUploadTicket.ownership->acquire.stages,
        std::max(geometryUploadTicket.signalValue, instanceUploadTicket.signalValue));
    device.queueManager.graphics().submit(setupGraphicsBatch);
    device.queueManager.graphics().waitIdle();

    auto descriptorSets = device.pipeline().allocateBindingSets(rtState.layout, rtState.bindingPool);
    if (descriptorSets.empty())
    {
        std::println("[error] no descriptor sets allocated for RT pipeline.");
        return false;
    }

    auto root = rtState.descriptorLayout.rootCursor();
    if (!root.valid())
    {
        std::println("[error] descriptor root cursor is invalid.");
        return false;
    }

    if (!root["scene"].setObject(tlas.raw()))
    {
        std::println("[error] failed to bind TLAS descriptor.");
        return false;
    }

    if (!root["outputImage"].setObject(outputImage, vk::ImageLayout::eGeneral))
    {
        std::println("[error] failed to bind storage image descriptor.");
        return false;
    }

    if (!root["camera"].setObject(cameraBuffer, 0, sizeof(CameraData)))
    {
        std::println("[error] failed to bind camera uniform buffer descriptor.");
        return false;
    }

    auto bindingSnapshot = root.snapshot();
    if (bindingSnapshot.descriptorWriteCount() == 0u)
    {
        std::println("[error] descriptor snapshot is empty after RT binding capture.");
        return false;
    }

    auto capabilities = device.rayTracingCapabilities();
    nr::rhi::ShaderBindingTableBuildDesc sbtDesc{};
    sbtDesc.pipeline = &rtState.pipeline;
    sbtDesc.capabilities = capabilities;
    sbtDesc.raygen = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 0, .groupCount = 1, .stride = 0};
    sbtDesc.miss = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 1, .groupCount = 1, .stride = 0};
    sbtDesc.hit = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 2, .groupCount = 1, .stride = 0};
    sbtDesc.debugName = "rt_minimal_sbt";

    auto sbt = nr::rhi::ShaderBindingTable::create(device.resourceFactory, sbtDesc);
    if (!sbt.valid())
    {
        std::println("[error] failed to create shader binding table.");
        return false;
    }

    auto frameGraphicsCommandOwnership = std::vector<std::optional<vk::raii::CommandBuffers>>(device.frameManager.frameCount());
    auto frameComputeCommandOwnership = std::vector<std::optional<vk::raii::CommandBuffers>>(device.frameManager.frameCount());
    auto graphicsToComputeSemaphores = std::vector<vk::raii::Semaphore>{};
    graphicsToComputeSemaphores.reserve(device.frameManager.frameCount());
    std::ranges::for_each(std::views::iota(size_t{0}, device.frameManager.frameCount()), [&](size_t) {
        graphicsToComputeSemaphores.push_back(nr::rhi::sync::createSemaphore(device.device));
    });

    bool outputReadyForTrace = false;
    std::uint32_t presentedFrames = 0;
    constexpr std::uint32_t kMaxFrames = 90;
    auto swapchainImageReadyForTransfer = std::vector<bool>(device.presentationContext.swapchainImageCount(), false);

    for (std::uint32_t frameIndex = 0; frameIndex < kMaxFrames; ++frameIndex)
    {
        device.presentationContext.pollEvents();
        if (device.presentationContext.windowShouldClose())
        {
            break;
        }

        auto frameBegin = device.beginFrame();
        auto swapchainImage = device.presentationContext.swapchainImage(frameBegin.swapchainImageIndex);
        auto const swapchainWasPresented = swapchainImageReadyForTransfer[frameBegin.swapchainImageIndex];

        auto &frame = device.frameManager.current();
        auto &graphicsCommandBuffersOwner = frameGraphicsCommandOwnership[frameBegin.frameIndex];
        graphicsCommandBuffersOwner.reset();
        graphicsCommandBuffersOwner.emplace(frame.primary<nr::rhi::QueueRole::Graphics>().allocatePrimary(1));
        auto &graphicsCommandBuffer = graphicsCommandBuffersOwner->front();

        auto &computeCommandBuffersOwner = frameComputeCommandOwnership[frameBegin.frameIndex];
        computeCommandBuffersOwner.reset();
        computeCommandBuffersOwner.emplace(frame.primary<nr::rhi::QueueRole::Compute>().allocatePrimary(1));
        auto &computeCommandBuffer = computeCommandBuffersOwner->front();

        nr::rhi::CommandRecorder::beginPrimary(graphicsCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *graphicsCommandBuffer;

            nr::rhi::ops::BarrierBatch traceInputBarriers{};

            if (outputReadyForTrace)
            {
                traceInputBarriers.add(nr::rhi::ops::makeImageOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Acquire>(
                    outputImage,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eGeneral,
                    nr::rhi::ops::QueueOwnershipRequest{
                        .srcQueueFamilyIndex = computeQueueFamily,
                        .dstQueueFamilyIndex = graphicsQueueFamily,
                        .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                        .access = vk::AccessFlagBits2::eShaderWrite,
                    }));
            }
            else
            {
                vk::ImageMemoryBarrier2 outputToGeneral{};
                outputToGeneral.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
                outputToGeneral.srcAccessMask = vk::AccessFlags2{};
                outputToGeneral.dstStageMask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
                outputToGeneral.dstAccessMask = vk::AccessFlagBits2::eShaderWrite;
                outputToGeneral.oldLayout = vk::ImageLayout::eUndefined;
                outputToGeneral.newLayout = vk::ImageLayout::eGeneral;
                outputToGeneral.srcQueueFamilyIndex = nr::rhi::ops::kIgnoredQueueFamilyIndex;
                outputToGeneral.dstQueueFamilyIndex = nr::rhi::ops::kIgnoredQueueFamilyIndex;
                traceInputBarriers.add(nr::rhi::ops::makeImageBarrier(outputImage, outputToGeneral));
            }

            nr::rhi::ops::pipelineBarrier(raw, traceInputBarriers);

            nr::rhi::bindResourcesToCommandBuffer(
                raw,
                vk::PipelineBindPoint::eRayTracingKHR,
                rtState.layout,
                rtState.bindingPool,
                descriptorSets,
                bindingSnapshot);

            nr::rhi::TraceRaysDesc traceDesc{};
            traceDesc.pipeline = &rtState.pipeline;
            traceDesc.shaderBindingTable = &sbt;
            traceDesc.dimensions = nr::rhi::TraceRaysDimensions{
                .width = swapchainExtent.width,
                .height = swapchainExtent.height,
                .depth = 1,
            };
            traceDesc.recordingQueueRole = nr::rhi::QueueRole::Graphics;
            nr::rhi::traceRays(graphicsCommandBuffer, traceDesc, capabilities);

            nr::rhi::ops::BarrierBatch graphicsToComputeRelease{};
            graphicsToComputeRelease.add(nr::rhi::ops::makeImageOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Release>(
                outputImage,
                vk::ImageLayout::eGeneral,
                vk::ImageLayout::eTransferSrcOptimal,
                nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = graphicsQueueFamily,
                    .dstQueueFamilyIndex = computeQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                    .access = vk::AccessFlagBits2::eShaderWrite,
                }));
            nr::rhi::ops::pipelineBarrier(raw, graphicsToComputeRelease);
        }
        nr::rhi::CommandRecorder::end(graphicsCommandBuffer);

        auto &graphicsToComputeSemaphore = graphicsToComputeSemaphores[frameBegin.frameIndex];

        nr::rhi::CommandBatch graphicsBatch{};
        graphicsBatch.addCommandBuffer(graphicsCommandBuffer);
        graphicsBatch.addSignal(graphicsToComputeSemaphore);
        device.queueManager.graphics().submit(graphicsBatch);

        nr::rhi::CommandRecorder::beginPrimary(computeCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto raw = *computeCommandBuffer;

            nr::rhi::ops::BarrierBatch computeInputBarriers{};
            computeInputBarriers.add(nr::rhi::ops::makeImageOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Acquire>(
                outputImage,
                vk::ImageLayout::eGeneral,
                vk::ImageLayout::eTransferSrcOptimal,
                nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = graphicsQueueFamily,
                    .dstQueueFamilyIndex = computeQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                }));

            vk::ImageMemoryBarrier2 swapchainToTransfer{};
            swapchainToTransfer.srcStageMask = swapchainWasPresented ? vk::PipelineStageFlagBits2::eBottomOfPipe : vk::PipelineStageFlagBits2::eTopOfPipe;
            swapchainToTransfer.srcAccessMask = vk::AccessFlags2{};
            swapchainToTransfer.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            swapchainToTransfer.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            swapchainToTransfer.oldLayout = swapchainWasPresented ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined;
            swapchainToTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
            swapchainToTransfer.srcQueueFamilyIndex = nr::rhi::ops::kIgnoredQueueFamilyIndex;
            swapchainToTransfer.dstQueueFamilyIndex = nr::rhi::ops::kIgnoredQueueFamilyIndex;
            swapchainToTransfer.image = swapchainImage;
            swapchainToTransfer.subresourceRange = vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor,
                0,
                1,
                0,
                1,
            };
            computeInputBarriers.add(swapchainToTransfer);

            nr::rhi::ops::pipelineBarrier(raw, computeInputBarriers);

            vk::ImageCopy copyRegion{};
            copyRegion.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            copyRegion.srcOffset = vk::Offset3D{0, 0, 0};
            copyRegion.dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            copyRegion.dstOffset = vk::Offset3D{0, 0, 0};
            copyRegion.extent = vk::Extent3D{swapchainExtent.width, swapchainExtent.height, 1};
            raw.copyImage(
                outputImage.handle(),
                vk::ImageLayout::eTransferSrcOptimal,
                swapchainImage,
                vk::ImageLayout::eTransferDstOptimal,
                {copyRegion});

            nr::rhi::ops::BarrierBatch computeToGraphicsRelease{};
            computeToGraphicsRelease.add(nr::rhi::ops::makeImageOwnershipBarrier<nr::rhi::ops::OwnershipBarrierPhase::Release>(
                outputImage,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eGeneral,
                nr::rhi::ops::QueueOwnershipRequest{
                    .srcQueueFamilyIndex = computeQueueFamily,
                    .dstQueueFamilyIndex = graphicsQueueFamily,
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                }));
            nr::rhi::ops::pipelineBarrier(raw, computeToGraphicsRelease);

            nr::rhi::ops::BarrierBatch presentBarriers{};
            presentBarriers.add(vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                vk::AccessFlags2{},
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                nr::rhi::ops::kIgnoredQueueFamilyIndex,
                nr::rhi::ops::kIgnoredQueueFamilyIndex,
                swapchainImage,
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                nullptr,
            });
            nr::rhi::ops::pipelineBarrier(raw, presentBarriers);
        }
        nr::rhi::CommandRecorder::end(computeCommandBuffer);

        nr::rhi::CommandBatch frameBatch{};
        frameBatch.addCommandBuffer(computeCommandBuffer);
        frameBatch.addWait(graphicsToComputeSemaphore, vk::PipelineStageFlagBits2::eTransfer);
        auto presentResult = device.endFrame(frameBatch, nr::rhi::QueueRole::Compute);

        if (presentResult.result != vk::Result::eSuccess && presentResult.result != vk::Result::eSuboptimalKHR)
        {
            std::println("[error] present failed on frame {} with result {}", frameIndex, vk::to_string(presentResult.result));
            return false;
        }

        device.queueManager.compute().waitIdle();

        outputReadyForTrace = true;
        swapchainImageReadyForTransfer[frameBegin.swapchainImageIndex] = true;
        ++presentedFrames;

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    device.waitIdle();
    std::ranges::for_each(frameGraphicsCommandOwnership, [](std::optional<vk::raii::CommandBuffers> &buffers) { buffers.reset(); });
    std::ranges::for_each(frameComputeCommandOwnership, [](std::optional<vk::raii::CommandBuffers> &buffers) { buffers.reset(); });

    if (presentedFrames == 0)
    {
        std::println("[error] no frame was presented.");
        return false;
    }

    std::println("[ok] presented {} frames with minimal RT pipeline.", presentedFrames);
    return true;
}
} // namespace

int main()
{
    try
    {
        if (!runMinimalRtPipelineTest())
        {
            std::println("[FAIL] minimal RT pipeline test failed");
            return 1;
        }

        std::println("[OK] minimal RT pipeline test passed");
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::println("[error] exception: {}", exception.what());
        return 2;
    }
}
