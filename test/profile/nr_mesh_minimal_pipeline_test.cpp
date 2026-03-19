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

struct MeshInputVertex
{
    std::array<float, 4> position{};
    std::array<float, 4> color{};
};

[[nodiscard]] bool runMinimalMeshPipelineTest()
{
    nr::rhi::Device device;
    device.initialize("nr_mesh_minimal_pipeline_test", "nrrhi_test");

    auto &shaderService = device.shaderCompiler();
    shaderService.configure();

    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("test/mesh/minimalMeshTriangle"),
    });
    if (!program.valid())
    {
        std::println("[error] failed to compile shader module test/mesh/minimalMeshTriangle.");
        return false;
    }

    auto const *taskEntry = program.entryPointData("taskMain");
    auto const *meshEntry = program.entryPointData("meshMain");
    auto const *fragmentEntry = program.entryPointData("fragmentMain");
    if (taskEntry == nullptr || meshEntry == nullptr || fragmentEntry == nullptr)
    {
        std::println("[error] required mesh pipeline entrypoints are missing.");
        return false;
    }
    if (taskEntry->stage != SLANG_STAGE_AMPLIFICATION ||
        meshEntry->stage != SLANG_STAGE_MESH ||
        fragmentEntry->stage != SLANG_STAGE_FRAGMENT)
    {
        std::println(
            "[error] mesh shader stage mismatch: task={}, mesh={}, fragment={}",
            static_cast<int32_t>(taskEntry->stage),
            static_cast<int32_t>(meshEntry->stage),
            static_cast<int32_t>(fragmentEntry->stage));
        return false;
    }

    auto swapchainExtent = device.presentationContext.swapchainExtent();
    auto swapchainFormat = device.presentationContext.swapchainFormat();

    auto chooseOutputFormat = [&]() -> std::optional<vk::Format> {
        auto candidates = std::array{
            swapchainFormat,
            vk::Format::eB8G8R8A8Unorm,
            vk::Format::eR8G8B8A8Unorm,
            vk::Format::eR16G16B16A16Sfloat,
        };
        constexpr auto requiredOutputFeatures =
            vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eTransferSrc;

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

    auto outputFormat = chooseOutputFormat();
    if (!outputFormat.has_value())
    {
        std::println("[error] no suitable mesh output format with color-attachment + transfer-src support.");
        return false;
    }

    auto swapchainFormatProperties = device.physicalDevice.getFormatProperties(swapchainFormat);
    if ((swapchainFormatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferDst) != vk::FormatFeatureFlagBits::eTransferDst)
    {
        std::println(
            "[error] swapchain format {} does not support transfer-dst for present copy.",
            vk::to_string(swapchainFormat));
        return false;
    }

    nr::rhi::GraphicsPipelineDesc meshPipelineDesc{};
    meshPipelineDesc.entryPointNames = {"taskMain", "meshMain", "fragmentMain"};
    meshPipelineDesc.colorAttachmentFormats = {*outputFormat};
    meshPipelineDesc.cullMode = vk::CullModeFlagBits::eNone;
    meshPipelineDesc.depthTestEnable = false;
    meshPipelineDesc.depthWriteEnable = false;
    meshPipelineDesc.mode = nr::rhi::GraphicsPipelineMode::Mesh;

    auto meshPipelineState = device.pipeline().createGraphicsPipeline(program, meshPipelineDesc);
    if (!meshPipelineState.pipeline.valid())
    {
        std::println("[error] failed to create graphics mesh pipeline.");
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
    outputImageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
    outputImageInfo.sharingMode = vk::SharingMode::eExclusive;
    outputImageInfo.initialLayout = vk::ImageLayout::eUndefined;
    auto outputImage = device.resourceFactory.createImage(outputImageInfo, nr::rhi::MemoryUsage::GpuOnly, "mesh_test_output");

    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    auto const computeQueueFamily = device.queueManager.compute().queueFamilyIndex();

    auto meshVertices = std::array{
        MeshInputVertex{
            .position = {0.0f, -0.65f, 0.0f, 1.0f},
            .color = {1.0f, 0.15f, 0.15f, 1.0f},
        },
        MeshInputVertex{
            .position = {0.7f, 0.6f, 0.0f, 1.0f},
            .color = {0.15f, 1.0f, 0.15f, 1.0f},
        },
        MeshInputVertex{
            .position = {-0.7f, 0.6f, 0.0f, 1.0f},
            .color = {0.15f, 0.35f, 1.0f, 1.0f},
        },
    };
    auto meshIndices = std::array<std::uint32_t, 3>{0u, 1u, 2u};

    CameraData cameraData{};
    cameraData.origin = {0.0f, 0.0f, -2.5f, 1.0f};
    cameraData.right = {0.8f, 0.0f, 0.0f, 0.0f};
    cameraData.up = {0.0f, 0.8f, 0.0f, 0.0f};
    cameraData.forward = {0.0f, 0.0f, 1.0f, 0.0f};

    vk::BufferCreateInfo cameraBufferInfo{};
    cameraBufferInfo.size = sizeof(CameraData);
    cameraBufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    cameraBufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto cameraBuffer = device.resourceFactory.createBuffer(cameraBufferInfo, nr::rhi::MemoryUsage::CpuToGpu, "mesh_camera_upload");
    cameraBuffer.write(std::as_bytes(std::span{&cameraData, 1}));
    cameraBuffer.flush();

    vk::BufferCreateInfo meshVerticesStagingInfo{};
    meshVerticesStagingInfo.size = sizeof(meshVertices);
    meshVerticesStagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    meshVerticesStagingInfo.sharingMode = vk::SharingMode::eExclusive;
    auto meshVerticesStaging = device.resourceFactory.createBuffer(meshVerticesStagingInfo, nr::rhi::MemoryUsage::CpuToGpu, "mesh_vertices_staging");
    meshVerticesStaging.write(std::as_bytes(std::span{&meshVertices, 1}));
    meshVerticesStaging.flush();

    vk::BufferCreateInfo meshVerticesBufferInfo{};
    meshVerticesBufferInfo.size = sizeof(meshVertices);
    meshVerticesBufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer;
    meshVerticesBufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto meshVerticesBuffer = device.resourceFactory.createBuffer(meshVerticesBufferInfo, nr::rhi::MemoryUsage::GpuOnly, "mesh_vertices_gpu");

    vk::BufferCreateInfo meshIndicesStagingInfo{};
    meshIndicesStagingInfo.size = sizeof(meshIndices);
    meshIndicesStagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    meshIndicesStagingInfo.sharingMode = vk::SharingMode::eExclusive;
    auto meshIndicesStaging = device.resourceFactory.createBuffer(meshIndicesStagingInfo, nr::rhi::MemoryUsage::CpuToGpu, "mesh_indices_staging");
    meshIndicesStaging.write(std::as_bytes(std::span{&meshIndices, 1}));
    meshIndicesStaging.flush();

    vk::BufferCreateInfo meshIndicesBufferInfo{};
    meshIndicesBufferInfo.size = sizeof(meshIndices);
    meshIndicesBufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer;
    meshIndicesBufferInfo.sharingMode = vk::SharingMode::eExclusive;
    auto meshIndicesBuffer = device.resourceFactory.createBuffer(meshIndicesBufferInfo, nr::rhi::MemoryUsage::GpuOnly, "mesh_indices_gpu");

    auto setupTransferToGraphics = nr::rhi::sync::createSemaphore(device.device);

    nr::rhi::CommandPool setupTransferPool(device.device, transferQueueFamily, vk::CommandPoolCreateFlagBits::eTransient);
    auto setupTransferBuffers = setupTransferPool.allocatePrimary(1);
    auto &setupTransfer = setupTransferBuffers.front();

    nr::rhi::CommandPool setupGraphicsPool(device.device, graphicsQueueFamily, vk::CommandPoolCreateFlagBits::eTransient);
    auto setupGraphicsBuffers = setupGraphicsPool.allocatePrimary(1);
    auto &setupGraphics = setupGraphicsBuffers.front();

    nr::rhi::CommandRecorder::beginPrimary(setupTransfer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *setupTransfer;

        nr::rhi::ops::copyBuffer(raw, meshVerticesStaging, meshVerticesBuffer, meshVerticesBuffer.size());
        nr::rhi::ops::copyBuffer(raw, meshIndicesStaging, meshIndicesBuffer, meshIndicesBuffer.size());

        nr::rhi::ops::BarrierBatch transferReleaseBarriers{};
        transferReleaseBarriers.add(nr::rhi::ops::makeBufferOwnershipReleaseBarrier(
            meshVerticesBuffer,
            transferQueueFamily,
            graphicsQueueFamily,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite));
        transferReleaseBarriers.add(nr::rhi::ops::makeBufferOwnershipReleaseBarrier(
            meshIndicesBuffer,
            transferQueueFamily,
            graphicsQueueFamily,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferWrite));
        nr::rhi::ops::pipelineBarrier(raw, transferReleaseBarriers);
    }
    nr::rhi::CommandRecorder::end(setupTransfer);

    nr::rhi::CommandRecorder::beginPrimary(setupGraphics, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto raw = *setupGraphics;

        nr::rhi::ops::BarrierBatch transferAcquireBarriers{};
        transferAcquireBarriers.add(nr::rhi::ops::makeBufferOwnershipAcquireBarrier(
            meshVerticesBuffer,
            transferQueueFamily,
            graphicsQueueFamily,
            vk::PipelineStageFlagBits2::eMeshShaderEXT,
            vk::AccessFlagBits2::eShaderRead));
        transferAcquireBarriers.add(nr::rhi::ops::makeBufferOwnershipAcquireBarrier(
            meshIndicesBuffer,
            transferQueueFamily,
            graphicsQueueFamily,
            vk::PipelineStageFlagBits2::eMeshShaderEXT,
            vk::AccessFlagBits2::eShaderRead));
        nr::rhi::ops::pipelineBarrier(raw, transferAcquireBarriers);
    }
    nr::rhi::CommandRecorder::end(setupGraphics);

    nr::rhi::CommandBatch setupTransferBatch{};
    setupTransferBatch.addCommandBuffer(setupTransfer);
    setupTransferBatch.addSignal(setupTransferToGraphics, 0, 0, vk::PipelineStageFlagBits2::eTransfer);
    device.queueManager.transfer().submit(setupTransferBatch);

    nr::rhi::CommandBatch setupGraphicsBatch{};
    setupGraphicsBatch.addCommandBuffer(setupGraphics);
    setupGraphicsBatch.addWait(setupTransferToGraphics, vk::PipelineStageFlagBits2::eAllCommands);
    device.queueManager.graphics().submit(setupGraphicsBatch);
    device.queueManager.graphics().waitIdle();

    auto descriptorSets = device.pipeline().allocateBindingSets(meshPipelineState.layout, meshPipelineState.bindingPool);
    if (descriptorSets.empty())
    {
        std::println("[error] no descriptor sets allocated for mesh pipeline.");
        return false;
    }

    nr::rhi::ShaderResourceWriter writer{};
    auto root = meshPipelineState.descriptorLayout.rootCursor();
    if (!root.valid())
    {
        std::println("[error] descriptor root cursor is invalid.");
        return false;
    }

    if (!writer.bindUniformBuffer(root["camera"], cameraBuffer, 0, sizeof(CameraData)))
    {
        std::println("[error] failed to bind camera uniform buffer descriptor.");
        return false;
    }
    if (!writer.bindStorageBuffer(root["meshVertices"], meshVerticesBuffer, 0, sizeof(meshVertices)))
    {
        std::println("[error] failed to bind meshVertices storage buffer descriptor.");
        return false;
    }
    if (!writer.bindStorageBuffer(root["meshIndices"], meshIndicesBuffer, 0, sizeof(meshIndices)))
    {
        std::println("[error] failed to bind meshIndices storage buffer descriptor.");
        return false;
    }
    writer.commit(meshPipelineState.bindingPool, descriptorSets);

    auto frameGraphicsCommandOwnership = std::vector<std::optional<vk::raii::CommandBuffers>>(device.frameManager.frameCount());
    auto frameComputeCommandOwnership = std::vector<std::optional<vk::raii::CommandBuffers>>(device.frameManager.frameCount());

    auto graphicsToComputeSemaphores = std::vector<vk::raii::Semaphore>{};
    graphicsToComputeSemaphores.reserve(device.frameManager.frameCount());
    std::ranges::for_each(std::views::iota(size_t{0}, device.frameManager.frameCount()), [&](size_t) {
        graphicsToComputeSemaphores.push_back(nr::rhi::sync::createSemaphore(device.device));
    });

    bool outputReadyForRender = false;
    std::uint32_t presentedFrames = 0;
    constexpr std::uint32_t kMaxFrames = 90;
    auto swapchainImageReadyForTransfer = std::vector<bool>(device.presentationContext.swapchainImageCount(), false);

    for (auto frameIndex : std::views::iota(std::uint32_t{0}, kMaxFrames))
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

            nr::rhi::ops::BarrierBatch graphicsInputBarriers{};
            if (outputReadyForRender)
            {
                graphicsInputBarriers.add(nr::rhi::ops::makeImageOwnershipAcquireBarrier(
                    outputImage,
                    computeQueueFamily,
                    graphicsQueueFamily,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite));
            }
            else
            {
                vk::ImageMemoryBarrier2 outputToColor{};
                outputToColor.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
                outputToColor.srcAccessMask = vk::AccessFlags2{};
                outputToColor.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                outputToColor.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                outputToColor.oldLayout = vk::ImageLayout::eUndefined;
                outputToColor.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
                outputToColor.srcQueueFamilyIndex = nr::rhi::ops::kIgnoredQueueFamilyIndex;
                outputToColor.dstQueueFamilyIndex = nr::rhi::ops::kIgnoredQueueFamilyIndex;
                graphicsInputBarriers.add(nr::rhi::ops::makeImageBarrier(outputImage, outputToColor));
            }
            nr::rhi::ops::pipelineBarrier(raw, graphicsInputBarriers);

            auto colorAttachment = nr::rhi::ops::RenderingAttachmentDesc{
                .imageView = *outputImage.view(),
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .resolveMode = vk::ResolveModeFlagBits::eNone,
                .resolveImageView = {},
                .resolveImageLayout = vk::ImageLayout::eUndefined,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.02f, 0.02f, 0.03f, 1.0f}}},
            };

            auto colorAttachments = std::array{colorAttachment};
            nr::rhi::ops::RenderingScopeDesc renderingScope{};
            renderingScope.renderArea = vk::Rect2D{vk::Offset2D{0, 0}, swapchainExtent};
            renderingScope.layerCount = 1;
            renderingScope.colorAttachments = colorAttachments;

            {
                nr::rhi::ops::ScopedRendering rendering(raw, renderingScope);

                meshPipelineState.layout.bindDescriptorSets(raw, vk::PipelineBindPoint::eGraphics, descriptorSets);
                raw.bindPipeline(vk::PipelineBindPoint::eGraphics, meshPipelineState.pipeline.raw());

                auto viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f};
                raw.setViewport(0, {viewport});
                auto scissor = vk::Rect2D{vk::Offset2D{0, 0}, swapchainExtent};
                raw.setScissor(0, {scissor});

                auto rasterState = nr::rhi::MeshRasterState{
                    .cullMode = vk::CullModeFlagBits::eNone,
                    .frontFace = vk::FrontFace::eCounterClockwise,
                    .depthTestEnable = vk::False,
                    .depthWriteEnable = vk::False,
                    .depthCompareOp = vk::CompareOp::eAlways,
                    .polygonMode = vk::PolygonMode::eFill,
                    .rasterizationSamples = vk::SampleCountFlagBits::e1,
                };
                nr::rhi::mesh::applyRasterState(graphicsCommandBuffer, rasterState);
                nr::rhi::mesh::drawTasks(graphicsCommandBuffer, 1, 1, 1);
            }

            nr::rhi::ops::BarrierBatch graphicsToComputeRelease{};
            graphicsToComputeRelease.add(nr::rhi::ops::makeImageOwnershipReleaseBarrier(
                outputImage,
                graphicsQueueFamily,
                computeQueueFamily,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite));
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
            computeInputBarriers.add(nr::rhi::ops::makeImageOwnershipAcquireBarrier(
                outputImage,
                graphicsQueueFamily,
                computeQueueFamily,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead));

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
            computeToGraphicsRelease.add(nr::rhi::ops::makeImageOwnershipReleaseBarrier(
                outputImage,
                computeQueueFamily,
                graphicsQueueFamily,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead));
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

        outputReadyForRender = true;
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

    std::println("[ok] presented {} frames with minimal mesh pipeline.", presentedFrames);
    return true;
}
} // namespace

int main()
{
    try
    {
        if (!runMinimalMeshPipelineTest())
        {
            std::println("[FAIL] minimal mesh pipeline test failed");
            return 1;
        }

        std::println("[OK] minimal mesh pipeline test passed");
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::println("[error] exception: {}", exception.what());
        return 2;
    }
}
