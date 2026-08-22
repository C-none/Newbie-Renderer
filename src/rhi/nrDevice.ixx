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
import :cooperativeVector;
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
    vk::raii::Instance instance;
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger;
    vk::raii::PhysicalDevice physicalDevice;
    vk::raii::Device device;

    MemoryAllocator memoryAllocator;
    ResourceFactory resourceFactory;
    ResourcePool resourcePool;

    QueueManager queueManager;
    FrameManager frameManager;

    PresentationContext presentationContext;
    PipelineService pipelineService;
    std::optional<ops::UploadReadbackContext> uploadReadbackContext_;

    [[nodiscard]] static Device create(std::string appName = "DefaultApp", std::string engineName = "DefaultEngine",
                                       std::filesystem::path pipelineBinaryRoot =
                                           std::filesystem::path{std::string{nr::psoCacheRoot}},
                                       bool debugShaderInstrumentationEnabled = true);

    /// Heap-allocating counterpart of create(), for owners that need a stable address.
    [[nodiscard]] static std::unique_ptr<Device> createUnique(
        std::string appName = "DefaultApp", std::string engineName = "DefaultEngine",
        std::filesystem::path pipelineBinaryRoot = std::filesystem::path{std::string{nr::psoCacheRoot}},
        bool debugShaderInstrumentationEnabled = true);

    Device(Device &) = delete;
    Device &operator=(Device &) = delete;

    [[nodiscard]] const RayTracingCapabilitySnapshot &rayTracingCapabilities() const noexcept;

    [[nodiscard]] const CooperativeVectorCapabilitySnapshot &cooperativeVectorCapabilities() const noexcept;

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

    [[nodiscard]] FrameBeginResult beginFrame();

    [[nodiscard]] FrameAcquireResult acquireFrameImage(
        std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max());

    void submitFrameBatch(CommandBatch &&batch, QueueRole submitRole, bool signalForPresent,
                          vk::PipelineStageFlags2 imageAvailableWaitStage);

    void submitFrameBatch(CommandBatch &&batch, QueueRole submitRole = QueueRole::Compute,
                          bool signalForPresent = false);

    [[nodiscard]] PresentResult presentFrame();

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
        const nr::dependency::dlss::RayReconstructionCreateDesc &desc);

    ~Device();

  protected:
    [[nodiscard]] VkQueue presentQueueRawForExternalTools() const noexcept;

    [[nodiscard]] VkImage activeSwapchainImageRawForExternalTools() const;

    // Declaration order matches construction order: the state below the Vulkan handles is
    // produced while building them, and members unwind in exact reverse at destruction.
    std::vector<std::string> instanceEnabledLayers;
    std::vector<std::string> instanceEnabledExtensions;
    std::vector<std::string> requestedDeviceExtensions_;
    std::vector<std::string> enabledDeviceExtensions_;
    RayTracingCapabilitySnapshot rtCapabilities_;
    CooperativeVectorCapabilitySnapshot cooperativeVectorCapabilities_;
    ops::QueueFamilyTransferPolicy queueFamilyTransferPolicy_;
    bool frameBoundaryEnabled_;
    bool hdrMetadataEnabled_;
    NsightGraphicsFrameHelper nsightGraphics_;
    std::shared_ptr<DlssContext> dlssContext_{};

    std::array<std::size_t, static_cast<std::size_t>(QueueFamilyKind::size)> queueFamilyDict;
    SwapChainConfig swapChainConfig_;
    std::optional<std::uint64_t> presentFrameBoundaryFrameID_{};
    bool frameAcquireRequiresRecreate_ = false;
    std::uint64_t swapchainRecreationGeneration_ = 0;

  private:
    struct Bootstrap;

    [[nodiscard]] static Bootstrap makeBootstrap(std::string appName, std::string engineName,
                                                 bool debugShaderInstrumentationEnabled);

    Device(Bootstrap &&bootstrap, std::filesystem::path pipelineBinaryRoot);
};

} // namespace nr::rhi
