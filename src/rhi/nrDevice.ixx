export module nr.rhi:device;
import dependency.window;
import dependency.nsight;
import dependency.dlss;
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
import :dlss;
import std;

export namespace nr::rhi
{
class Device
{
  public:
    struct FrameBeginResult
    {
        std::uint32_t frameIndex = 0;
        double cpuWaitGpuMilliseconds = 0.0;
    };

    struct FrameAcquireResult
    {
        std::uint32_t swapchainImageIndex = 0;
        vk::Result swapchainResult = vk::Result::eSuccess;
        bool recreatedSwapchain = false;
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

    void initialize(std::string const &_appName, std::string const &_engineName,
                    std::filesystem::path pipelineBinaryRoot);

    [[nodiscard]] FrameBeginResult beginFrame();

    [[nodiscard]] FrameAcquireResult acquireFrameImage(
        std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max());

    void submitFrameBatch(CommandBatch &&batch, QueueRole submitRole, bool signalForPresent,
                          vk::PipelineStageFlags2 imageAvailableWaitStage);

    void submitFrameBatch(CommandBatch &&batch, QueueRole submitRole = QueueRole::Compute,
                          bool signalForPresent = false);

    [[nodiscard]] PresentResult presentFrame();

    vk::raii::Instance makeInstance(std::uint32_t apiVersion = vk::ApiVersion14) const;

    vk::raii::Device makeDevice();

    void initializeCommandSystem();

    void waitIdle();

    void recreateSwapchain();

    [[nodiscard]] std::uint64_t swapchainRecreationGeneration() const noexcept;

    [[nodiscard]] PipelineService &pipeline() noexcept;

    [[nodiscard]] const PipelineService &pipeline() const noexcept;

    [[nodiscard]] ShaderService &shaderCompiler() const;

    [[nodiscard]] ops::UploadReadbackContext &uploadReadback() noexcept;

    [[nodiscard]] const ops::UploadReadbackContext &uploadReadback() const noexcept;

    [[nodiscard]] std::shared_ptr<DlssContext> dlssContext();

    [[nodiscard]] std::unique_ptr<DlssRayReconstructionFeature> createDlssRayReconstructionFeature(
        const DlssRayReconstructionCreateDesc &desc);

    ~Device();

  protected:
    [[nodiscard]] VkQueue presentQueueRawForExternalTools() const noexcept;

    [[nodiscard]] VkImage activeSwapchainImageRawForExternalTools() const;

    void setupInitialFlags();

    [[nodiscard]] std::uint32_t requiredQueueFamily(QueueFamilyKind kind) const;

    [[nodiscard]] std::uint32_t presentQueueFamilyIndex() const;

    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    std::vector<std::string> requestedDeviceExtensions_{
        vk::KHRSwapchainExtensionName,
        vk::EXTSwapchainMaintenance1ExtensionName,
        vk::KHRDeferredHostOperationsExtensionName,
        vk::KHRAccelerationStructureExtensionName,
        vk::KHRRayTracingPipelineExtensionName,
        vk::EXTRayTracingInvocationReorderExtensionName,
        vk::KHRPipelineLibraryExtensionName,
        vk::KHRPipelineBinaryExtensionName,
        vk::EXTMemoryBudgetExtensionName,
        vk::KHRMaintenance8ExtensionName,
        vk::KHRMaintenance9ExtensionName,
        vk::EXTFullScreenExclusiveExtensionName,
    };
    std::vector<std::string> enabledDeviceExtensions_{};
    RayTracingCapabilitySnapshot rtCapabilities_{};
    ops::QueueFamilyTransferPolicy queueFamilyTransferPolicy_{};
    bool frameBoundaryEnabled_ = false;
    bool hdrMetadataEnabled_ = false;
    NsightGraphicsFrameHelper nsightGraphics_{};
    std::shared_ptr<DlssContext> dlssContext_{};

    std::array<std::size_t, static_cast<std::size_t>(QueueFamilyKind::size)> queueFamilyDict{};
    SwapChainConfig swapChainConfig_{};
    std::optional<std::uint64_t> presentFrameBoundaryFrameID_{};
    bool frameAcquireRequiresRecreate_ = false;
    std::uint64_t swapchainRecreationGeneration_ = 0;
};

} // namespace nr::rhi
