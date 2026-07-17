import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
const nr::test::CaseRegistrar deviceCapabilityCase{
    "rhi device initialization exposes required target capabilities",
    [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_rhi_device_capability_contract_test", "NewbieRenderer");

        nr::test::require(device.queueManager.graphics().valid(), "graphics queue should be valid");
        nr::test::require(device.queueManager.compute().valid(), "compute queue should be valid");
        nr::test::require(device.queueManager.transfer().valid(), "transfer queue should be valid");
        nr::test::require(device.resourceFactory.valid(), "resource factory should be initialized");
        nr::test::require(device.resourcePool.valid(), "resource pool should be initialized");
        nr::test::require(device.uploadReadback().valid(), "upload/readback context should be initialized");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRSwapchainExtensionName),
                          "swapchain device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRMaintenance8ExtensionName),
                          "maintenance8 device extension should be enabled");
        nr::test::require(device.hasEnabledDeviceExtension(vk::KHRMaintenance9ExtensionName),
                          "maintenance9 device extension should be enabled");
        nr::test::require(!device.hasEnabledDeviceExtension(vk::NVCommandBufferInheritanceExtensionName),
                          "NV command buffer state inheritance must remain disabled");

        auto const &descriptorIndexing = device.descriptorIndexingCapabilities();
        nr::test::require(descriptorIndexing.descriptorIndexing, "descriptor indexing should be enabled");
        nr::test::require(descriptorIndexing.runtimeDescriptorArray, "runtime descriptor arrays should be enabled");
        nr::test::require(descriptorIndexing.descriptorBindingPartiallyBound, "partially bound descriptors should be enabled");
        nr::test::require(descriptorIndexing.descriptorBindingVariableDescriptorCount, "variable descriptor counts should be enabled");
        nr::test::require(descriptorIndexing.maxDescriptorSetUpdateAfterBindSampledImages > 0,
                          "sampled-image update-after-bind limit should be populated");

        auto const &bufferAddress = device.bufferDeviceAddressCapabilities();
        nr::test::require(bufferAddress.bufferDeviceAddress, "buffer device address should be enabled");

        auto const &vulkan14 = device.vulkan14Capabilities();
        nr::test::require(vulkan14.maintenance5, "Vulkan 1.4 maintenance5 should be enabled on the target profile");
        nr::test::require(vulkan14.maintenance6, "Vulkan 1.4 maintenance6 should be enabled on the target profile");
        nr::test::require(vulkan14.maintenance8, "VK_KHR_maintenance8 should be enabled on the target profile");
        nr::test::require(vulkan14.maintenance9, "VK_KHR_maintenance9 should be enabled on the target profile");
        nr::test::require(
            device.queueFamilyTransferPolicy().maintenance9,
            "queue family transfer policy should be backed by maintenance9");
        nr::test::require(
            !device.queueFamilyTransferPolicy().optimalImageTransferToQueueFamilies.empty(),
            "maintenance9 queue family ownership transfer masks should be populated");

        auto const graphicsQueueFamilyIndex = device.queueManager.graphics().queueFamilyIndex();
        auto const computeQueueFamilyIndex = device.queueManager.compute().queueFamilyIndex();
        auto const pathTracingGuideUsage = vk::ImageUsageFlagBits::eStorage |
                                           vk::ImageUsageFlagBits::eSampled |
                                           vk::ImageUsageFlagBits::eTransferDst |
                                           vk::ImageUsageFlagBits::eTransferSrc;
        auto const canImplicitlyAcquirePathTracingGuides = [&](std::uint32_t srcQueueFamilyIndex,
                                                               std::uint32_t dstQueueFamilyIndex) {
            return srcQueueFamilyIndex == dstQueueFamilyIndex ||
                   device.queueFamilyTransferPolicy().canOmitImageQueueFamilyTransfer(
                       srcQueueFamilyIndex,
                       dstQueueFamilyIndex,
                       vk::ImageTiling::eOptimal,
                       pathTracingGuideUsage);
        };
        nr::test::require(
            canImplicitlyAcquirePathTracingGuides(graphicsQueueFamilyIndex, computeQueueFamilyIndex),
            "target GPU should preserve PathTracing guides from graphics to compute without an explicit ownership transfer");
        nr::test::require(
            canImplicitlyAcquirePathTracingGuides(computeQueueFamilyIndex, graphicsQueueFamilyIndex),
            "target GPU should preserve retained PathTracing guides from compute to graphics without an explicit ownership transfer");

        auto const &rt = device.rayTracingCapabilities();
        nr::test::require(rt.rayTracingMaintenance1, "ray tracing maintenance1 should be enabled");
        nr::test::require(rt.rayTracingPipelineTraceRaysIndirect, "traceRaysIndirect should be enabled");
        nr::test::require(rt.shaderGroupHandleSize > 0, "shader-group handle size should be populated");
        nr::test::require(rt.shaderGroupBaseAlignment > 0, "shader-group base alignment should be populated");
        nr::test::require(rt.maxRayRecursionDepth > 0, "ray recursion depth limit should be populated");

        device.waitIdle();
    }};
} // namespace
