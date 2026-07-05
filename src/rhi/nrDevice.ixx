export module nr.rhi:device;
import dependency.window;
import dependency.nsight;
import dependency.vulkan;

import :vk;
import :surface;
import :swapchain;
import :type;
import :queue;
import :frameContext;
import :memoryAllocator;
import :nsightGraphics;
import :resourcePool;
import :pipeline;
import :resourceOps;
import nr.utils;
import std;

export namespace nr::rhi
{
struct DescriptorIndexingCapabilitySnapshot
{
    bool descriptorIndexing = false;
    bool runtimeDescriptorArray = false;
    bool descriptorBindingPartiallyBound = false;
    bool descriptorBindingVariableDescriptorCount = false;
    bool descriptorBindingSampledImageUpdateAfterBind = false;
    bool descriptorBindingUpdateUnusedWhilePending = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    std::uint32_t maxPerStageDescriptorUpdateAfterBindSampledImages = 0;
    std::uint32_t maxDescriptorSetUpdateAfterBindSampledImages = 0;
};

struct BufferDeviceAddressCapabilitySnapshot
{
    bool bufferDeviceAddress = false;
    bool bufferDeviceAddressCaptureReplay = false;
    bool bufferDeviceAddressMultiDevice = false;
};

struct Vulkan14CapabilitySnapshot
{
    bool globalPriorityQuery = false;
    bool shaderSubgroupRotate = false;
    bool shaderSubgroupRotateClustered = false;
    bool shaderFloatControls2 = false;
    bool shaderExpectAssume = false;
    bool rectangularLines = false;
    bool bresenhamLines = false;
    bool smoothLines = false;
    bool stippledRectangularLines = false;
    bool stippledBresenhamLines = false;
    bool stippledSmoothLines = false;
    bool vertexAttributeInstanceRateDivisor = false;
    bool vertexAttributeInstanceRateZeroDivisor = false;
    bool indexTypeUint8 = false;
    bool dynamicRenderingLocalRead = false;
    bool maintenance5 = false;
    bool maintenance6 = false;
    bool maintenance9 = false;
    bool pipelineProtectedAccess = false;
    bool pipelineRobustness = false;
    bool hostImageCopy = false;
    bool pushDescriptor = false;
};

struct Vulkan14PropertySnapshot
{
    std::uint32_t lineSubPixelPrecisionBits = 0;
    std::uint32_t maxVertexAttribDivisor = 0;
    bool supportsNonZeroFirstInstance = false;
    std::uint32_t maxPushDescriptors = 0;
    bool dynamicRenderingLocalReadDepthStencilAttachments = false;
    bool dynamicRenderingLocalReadMultisampledAttachments = false;
    bool earlyFragmentMultisampleCoverageAfterSampleCounting = false;
    bool earlyFragmentSampleMaskTestBeforeSampleCounting = false;
    bool depthStencilSwizzleOneSupport = false;
    bool polygonModePointSize = false;
    bool nonStrictSinglePixelWideLinesUseParallelogram = false;
    bool nonStrictWideLinesUseParallelogram = false;
    bool blockTexelViewCompatibleMultipleLayers = false;
    std::uint32_t maxCombinedImageSamplerDescriptorCount = 0;
    bool fragmentShadingRateClampCombinerInputs = false;
    vk::PipelineRobustnessBufferBehavior defaultRobustnessStorageBuffers = vk::PipelineRobustnessBufferBehavior::eDeviceDefault;
    vk::PipelineRobustnessBufferBehavior defaultRobustnessUniformBuffers = vk::PipelineRobustnessBufferBehavior::eDeviceDefault;
    vk::PipelineRobustnessBufferBehavior defaultRobustnessVertexInputs = vk::PipelineRobustnessBufferBehavior::eDeviceDefault;
    vk::PipelineRobustnessImageBehavior defaultRobustnessImages = vk::PipelineRobustnessImageBehavior::eDeviceDefault;
    std::vector<vk::ImageLayout> hostImageCopySrcLayouts{};
    std::vector<vk::ImageLayout> hostImageCopyDstLayouts{};
    std::array<std::uint8_t, vk::UuidSize> optimalTilingLayoutUUID{};
    bool identicalMemoryTypeRequirements = false;
};

class Device
{
  public:
    struct FrameBeginResult
    {
        std::uint32_t frameIndex = 0;
        std::uint32_t swapchainImageIndex = 0;
        vk::Result swapchainResult = vk::Result::eSuccess;
        double cpuWaitGpuMilliseconds = 0.0;
    };

    std::string appName;
    std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance = {nullptr};
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger = {nullptr};
    vk::raii::PhysicalDevice physicalDevice = {nullptr};
    vk::raii::Device device = {nullptr};

    MemoryAllocator memoryAllocator;
    ResourceFactory resourceFactory;
    ResourcePool resourcePool;

    QueueManager queueManager;
    FrameManager frameManager;

    PresentationContext presentationContext;
    PipelineService pipelineService;
    std::optional<ops::UploadReadbackContext> uploadReadbackContext_{};

    Device() = default;
    Device(Device &) = delete;
    Device &operator=(Device &) = delete;

    [[nodiscard]] const RayTracingCapabilitySnapshot &rayTracingCapabilities() const noexcept;

    [[nodiscard]] const DescriptorIndexingCapabilitySnapshot &descriptorIndexingCapabilities() const noexcept;

    [[nodiscard]] const BufferDeviceAddressCapabilitySnapshot &bufferDeviceAddressCapabilities() const noexcept;

    [[nodiscard]] const Vulkan14CapabilitySnapshot &vulkan14Capabilities() const noexcept;

    [[nodiscard]] const Vulkan14PropertySnapshot &vulkan14Properties() const noexcept;

    [[nodiscard]] const ops::QueueFamilyTransferPolicy &queueFamilyTransferPolicy() const noexcept;

    [[nodiscard]] static constexpr QueueRole presentSubmitRole() noexcept
    {
        return QueueRole::Compute;
    }

    [[nodiscard]] bool frameBoundaryEnabled() const noexcept;

    [[nodiscard]] bool hdrMetadataEnabled() const noexcept;

    [[nodiscard]] bool nsightGraphicsEnabled() const noexcept;

    [[nodiscard]] bool hasEnabledInstanceExtension(std::string_view extension) const;

    [[nodiscard]] bool hasEnabledDeviceExtension(std::string_view extension) const;

    void initialize(std::string const &_appName = {"DefaultApp"}, std::string const &_engineName = {"DefaultEngine"});

    [[nodiscard]] FrameBeginResult beginFrame(std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max());

    void submitFrameBatch(CommandBatch&& batch, QueueRole submitRole, bool signalForPresent, vk::PipelineStageFlags2 imageAvailableWaitStage);

    void submitFrameBatch(CommandBatch&& batch, QueueRole submitRole = QueueRole::Compute, bool signalForPresent = false);

    void submitFrame(CommandBatch&& batch, QueueRole submitRole = QueueRole::Compute);

    [[nodiscard]] bool canPresentCurrentFrame() const noexcept;

    [[nodiscard]] QueueRole submitRoleForPresent() const noexcept;

    [[nodiscard]] std::uint32_t frameSubmitCount() const noexcept;

    [[nodiscard]] PresentResult presentFrame();

    [[nodiscard]] PresentResult endFrame(CommandBatch&& batch, QueueRole submitRole = QueueRole::Compute);

    vk::raii::Instance makeInstance(std::uint32_t apiVersion = vk::ApiVersion14) const;

    vk::raii::Device makeDevice();

    void initializeCommandSystem();

    void waitIdle();

    void recreateSwapchain();

    [[nodiscard]] PipelineService &pipeline() noexcept;

    [[nodiscard]] const PipelineService &pipeline() const noexcept;

    [[nodiscard]] ShaderService &shaderCompiler() const;

    [[nodiscard]] ops::UploadReadbackContext &uploadReadback() noexcept;

    [[nodiscard]] const ops::UploadReadbackContext &uploadReadback() const noexcept;

    ~Device();

  protected:
    [[nodiscard]] VkQueue presentQueueRawForExternalTools() const noexcept;

    [[nodiscard]] VkImage activeSwapchainImageRawForExternalTools() const;

    [[nodiscard]] Vulkan14PropertySnapshot queryVulkan14PropertySnapshot() const;

    void setupInitialFlags();

    [[nodiscard]] std::uint32_t requiredQueueFamily(QueueFamilyKind kind) const;

    [[nodiscard]] std::uint32_t presentQueueFamilyIndex() const;

    void refreshPresentSemaphores();

    [[nodiscard]] const vk::raii::Semaphore &activePresentSemaphore() const;

    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    std::vector<std::string> deviceEnabledExtensions{
        vk::KHRSwapchainExtensionName,          vk::KHRDeferredHostOperationsExtensionName,      vk::EXTMeshShaderExtensionName,       vk::KHRAccelerationStructureExtensionName,
        vk::KHRRayTracingPipelineExtensionName, vk::KHRRayTracingMaintenance1ExtensionName,      vk::KHRPipelineLibraryExtensionName,  vk::KHRRayQueryExtensionName,
        vk::EXTOpacityMicromapExtensionName,    vk::EXTRayTracingInvocationReorderExtensionName, vk::NVCooperativeVectorExtensionName, vk::EXTExtendedDynamicState3ExtensionName,
        vk::EXTMemoryBudgetExtensionName,       vk::KHRMaintenance9ExtensionName,
    };
    RayTracingCapabilitySnapshot rtCapabilities_{};
    DescriptorIndexingCapabilitySnapshot descriptorIndexingCapabilities_{};
    BufferDeviceAddressCapabilitySnapshot bufferDeviceAddressCapabilities_{};
    Vulkan14CapabilitySnapshot vulkan14Capabilities_{};
    Vulkan14PropertySnapshot vulkan14Properties_{};
    ops::QueueFamilyTransferPolicy queueFamilyTransferPolicy_{};
    bool frameBoundaryEnabled_ = false;
    bool hdrMetadataEnabled_ = false;
    NsightGraphicsFrameHelper nsightGraphics_{};

    std::array<std::size_t, static_cast<std::size_t>(QueueFamilyKind::size)> queueFamilyDict{};
    SwapChainConfig swapChainConfig_{};
    std::uint32_t frameSubmitCount_ = 0;
    std::optional<QueueRole> frameFinalSubmitRole_{};
    std::optional<std::uint64_t> presentFrameBoundaryFrameID_{};
    std::vector<vk::raii::Semaphore> presentSemaphoresByImage_{};
};

void rhiTest();
} // namespace nr::rhi
