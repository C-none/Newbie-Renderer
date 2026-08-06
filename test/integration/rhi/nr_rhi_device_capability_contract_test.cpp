import std;
import dependency.dlss;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
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
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTMemoryBudgetExtensionName),
                          "memory budget device extension should be enabled for VMA");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRMaintenance8ExtensionName),
                          "maintenance8 device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRMaintenance9ExtensionName),
                          "maintenance9 device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::EXTFullScreenExclusiveExtensionName),
                          "full-screen exclusive device extension should be enabled");
        nr::test::require(!device.frameBoundaryEnabled() ||
                              device.hasEnabledDeviceExtension(vk::EXTFrameBoundaryExtensionName),
                          "enabled frame-boundary support should appear in the final device extension set");
        nr::test::require(!device.hdrMetadataEnabled() ||
                              device.hasEnabledDeviceExtension(vk::EXTHdrMetadataExtensionName),
                          "enabled HDR metadata support should appear in the final device extension set");

        auto const disabledDeviceExtensions = std::array<std::string_view, 6>{
            "VK_EXT_mesh_shader",
            "VK_KHR_ray_tracing_maintenance1",
            "VK_KHR_ray_query",
            "VK_EXT_opacity_micromap",
            "VK_NV_cooperative_vector",
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
} // namespace
