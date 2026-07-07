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
		std::vector<std::string> entryPointNames;
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
		std::vector<vk::DynamicState> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
		DescriptorBindingPolicy descriptorBindingPolicy{};
		vk::PipelineCreateFlags flags = {};
		GraphicsPipelineMode mode = GraphicsPipelineMode::StandardGraphics;
};

struct ComputePipelineDesc
{
		std::string entryPointName;
		DescriptorBindingPolicy descriptorBindingPolicy{};
		vk::PipelineCreateFlags flags = {};
};

struct RayTracingShaderGroupDesc
{
		vk::RayTracingShaderGroupTypeKHR type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
		std::string generalEntryPoint;
		std::string closestHitEntryPoint;
		std::string anyHitEntryPoint;
		std::string intersectionEntryPoint;
		std::vector<std::uint8_t> captureReplayHandle;
};

struct RayTracingPipelineLibraryInterfaceDesc
{
		std::uint32_t maxPipelineRayPayloadSize = 0;
		std::uint32_t maxPipelineRayHitAttributeSize = 0;
};

struct RayTracingPipelineDesc
{
		std::vector<std::string> entryPointNames;
		std::uint32_t maxRayRecursionDepth = 1;
		std::vector<RayTracingShaderGroupDesc> groups;
		DescriptorBindingPolicy descriptorBindingPolicy{};
		vk::PipelineCreateFlags flags = {};
		bool createAsLibrary = false;
		std::vector<vk::Pipeline> linkedLibraries;
		std::optional<RayTracingPipelineLibraryInterfaceDesc> libraryInterface{};
		bool dynamicPipelineStackSize = false;
		std::optional<std::reference_wrapper<const vk::raii::DeferredOperationKHR>> deferredOperation{};
};

struct RayTracingPipelineStageSelection
{
		std::reference_wrapper<const SlangProgram> program;
		std::string entryPointName;
		std::string logicalEntryPointName;
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

namespace detail
{
[[nodiscard]] constexpr bool isStageInSet(SlangStage stage, std::initializer_list<SlangStage> stages)
{
	return std::ranges::any_of(stages, [stage](SlangStage s) { return s == stage; });
}

[[nodiscard]] bool isGraphicsStage(SlangStage stage);

[[nodiscard]] bool isRayTracingStage(SlangStage stage);
}

struct MeshRasterState
{
	vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
	vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
	vk::Bool32 depthTestEnable = vk::False;
	vk::Bool32 depthWriteEnable = vk::False;
	vk::CompareOp depthCompareOp = vk::CompareOp::eLessOrEqual;
	vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
	vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1;
};

namespace mesh
{
void applyRasterState(const vk::raii::CommandBuffer &commandBuffer, const MeshRasterState &state);

void drawTasks(const vk::raii::CommandBuffer &commandBuffer, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ);
} // namespace mesh

class CursorPipelineLayout
{
	public:
		[[nodiscard]] static CursorPipelineLayout create(
				const vk::raii::Device &device,
				const ShaderDescriptorLayout &descriptorLayout,
				std::span<const SlangImmutableSamplerBinding> immutableSamplers = {});

		[[nodiscard]] bool valid() const noexcept;
		[[nodiscard]] vk::PipelineLayout raw() const noexcept;

		[[nodiscard]] std::optional<vk::DescriptorSetLayout> descriptorSetLayout(std::uint32_t setIndex) const noexcept;

		[[nodiscard]] std::vector<std::uint32_t> setIndices() const;

		void bindDescriptorSet(
				const vk::raii::CommandBuffer &commandBuffer,
				vk::PipelineBindPoint bindPoint,
				const ShaderBindingSet &set,
				std::span<const std::uint32_t> dynamicOffsets = {}) const;

		void bindDescriptorSets(
				const vk::raii::CommandBuffer &commandBuffer,
				vk::PipelineBindPoint bindPoint,
				std::span<const ShaderBindingSet> sets,
				std::span<const std::uint32_t> dynamicOffsets = {}) const;

		void pushConstants(
				const vk::raii::CommandBuffer &commandBuffer,
				vk::ShaderStageFlags stageFlags,
				std::uint32_t offset,
				std::span<const std::uint8_t> bytes) const;

		void pushConstants(
				const vk::raii::CommandBuffer &commandBuffer,
				const ShaderCursor &cursor,
				std::span<const std::uint8_t> bytes) const;

	private:
		struct DescriptorSetLayoutHandle
		{
				std::uint32_t set = 0;
				vk::raii::DescriptorSetLayout layout = {nullptr};
				bool isPlaceholder = false;
		};

		struct ImmutableSamplerBindingState
		{
				std::uint32_t set = 0;
				std::uint32_t binding = 0;
				std::vector<SlangSampler> samplers;
				std::vector<vk::Sampler> rawSamplers;
				bool isApplied = false;
		};

		std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
		std::vector<DescriptorSetLayoutHandle> setLayouts_;
		std::vector<ImmutableSamplerBindingState> immutableSamplerBindings_;
		vk::raii::PipelineLayout pipelineLayout_ = {nullptr};
};

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(
	const CursorPipelineLayout &layout,
	ShaderBindingPool &pool,
	const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet);

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(const CursorPipelineLayout &layout, ShaderBindingPool &pool);

void updateResourcesForBindingSnapshot(
	ShaderBindingPool &pool,
	std::span<const ShaderBindingSet> sets,
	DescriptorWriteCache &descriptorWriteCache,
	const ShaderBindingSnapshot &snapshot,
	LogicalDescriptorResolver logicalResolver);

void bindPreparedResourcesToCommandBuffer(
	const vk::raii::CommandBuffer &commandBuffer,
	vk::PipelineBindPoint bindPoint,
	const CursorPipelineLayout &layout,
	std::span<const ShaderBindingSet> sets);

void pushConstantsToCommandBuffer(
	const vk::raii::CommandBuffer &commandBuffer,
	const CursorPipelineLayout &layout,
	const ShaderBindingSnapshot &snapshot);

class VkShaderProgram
{
	public:
		[[nodiscard]] static VkShaderProgram create(const vk::raii::Device &device, std::span<const SlangEntryPointData *const> selectedEntryPoints);
		[[nodiscard]] static VkShaderProgram create(const vk::raii::Device &device, std::span<const RayTracingPipelineStageSelection> selectedEntryPoints);

		[[nodiscard]] bool valid() const noexcept;
		[[nodiscard]] const vk::PipelineShaderStageCreateInfo &stageCreateInfo(std::uint32_t index) const noexcept;
		[[nodiscard]] std::span<const SlangStage> stages() const noexcept;
		[[nodiscard]] std::span<const std::string> entryPointNames() const noexcept;

	private:
		std::vector<vk::raii::ShaderModule> modules_;
		std::vector<std::string> shaderEntryPointNames_;
		std::vector<std::string> entryPointNames_;
		std::vector<SlangStage> stages_;
		std::vector<vk::PipelineShaderStageCreateInfo> stageCreateInfos_;
};

class GraphicsPipeline
{
	public:
		[[nodiscard]] static GraphicsPipeline create(
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
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
		[[nodiscard]] static ComputePipeline create(
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
				const VkShaderProgram &shaderProgram,
				const ComputePipelineDesc &desc = {},
				const vk::raii::PipelineCache *pipelineCache = nullptr);

		[[nodiscard]] bool valid() const noexcept;
		[[nodiscard]] vk::Pipeline raw() const noexcept;

	private:
		vk::raii::Pipeline pipeline_ = {nullptr};
};

class RayTracingPipeline
{
	public:
		[[nodiscard]] static RayTracingPipeline create(
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
				const VkShaderProgram &shaderProgram,
				const RayTracingPipelineDesc &desc = {},
				const vk::raii::PipelineCache *pipelineCache = nullptr);

		[[nodiscard]] bool valid() const noexcept;
		[[nodiscard]] vk::Pipeline raw() const noexcept;
		[[nodiscard]] std::uint32_t shaderGroupCount() const noexcept;
		[[nodiscard]] bool dynamicPipelineStackSize() const noexcept;

		[[nodiscard]] std::vector<std::uint8_t> shaderGroupHandles(std::uint32_t firstGroup, std::uint32_t groupCount, std::uint32_t handleSize) const;

		[[nodiscard]] std::vector<std::uint8_t> captureReplayShaderGroupHandles(std::uint32_t firstGroup, std::uint32_t groupCount, std::uint32_t captureReplayHandleSize) const;

		[[nodiscard]] vk::DeviceSize shaderGroupStackSize(std::uint32_t group, vk::ShaderGroupShaderKHR groupShader) const;

	private:
		std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
		std::uint32_t shaderGroupCount_ = 0;
		bool dynamicPipelineStackSize_ = false;
		vk::raii::Pipeline pipeline_ = {nullptr};
};

/**
 * @brief Set a VK_EXT_debug_utils name on a VkPipeline for profiler/debugger labeling.
 *
 * No-op when GPU debug names are disabled. Name source is typically a node's describe().name.
 */
void setPipelineDebugName(const vk::raii::Device &device, vk::Pipeline pipeline, std::string_view name);

template <typename TPipeline>
struct PipelineState
{
		SlangProgram reflectionProgram;
		CursorPipelineLayout layout;
		ShaderDescriptorLayout descriptorLayout;
		ShaderBindingPool bindingPool;
		TPipeline pipeline;
		std::optional<GraphicsPipelineDesc> graphicsDesc{};
};

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
		std::optional<std::reference_wrapper<const RayTracingCapabilitySnapshot>> rtCapabilities = std::nullopt,
		PipelineCacheConfig cacheConfig = {});

	[[nodiscard]] bool savePipelineCache() const;

	[[nodiscard]] ShaderBindingPool createBindingPool(const ShaderDescriptorLayout &descriptorLayout, ShaderBindingPoolConfig config = {}) const;

	[[nodiscard]] ShaderBindingSet allocateBindingSet(const CursorPipelineLayout &layout, ShaderBindingPool &bindingPool, std::uint32_t setIndex, std::optional<std::uint32_t> variableDescriptorCount = std::nullopt) const;

	[[nodiscard]] std::vector<ShaderBindingSet> allocateBindingSets(const CursorPipelineLayout &layout, ShaderBindingPool &bindingPool) const;

	[[nodiscard]] SlangSampler createSampler(SlangSamplerDesc desc = {}, std::string_view debugName = {}) const;

  private:
	struct PipelineLayoutBundle
	{
		ShaderDescriptorLayout descriptorLayout;
		CursorPipelineLayout layout;
	};

	[[nodiscard]] PipelineLayoutBundle createPipelineLayoutBundle(
		const SlangProgram &slangProgram,
		const DescriptorBindingPolicy &descriptorBindingPolicy,
		std::span<const SlangImmutableSamplerBinding> immutableSamplers) const;

	[[nodiscard]] const vk::raii::PipelineCache *pipelineCacheOrNull() const noexcept;

	template <typename TPipeline>
	[[nodiscard]] PipelineState<TPipeline> makePipelineState(
		const SlangProgram &slangProgram,
		PipelineLayoutBundle bundle,
		std::uint32_t descriptorMaxSets,
		TPipeline pipeline) const
	{
		auto bindingPool = createBindingPool(bundle.descriptorLayout, ShaderBindingPoolConfig{.maxSets = descriptorMaxSets});
		return PipelineState<TPipeline>{
			.reflectionProgram = slangProgram,
			.layout = std::move(bundle.layout),
			.descriptorLayout = std::move(bundle.descriptorLayout),
			.bindingPool = std::move(bindingPool),
			.pipeline = std::move(pipeline),
		};
	}

  public:
	[[nodiscard]] PipelineState<GraphicsPipeline> createGraphicsPipeline(const SlangProgram &slangProgram, const GraphicsPipelineDesc &desc = {}, std::uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const;

	[[nodiscard]] PipelineState<ComputePipeline> createComputePipeline(const SlangProgram &slangProgram, const ComputePipelineDesc &desc = {}, std::uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const;

	[[nodiscard]] PipelineState<RayTracingPipeline> createRayTracingPipeline(const SlangProgram &slangProgram, const RayTracingPipelineDesc &desc = {}, std::uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const;

	[[nodiscard]] PipelineState<RayTracingPipeline> createRayTracingPipeline(
		const SlangProgram &reflectionProgram,
		std::span<const RayTracingPipelineStageSelection> selectedEntryPoints,
		const RayTracingPipelineDesc &desc = {},
		std::uint32_t descriptorMaxSets = 64,
		std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const;

 private:
	std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
	std::optional<RayTracingCapabilitySnapshot> rtCapabilities_{};
	PipelineCacheConfig cacheConfig_{};
	vk::raii::PipelineCache pipelineCache_ = {nullptr};
};

} // namespace nr::rhi
