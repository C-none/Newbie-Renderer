export module nr.rhi:pipeline;
import dependency.slang;
import dependency.vulkan;
import :type;
import nr.utils;
import :descriptor;
import :slang;
import std;

export namespace nr::rhi
{
// Cursor-first migration note:
// This partition intentionally avoids exporting descriptor-reflection data models.
// Pipeline setup consumes SlangProgram directly, while runtime parameter binding is
// expected to happen through shader objects and ShaderCursor.

struct GraphicsPipelineDesc
{
    std::vector<vk::Format> colorAttachmentFormats;
    std::optional<vk::Format> depthAttachmentFormat{};
    std::optional<vk::Format> stencilAttachmentFormat{};
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLessOrEqual;
    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
    std::vector<vk::VertexInputBindingDescription> vertexBindings;
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes;
    DescriptorBindingPolicy descriptorBindingPolicy{};
};

struct ComputePipelineDesc
{
    DescriptorBindingPolicy descriptorBindingPolicy{};
};

struct RayTracingShaderGroupDesc
{
    std::string name{};
    vk::RayTracingShaderGroupTypeKHR type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    std::string generalEntryPoint{};
    std::string closestHitEntryPoint{};
    std::string anyHitEntryPoint{};
    std::string intersectionEntryPoint{};
};

struct RayTracingPipelineDesc
{
    std::uint32_t maxRayRecursionDepth = 1;
    DescriptorBindingPolicy descriptorBindingPolicy{};
    vk::PipelineCreateFlags flags = {};
};

struct RayTracingPipelineStageSelection
{
    std::reference_wrapper<const SlangProgram> program;
    std::string logicalEntryPointName;
};

struct RayTracingProgramAssemblyDesc
{
    std::vector<RayTracingPipelineStageSelection> stages;
    std::vector<RayTracingShaderGroupDesc> groups;
};

struct PipelineCacheConfig
{
    bool enabled = true;
    std::filesystem::path directory{};
    std::string fileName = "vulkan-pipeline-cache.bin";
    bool saveOnIdle = true;

    [[nodiscard]] bool persistent() const noexcept
    {
        return enabled && !directory.empty() && !fileName.empty();
    }
};

[[nodiscard]] std::optional<std::string> validateRayTracingPipelineDesc(const RayTracingPipelineDesc &desc);

[[nodiscard]] std::optional<std::string> validateRayTracingProgramAssemblyDesc(
    const RayTracingProgramAssemblyDesc &desc);

struct MeshRasterState
{
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
    vk::Bool32 depthTestEnable = vk::False;
    vk::Bool32 depthWriteEnable = vk::False;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLessOrEqual;
};

namespace mesh
{
void applyRasterState(const vk::raii::CommandBuffer &commandBuffer, const MeshRasterState &state);

} // namespace mesh

class CursorPipelineLayout
{
  public:
    CursorPipelineLayout() = default;
    CursorPipelineLayout(const CursorPipelineLayout &) = delete;
    CursorPipelineLayout &operator=(const CursorPipelineLayout &) = delete;
    CursorPipelineLayout(CursorPipelineLayout &&) noexcept = default;
    CursorPipelineLayout &operator=(CursorPipelineLayout &&) = delete;

    [[nodiscard]] static CursorPipelineLayout create(
        const vk::raii::Device &device, const ShaderDescriptorLayout &descriptorLayout,
        std::uint32_t maxBoundDescriptorSets,
        std::span<const SlangImmutableSamplerBinding> immutableSamplers = {});

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] vk::PipelineLayout raw() const noexcept;

    [[nodiscard]] std::optional<vk::DescriptorSetLayout> descriptorSetLayout(std::uint32_t setIndex) const noexcept;

    [[nodiscard]] std::vector<std::uint32_t> setIndices() const;

    void bindDescriptorSets(const vk::raii::CommandBuffer &commandBuffer, vk::PipelineBindPoint bindPoint,
                            std::span<const ShaderBindingSet> sets,
                            std::span<const std::uint32_t> dynamicOffsets = {}) const;

    void pushConstants(const vk::raii::CommandBuffer &commandBuffer, vk::ShaderStageFlags stageFlags,
                       std::uint32_t offset, std::span<const std::uint8_t> bytes) const;

  private:
    struct DescriptorSetLayoutHandle
    {
        std::uint32_t set = 0;
        vk::raii::DescriptorSetLayout layout = {nullptr};
        bool isPlaceholder = false;
    };

    // Member order is a lifetime contract: layouts must die before their immutable samplers.
    std::vector<SlangSampler> immutableSamplers_;
    std::vector<DescriptorSetLayoutHandle> setLayouts_;
    vk::raii::PipelineLayout pipelineLayout_ = {nullptr};
};

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(
    const CursorPipelineLayout &layout, ShaderBindingPool &pool,
    const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet);

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(const CursorPipelineLayout &layout, ShaderBindingPool &pool);

void updateResourcesForBindingSnapshot(ShaderBindingPool &pool, std::span<const ShaderBindingSet> sets,
                                       DescriptorWriteCache &descriptorWriteCache,
                                       const ShaderBindingSnapshot &snapshot,
                                       LogicalDescriptorResolver logicalResolver);

void bindPreparedResourcesToCommandBuffer(const vk::raii::CommandBuffer &commandBuffer, vk::PipelineBindPoint bindPoint,
                                          const CursorPipelineLayout &layout, std::span<const ShaderBindingSet> sets);

void pushConstantsToCommandBuffer(const vk::raii::CommandBuffer &commandBuffer, const CursorPipelineLayout &layout,
                                  const ShaderBindingSnapshot &snapshot);

class VkShaderProgram
{
  public:
    [[nodiscard]] static VkShaderProgram create(const vk::raii::Device &device, std::span<const SlangProgram> programs);
    [[nodiscard]] static VkShaderProgram create(const vk::raii::Device &device,
                                                std::span<const RayTracingPipelineStageSelection> selectedEntryPoints);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const vk::PipelineShaderStageCreateInfo &stageCreateInfo(std::uint32_t index) const noexcept;
    [[nodiscard]] std::span<const SlangStage> stages() const noexcept;
    [[nodiscard]] std::span<const std::string> logicalEntryPointNames() const noexcept;

  private:
    static void appendStage(VkShaderProgram &program, const vk::raii::Device &device,
                            const SlangEntryPointData &entryPoint, std::string logicalEntryPointName);

    std::vector<vk::raii::ShaderModule> modules_;
    std::vector<std::string> shaderEntryPointNames_;
    std::vector<std::string> logicalEntryPointNames_;
    std::vector<SlangStage> stages_;
    std::vector<vk::PipelineShaderStageCreateInfo> stageCreateInfos_;
};

class GraphicsPipeline
{
  public:
    [[nodiscard]] static GraphicsPipeline create(const vk::raii::Device &device, const CursorPipelineLayout &layout,
                                                 const VkShaderProgram &shaderProgram,
                                                 const GraphicsPipelineDesc &desc = {},
                                                 const vk::raii::PipelineCache *pipelineCache = nullptr);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] vk::Pipeline raw() const noexcept;

  private:
    vk::raii::Pipeline pipeline_ = {nullptr};
};

class ComputePipeline
{
  public:
    [[nodiscard]] static ComputePipeline create(const vk::raii::Device &device, const CursorPipelineLayout &layout,
                                                const VkShaderProgram &shaderProgram,
                                                const vk::raii::PipelineCache *pipelineCache = nullptr);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] vk::Pipeline raw() const noexcept;

  private:
    vk::raii::Pipeline pipeline_ = {nullptr};
};

struct RayTracingPipelineIdentity
{
    std::uint64_t value = 0;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value != 0;
    }

    auto operator<=>(const RayTracingPipelineIdentity &) const = default;
};

class RayTracingPipeline
{
  public:
    [[nodiscard]] static RayTracingPipeline create(const vk::raii::Device &device, const CursorPipelineLayout &layout,
                                                   const VkShaderProgram &shaderProgram,
                                                   threading::StaticThreadPool &deferredHostPool,
                                                   const RayTracingCapabilitySnapshot &capabilities,
                                                   const RayTracingPipelineDesc &desc = {},
                                                   std::span<const RayTracingShaderGroupDesc> groups = {},
                                                   const vk::raii::PipelineCache *pipelineCache = nullptr);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] vk::Pipeline raw() const noexcept;
    [[nodiscard]] RayTracingPipelineIdentity identity() const noexcept;
    [[nodiscard]] const RayTracingCapabilitySnapshot &capabilities() const noexcept;
    [[nodiscard]] std::uint32_t shaderGroupCount() const noexcept;
    [[nodiscard]] std::uint32_t shaderGroupIndex(std::string_view name) const;
    [[nodiscard]] std::vector<std::uint8_t> shaderGroupHandles(std::uint32_t firstGroup,
                                                               std::uint32_t groupCount) const;

  private:
    RayTracingPipelineIdentity identity_{};
    RayTracingCapabilitySnapshot capabilities_{};
    std::uint32_t shaderGroupCount_ = 0;
    std::map<std::string, std::uint32_t> shaderGroupIndices_{};
    vk::raii::Pipeline pipeline_ = {nullptr};
};

template <typename TPipeline> struct PipelineState
{
    PipelineState(CursorPipelineLayout pipelineLayout, ShaderDescriptorLayout shaderDescriptorLayout,
                  ShaderBindingPool shaderBindingPool, TPipeline pipelineObject)
        : layout(std::move(pipelineLayout)), descriptorLayout(std::move(shaderDescriptorLayout)),
          bindingPool(std::move(shaderBindingPool)), pipeline(std::move(pipelineObject))
    {
    }

    PipelineState(const PipelineState &) = delete;
    PipelineState &operator=(const PipelineState &) = delete;
    PipelineState(PipelineState &&) noexcept = default;
    PipelineState &operator=(PipelineState &&) = delete;

    // Field order is a teardown contract: pipeline, pool, descriptor layout, then Vulkan layout.
    CursorPipelineLayout layout;
    ShaderDescriptorLayout descriptorLayout;
    ShaderBindingPool bindingPool;
    TPipeline pipeline;
    std::optional<GraphicsPipelineDesc> graphicsDesc{};
};

template <typename TPipeline> using PipelineBuild = std::future<PipelineState<TPipeline>>;

/**
 * @brief Internal pipeline construction service bound to one logical device lifetime.
 *
 * Lifetime contract: bind once after logical device creation, then use for explicit
 * pipeline/binding/sampler creation calls from Device::pipeline().
 */
class PipelineService
{
  public:
    void bindDevice(
        const vk::raii::Device &device,
        std::uint32_t maxBoundDescriptorSets,
        const RayTracingCapabilitySnapshot &rtCapabilities,
        PipelineCacheConfig cacheConfig = {});

    [[nodiscard]] bool savePipelineCache() const;

    void waitForBuilds() const;

    [[nodiscard]] SlangSampler createSampler(SlangSamplerDesc desc = {}, std::string_view debugName = {}) const;

  private:
    struct PipelineLayoutBundle
    {
        ShaderDescriptorLayout descriptorLayout;
        CursorPipelineLayout layout;
    };

    [[nodiscard]] PipelineLayoutBundle createPipelineLayoutBundle(
        const SlangProgram &slangProgram, const DescriptorBindingPolicy &descriptorBindingPolicy,
        std::span<const SlangImmutableSamplerBinding> immutableSamplers) const;

    [[nodiscard]] const vk::raii::PipelineCache *pipelineCacheOrNull() const noexcept;

    template <typename TPipeline>
    [[nodiscard]] PipelineState<TPipeline> makePipelineState(PipelineLayoutBundle bundle,
                                                             std::uint32_t descriptorMaxSets, TPipeline pipeline) const
    {
        nrAssert(device_.has_value(), "PipelineService pipeline construction requires a bound logical device.");
        auto bindingPool = ShaderBindingPool::create(device_->get(), bundle.descriptorLayout, descriptorMaxSets);
        return PipelineState<TPipeline>{std::move(bundle.layout), std::move(bundle.descriptorLayout),
                                        std::move(bindingPool), std::move(pipeline)};
    }

  public:
    [[nodiscard]] PipelineBuild<GraphicsPipeline> createGraphicsPipeline(
        std::span<const SlangProgram> programs, const GraphicsPipelineDesc &desc = {},
        std::uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {},
        std::string debugName = {}) const;

    [[nodiscard]] PipelineBuild<ComputePipeline> createComputePipeline(
        const SlangProgram &slangProgram, const ComputePipelineDesc &desc = {}, std::uint32_t descriptorMaxSets = 64,
        std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}, std::string debugName = {}) const;

    [[nodiscard]] PipelineBuild<RayTracingPipeline> createRayTracingPipeline(
        const SlangProgram &reflectionProgram, const RayTracingProgramAssemblyDesc &assembly,
        const RayTracingPipelineDesc &desc = {}, std::uint32_t descriptorMaxSets = 64,
        std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}, std::string debugName = {}) const;

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    std::uint32_t maxBoundDescriptorSets_ = 0;
    RayTracingCapabilitySnapshot rtCapabilities_{};
    PipelineCacheConfig cacheConfig_{};
    vk::raii::PipelineCache pipelineCache_ = {nullptr};
    mutable threading::StaticThreadPool rayTracingDeferredHostPool_{};
    mutable threading::StaticThreadPool pipelineBuildPool_{};
};

} // namespace nr::rhi
