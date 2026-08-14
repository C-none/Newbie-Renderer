import std;
import dependency.dlss;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeUploadOwnershipPlan(const nr::rhi::Device &device)
{
    return nr::rhi::ops::BufferUploadOwnershipPlan{
        .releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
            device.queueManager.transfer().queueFamilyIndex(), device.queueManager.graphics().queueFamilyIndex(),
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV,
                .access = vk::AccessFlagBits2::eTransferRead,
            }),
    };
}

void acquireUploadedBufferOnGraphics(nr::rhi::Device &device, const nr::rhi::ops::BufferUploadTicket &ticket)
{
    auto commandPool = nr::rhi::CommandPool{
        device.device,
        device.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    auto commandBuffers = commandPool.allocatePrimary(1);
    auto const &commandBuffer = commandBuffers.front();
    nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    device.uploadReadback().recordAcquireBarrier(commandBuffer, ticket);
    nr::rhi::CommandRecorder::end(commandBuffer);

    auto batch = nr::rhi::CommandBatch{};
    batch.addWait(device.uploadReadback().uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands,
                  ticket.signalValue);
    batch.addCommandBuffer(commandBuffer);
    device.queueManager.graphics().submit(std::move(batch));
    device.queueManager.graphics().waitIdle();
}

const nr::test::CaseRegistrar deviceCapabilityCase{
    "rhi device initialization exposes required target capabilities", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_device_capability_contract_test", "NewbieRenderer");

        nr::test::require(device.queueManager.graphics().valid(), "graphics queue should be valid");
        nr::test::require(device.queueManager.compute().valid(), "compute queue should be valid");
        nr::test::require(device.queueManager.transfer().valid(), "transfer queue should be valid");
        nr::test::require(
            device.physicalDevice.getSurfaceSupportKHR(device.queueManager.compute().queueFamilyIndex(),
                                                        device.presentationContext.surfaceHandle()),
            "selected compute queue family should support the active presentation surface");
        nr::test::require(device.resourceFactory.valid(), "resource factory should be initialized");
        nr::test::require(device.resourcePool.valid(), "resource pool should be initialized");
        nr::test::require(device.uploadReadback().valid(), "upload/readback context should be initialized");
        nr::test::require(device.hasEnabledInstanceExtension(vk::EXTSurfaceMaintenance1ExtensionName),
                          "surface maintenance1 instance extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRSwapchainExtensionName),
                          "swapchain device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTSwapchainMaintenance1ExtensionName),
                          "swapchain maintenance1 device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRDeferredHostOperationsExtensionName),
                          "deferred host operations device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRAccelerationStructureExtensionName),
                          "acceleration structure device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRRayTracingPipelineExtensionName),
                          "ray tracing pipeline device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTRayTracingInvocationReorderExtensionName),
                          "path-tracing shader invocation reorder extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRPipelineLibraryExtensionName),
                          "pipeline library device extension should be enabled for ray tracing");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRPipelineBinaryExtensionName),
                          "pipeline binary device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTMemoryBudgetExtensionName),
                          "memory budget device extension should be enabled for VMA");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRMaintenance8ExtensionName),
                          "maintenance8 device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRMaintenance9ExtensionName),
                          "maintenance9 device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTFullScreenExclusiveExtensionName),
                          "full-screen exclusive device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::NVCooperativeVectorExtensionName),
                          "cooperative-vector device extension should be enabled globally");
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTShaderReplicatedCompositesExtensionName),
                          "shader replicated-composites extension should be enabled for CoopVec SPIR-V");
        nr::test::require(!device.frameBoundaryEnabled() ||
                              device.hasEnabledDeviceExtension(vk::EXTFrameBoundaryExtensionName),
                          "enabled frame-boundary support should appear in the final device extension set");
        nr::test::require(!device.hdrMetadataEnabled() ||
                              device.hasEnabledDeviceExtension(vk::EXTHdrMetadataExtensionName),
                          "enabled HDR metadata support should appear in the final device extension set");

        auto const disabledDeviceExtensions = std::array<std::string_view, 5>{
            "VK_EXT_mesh_shader",
            "VK_KHR_ray_tracing_maintenance1",
            "VK_KHR_ray_query",
            "VK_EXT_opacity_micromap",
            "VK_EXT_extended_dynamic_state3",
        };
        std::ranges::for_each(disabledDeviceExtensions, [&](std::string_view extension) {
            nr::test::require(!device.hasEnabledDeviceExtension(extension),
                              std::format("dead device extension '{}' must remain disabled", extension));
        });
        nr::test::require(!device.hasEnabledDeviceExtension(vk::NVCommandBufferInheritanceExtensionName),
                          "NV command buffer state inheritance must remain disabled");

        if (nr::dependency::dlss::sdkCompiled())
        {
            auto const dlssExtensions = nr::dependency::dlss::rayReconstructionDeviceExtensions(
                *device.instance, *device.physicalDevice);
            nr::test::require(dlssExtensions.status.success(),
                              "DLSS device-extension discovery should succeed when its SDK is compiled");
            std::ranges::for_each(dlssExtensions.names, [&](std::string_view extension) {
                nr::test::require(device.hasEnabledDeviceExtension(extension),
                                  std::format("DLSS extension '{}' should appear in the final enabled set", extension));
            });
        }

        auto const suboptimalPresent =
            nr::rhi::SwapChain::resolvePresentResult(vk::Result::eSuccess, vk::Result::eSuboptimalKHR);
        nr::test::require(suboptimalPresent.result == vk::Result::eSuboptimalKHR && suboptimalPresent.queued,
                          "single-swapchain suboptimal pResults should be observable as a queued suboptimal present");
        auto const outOfDatePresent =
            nr::rhi::SwapChain::resolvePresentResult(vk::Result::eSuccess, vk::Result::eErrorOutOfDateKHR);
        nr::test::require(outOfDatePresent.result == vk::Result::eErrorOutOfDateKHR && !outOfDatePresent.queued,
                          "single-swapchain out-of-date pResults should request recreation without pending a fence");
        auto const failedPresent =
            nr::rhi::SwapChain::resolvePresentResult(vk::Result::eErrorOutOfDateKHR, vk::Result::eSuccess);
        nr::test::require(failedPresent.result == vk::Result::eErrorOutOfDateKHR && !failedPresent.queued,
                          "a failed queue present should remain visible and must not pend a present fence");

        nr::test::require(device.queueFamilyTransferPolicy().maintenance9,
                          "queue family transfer policy should be backed by maintenance9");
        nr::test::require(!device.queueFamilyTransferPolicy().optimalImageTransferToQueueFamilies.empty(),
                          "maintenance9 queue family ownership transfer masks should be populated");

        auto const graphicsQueueFamilyIndex = device.queueManager.graphics().queueFamilyIndex();
        auto const computeQueueFamilyIndex = device.queueManager.compute().queueFamilyIndex();
        auto const pathTracingGuideUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                                           vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
        auto const canImplicitlyAcquirePathTracingGuides = [&](std::uint32_t srcQueueFamilyIndex,
                                                               std::uint32_t dstQueueFamilyIndex) {
            return srcQueueFamilyIndex == dstQueueFamilyIndex ||
                   device.queueFamilyTransferPolicy().canOmitImageQueueFamilyTransfer(
                       srcQueueFamilyIndex, dstQueueFamilyIndex, vk::ImageTiling::eOptimal, pathTracingGuideUsage);
        };
        nr::test::require(canImplicitlyAcquirePathTracingGuides(graphicsQueueFamilyIndex, computeQueueFamilyIndex),
                          "target GPU should preserve PathTracing guides from graphics to compute without an explicit "
                          "ownership transfer");
        nr::test::require(canImplicitlyAcquirePathTracingGuides(computeQueueFamilyIndex, graphicsQueueFamilyIndex),
                          "target GPU should preserve retained PathTracing guides from compute to graphics without an "
                          "explicit ownership transfer");

        auto const &rt = device.rayTracingCapabilities();
        nr::test::require(rt.shaderGroupHandleSize > 0, "shader-group handle size should be populated");
        nr::test::require(rt.shaderGroupHandleAlignment > 0, "shader-group handle alignment should be populated");
        nr::test::require(rt.shaderGroupBaseAlignment > 0, "shader-group base alignment should be populated");
        nr::test::require(rt.maxShaderGroupStride > 0, "shader-group stride limit should be populated");
        nr::test::require(rt.maxRayDispatchInvocationCount > 0,
                          "ray dispatch invocation limit should be populated");
        nr::test::require(rt.maxRayRecursionDepth > 0, "ray recursion depth limit should be populated");
        nr::test::require(std::ranges::all_of(rt.maxDispatchDimensions, [](std::uint64_t limit) { return limit > 0; }),
                          "ray dispatch dimension limits should be populated");

        auto const &cooperativeVector = device.cooperativeVectorCapabilities();
        nr::test::require(cooperativeVector.extensionEnabled,
                          "cooperative-vector extension must be enabled in the logical-device contract");
        nr::test::require(cooperativeVector.cooperativeVectorFeatureEnabled,
                          "cooperativeVector device feature must be enabled");
        nr::test::require(cooperativeVector.cooperativeVectorTrainingFeatureEnabled,
                          "cooperativeVectorTraining device feature must be enabled");
        nr::test::require(cooperativeVector.shaderFloat16FeatureEnabled,
                          "shaderFloat16 device feature must be enabled");
        nr::test::require(cooperativeVector.vulkanMemoryModelFeatureEnabled,
                          "vulkanMemoryModel device feature must be enabled for CoopVec SPIR-V");
        nr::test::require(cooperativeVector.shaderReplicatedCompositesFeatureEnabled,
                          "shaderReplicatedComposites device feature must be enabled for CoopVec SPIR-V");
        nr::test::require(cooperativeVector.storageBuffer16BitAccessFeatureEnabled,
                          "storageBuffer16BitAccess device feature must be enabled");
        nr::test::require(cooperativeVector.uniformAndStorageBuffer16BitAccessFeatureEnabled,
                          "uniformAndStorageBuffer16BitAccess device feature must be enabled");
        nr::test::require(cooperativeVector.computeStage,
                          "cooperative vectors must support compute shader execution");
        nr::test::require(cooperativeVector.raygenStage,
                          "cooperative vectors must support raygen shader execution");
        nr::test::require(cooperativeVector.closestHitStage,
                          "cooperative vectors must support closest-hit shader execution");
        nr::test::require(cooperativeVector.trainingFloat16Accumulation,
                          "cooperative vectors must support FP16 training accumulation");
        nr::test::require(cooperativeVector.fullFloat16Tuple,
                          "cooperative vectors must expose the all-FP16 tuple without transpose support");
        nr::test::require(cooperativeVector.fullFloat16TupleWithTranspose,
                          "cooperative vectors must expose the all-FP16 tuple with transpose support");
        nr::test::require(cooperativeVector.maxComponents >= 32u,
                          "cooperative vectors must expose at least 32 components");

        auto const rowMajorLayout = device.cooperativeVectorMatrixLayoutSize(nr::rhi::CooperativeVectorMatrixDesc{
            .rows = 32u,
            .columns = 32u,
            .layout = nr::rhi::CooperativeVectorMatrixLayout::RowMajor,
            .rowStrideBytes = 64u,
        });
        nr::test::require(rowMajorLayout.byteSize == 2048u,
                          "row-major FP16 cooperative-vector matrix layout size should be exact");
        auto const trainingOptimalLayout = device.cooperativeVectorMatrixLayoutSize(nr::rhi::CooperativeVectorMatrixDesc{
            .rows = 32u,
            .columns = 32u,
            .layout = nr::rhi::CooperativeVectorMatrixLayout::TrainingOptimal,
        });
        nr::test::require(trainingOptimalLayout.byteSize > 0u,
                          "TrainingOptimal cooperative-vector matrix layout size should be queryable");
        nr::test::require(nr::rhi::kCooperativeVectorMatrixDeviceAddressAlignment == 64u,
                          "VK_NV_cooperative_vector fixes conversion device-address alignment at 64 bytes");
        auto const paddedRowMajorLayout = device.cooperativeVectorMatrixLayoutSize(
            nr::rhi::CooperativeVectorMatrixDesc{
                .rows = 3u,
                .columns = 8u,
                .layout = nr::rhi::CooperativeVectorMatrixLayout::RowMajor,
                .rowStrideBytes = 32u,
            });
        nr::test::require(paddedRowMajorLayout.byteSize == 80u,
                          "row-major matrix size must exclude padding after its final row");

        auto bufferInfo = vk::BufferCreateInfo{};
        bufferInfo.size = 16;
        bufferInfo.usage = vk::BufferUsageFlagBits::eUniformTexelBuffer;
        auto buffer = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                          "move_assignment_buffer_destination");
        static_cast<void>(buffer.addView(vk::Format::eR32Uint));
        auto replacementBuffer = device.resourceFactory.createBuffer(
            bufferInfo, nr::rhi::MemoryUsage::GpuOnly, "move_assignment_buffer_source");
        static_cast<void>(replacementBuffer.addView(vk::Format::eR32Uint));
        buffer = std::move(replacementBuffer);
        nr::test::require(buffer.valid(), "move-assigned buffer should remain valid");

        auto imageInfo = vk::ImageCreateInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.extent = vk::Extent3D{1, 1, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled;
        auto image = device.resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                        "move_assignment_image_destination");
        auto replacementImage = device.resourceFactory.createImage(
            imageInfo, nr::rhi::MemoryUsage::GpuOnly, "move_assignment_image_source");
        image = std::move(replacementImage);
        nr::test::require(image.valid(), "move-assigned image should remain valid");

        device.waitIdle();
    }};

const nr::test::CaseRegistrar cooperativeVectorRoundtripCase{
    "rhi cooperative-vector conversion roundtrips FP16 row-major bytes through TrainingOptimal", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_cooperative_vector_roundtrip_test", "NewbieRenderer");

        constexpr auto values = std::array<std::uint16_t, 32>{
            0x0000u, 0x3C00u, 0x4000u, 0x4200u, 0x4400u, 0x4500u, 0x4600u, 0x4700u,
            0x4800u, 0x4880u, 0x4900u, 0x4980u, 0x4A00u, 0x4A80u, 0x4B00u, 0x4B80u,
            0xBC00u, 0xC000u, 0xC200u, 0xC400u, 0xC500u, 0xC600u, 0xC700u, 0xC800u,
            0xC880u, 0xC900u, 0xC980u, 0xCA00u, 0xCA80u, 0xCB00u, 0xCB80u, 0x4C00u,
        };
        auto const valueBytes = std::as_bytes(std::span<const std::uint16_t>{values.data(), values.size()});
        auto const rowMajor = nr::rhi::CooperativeVectorMatrixDesc{
            .rows = 4u,
            .columns = 8u,
            .layout = nr::rhi::CooperativeVectorMatrixLayout::RowMajor,
            .rowStrideBytes = 16u,
        };
        auto const trainingOptimal = nr::rhi::CooperativeVectorMatrixDesc{
            .rows = rowMajor.rows,
            .columns = rowMajor.columns,
            .layout = nr::rhi::CooperativeVectorMatrixLayout::TrainingOptimal,
        };
        auto const rowMajorSize = device.cooperativeVectorMatrixLayoutSize(rowMajor);
        auto const trainingOptimalSize = device.cooperativeVectorMatrixLayoutSize(trainingOptimal);

        auto inputInfo = vk::BufferCreateInfo{};
        inputInfo.size = valueBytes.size_bytes();
        inputInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        auto input = device.resourceFactory.createBuffer(inputInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                         "cooperative_vector_roundtrip_input");

        auto optimalInfo = vk::BufferCreateInfo{};
        optimalInfo.size = trainingOptimalSize.byteSize;
        optimalInfo.usage = vk::BufferUsageFlagBits::eShaderDeviceAddress;
        auto optimal = device.resourceFactory.createBuffer(optimalInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                           "cooperative_vector_roundtrip_optimal");

        auto outputInfo = vk::BufferCreateInfo{};
        outputInfo.size = valueBytes.size_bytes();
        outputInfo.usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        auto output = device.resourceFactory.createBuffer(outputInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                          "cooperative_vector_roundtrip_output");

        auto upload = device.uploadReadback().uploadBuffer(valueBytes, input, 0u, makeUploadOwnershipPlan(device));
        nr::test::require(upload.valid(), "cooperative-vector source upload should succeed");
        acquireUploadedBufferOnGraphics(device, upload);

        auto commandPool = nr::rhi::CommandPool{
            device.device,
            device.queueManager.graphics().queueFamilyIndex(),
            vk::CommandPoolCreateFlagBits::eTransient,
        };
        auto commandBuffers = commandPool.allocatePrimary(1);
        auto const &commandBuffer = commandBuffers.front();
        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        device.recordCooperativeVectorMatrixConversion(
            commandBuffer,
            nr::rhi::CooperativeVectorMatrixMemory{
                .deviceAddress = input.deviceAddress(),
                .size = rowMajorSize.byteSize,
            },
            rowMajor,
            rowMajorSize,
            nr::rhi::CooperativeVectorMatrixMemory{
                .deviceAddress = optimal.deviceAddress(),
                .size = trainingOptimalSize.byteSize,
            },
            trainingOptimal,
            trainingOptimalSize);

        auto conversionBarrier = vk::MemoryBarrier2{};
        conversionBarrier.srcStageMask = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV;
        conversionBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        conversionBarrier.dstStageMask = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV;
        conversionBarrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
        auto dependencyInfo = vk::DependencyInfo{};
        dependencyInfo.memoryBarrierCount = 1u;
        dependencyInfo.pMemoryBarriers = std::addressof(conversionBarrier);
        commandBuffer.pipelineBarrier2(dependencyInfo);

        device.recordCooperativeVectorMatrixConversion(
            commandBuffer,
            nr::rhi::CooperativeVectorMatrixMemory{
                .deviceAddress = optimal.deviceAddress(),
                .size = trainingOptimalSize.byteSize,
            },
            trainingOptimal,
            trainingOptimalSize,
            nr::rhi::CooperativeVectorMatrixMemory{
                .deviceAddress = output.deviceAddress(),
                .size = rowMajorSize.byteSize,
            },
            rowMajor,
            rowMajorSize);
        nr::rhi::CommandRecorder::end(commandBuffer);
        device.queueManager.graphics().submit(commandBuffer);
        device.queueManager.graphics().waitIdle();

        auto readback = device.uploadReadback().readbackBuffer(
            output, 0u, valueBytes.size_bytes(), nr::rhi::QueueRole::Graphics,
            nr::rhi::ops::ReadbackSyncPlan{
                .preCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV,
                    .access = vk::AccessFlagBits2::eTransferWrite,
                },
                .postCopy = nr::rhi::ops::ReadbackSyncScope{
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                },
            });
        auto const bytes = device.uploadReadback().readbackBytes(readback);
        nr::test::require(std::ranges::equal(bytes, valueBytes),
                          "row-major FP16 bytes must survive TrainingOptimal conversion roundtrip");

        device.waitIdle();
    }};
} // namespace
