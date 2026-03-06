module;
export module nr.rhi:pipeline;
import dependency;
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
		std::optional<vk::Format> depthAttachmentFormat;
		std::optional<vk::Format> stencilAttachmentFormat;
		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
		vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
		vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
		vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1;
		bool depthTestEnable = false;
		bool depthWriteEnable = false;
		vk::CompareOp depthCompareOp = vk::CompareOp::eLessOrEqual;
		std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
		std::vector<vk::DynamicState> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
		vk::PipelineCreateFlags flags = {};
		GraphicsPipelineMode mode = GraphicsPipelineMode::StandardGraphics;
};

struct ComputePipelineDesc
{
		vk::PipelineCreateFlags flags = {};
};

struct RayTracingShaderGroupDesc
{
		vk::RayTracingShaderGroupTypeKHR type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
		std::string generalEntryPoint;
		std::string closestHitEntryPoint;
		std::string anyHitEntryPoint;
		std::string intersectionEntryPoint;
};

struct RayTracingPipelineDesc
{
		uint32_t maxRayRecursionDepth = 1;
		std::vector<RayTracingShaderGroupDesc> groups;
		vk::PipelineCreateFlags flags = {};
};

struct PipelineRuntimeCapabilities
{
		bool dynamicRendering = false;
		bool extendedDynamicState = false;
		bool pipelineLibrary = false;
		bool pipelineRobustness = false;
		bool synchronization2 = false;
		bool descriptorIndexing = false;
		bool accelerationStructure = false;
		bool rayTracingPipeline = false;
};

class CursorPipelineLayout
{
	public:
		[[nodiscard]] static CursorPipelineLayout create(const vk::raii::Device &device, const ShaderDescriptorLayout &descriptorLayout)
		{
				CursorPipelineLayout layout;
				layout.device_ = std::cref(device);

				auto setLayouts = descriptorLayout.descriptorSets();
				layout.setLayouts_.reserve(setLayouts.size());

				std::vector<vk::DescriptorSetLayout> pipelineSetLayouts;
				pipelineSetLayouts.reserve(setLayouts.size());

				std::ranges::for_each(setLayouts, [&](const DescriptorSetLayoutInfo &setInfo) {
						auto bindings = descriptorLayout.makeVkSetLayoutBindings(setInfo.set);
						vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
						setLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
						setLayoutInfo.pBindings = bindings.data();

						layout.setLayouts_.push_back(DescriptorSetLayoutHandle{
								.set = setInfo.set,
								.layout = vk::raii::DescriptorSetLayout(device, setLayoutInfo),
						});
						pipelineSetLayouts.push_back(*layout.setLayouts_.back().layout);
				});

				vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
				pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(pipelineSetLayouts.size());
				pipelineLayoutInfo.pSetLayouts = pipelineSetLayouts.data();
				layout.pipelineLayout_ = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
				return layout;
		}

		[[nodiscard]] bool valid() const noexcept { return pipelineLayout_ != nullptr; }
		[[nodiscard]] vk::PipelineLayout raw() const noexcept { return valid() ? *pipelineLayout_ : vk::PipelineLayout{}; }

		[[nodiscard]] std::optional<vk::DescriptorSetLayout> descriptorSetLayout(uint32_t setIndex) const noexcept
		{
				auto it = std::ranges::find_if(setLayouts_, [setIndex](const DescriptorSetLayoutHandle &handle) {
						return handle.set == setIndex;
				});
				if (it == std::ranges::end(setLayouts_))
				{
						return std::nullopt;
				}
				return *it->layout;
		}

		[[nodiscard]] std::vector<uint32_t> setIndices() const
		{
				return setLayouts_ |
				       std::views::transform([](const DescriptorSetLayoutHandle &handle) { return handle.set; }) |
				       std::ranges::to<std::vector>();
		}

	private:
		struct DescriptorSetLayoutHandle
		{
				uint32_t set = 0;
				vk::raii::DescriptorSetLayout layout = {nullptr};
		};

		std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
		std::vector<DescriptorSetLayoutHandle> setLayouts_;
		vk::raii::PipelineLayout pipelineLayout_ = {nullptr};
};

class ShaderBindingSet
{
	public:
		[[nodiscard]] bool valid() const noexcept { return static_cast<bool>(set_); }
		[[nodiscard]] vk::DescriptorSet raw() const noexcept { return set_; }
		[[nodiscard]] uint32_t setIndex() const noexcept { return setIndex_; }

	private:
		friend class ShaderBindingPool;
		vk::DescriptorSet set_{};
		uint32_t setIndex_ = 0;
};

class ShaderBindingPool
{
	public:
		[[nodiscard]] static ShaderBindingPool create(
				const vk::raii::Device &device,
				const ShaderDescriptorLayout &descriptorLayout,
				uint32_t maxSets = 64,
				vk::DescriptorPoolCreateFlags flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
		{
				ShaderBindingPool pool;
				pool.device_ = std::cref(device);

				auto descriptorCounts = std::map<vk::DescriptorType, uint32_t>{};
				std::ranges::for_each(descriptorLayout.descriptorSets(), [&](const DescriptorSetLayoutInfo &setInfo) {
						std::ranges::for_each(setInfo.bindings, [&](const DescriptorBindingInfo &bindingInfo) {
								descriptorCounts[bindingInfo.descriptorType] += bindingInfo.descriptorCount * std::max(maxSets, 1u);
						});
				});

				auto poolSizes = descriptorCounts |
				                 std::views::transform([](const auto &pair) {
					                 return vk::DescriptorPoolSize{pair.first, pair.second};
				                 }) |
				                 std::ranges::to<std::vector>();

				if (poolSizes.empty())
				{
						poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, std::max(maxSets, 1u)});
				}

				vk::DescriptorPoolCreateInfo poolInfo{};
				poolInfo.flags = flags;
				poolInfo.maxSets = std::max(maxSets, 1u);
				poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
				poolInfo.pPoolSizes = poolSizes.data();
				pool.pool_ = vk::raii::DescriptorPool(device, poolInfo);
				return pool;
		}

		[[nodiscard]] ShaderBindingSet allocate(const CursorPipelineLayout &layout, uint32_t setIndex) const
		{
				nrAssert(device_.has_value(), "ShaderBindingPool::allocate requires an owning device reference.");

				ShaderBindingSet set;
				set.setIndex_ = setIndex;

				auto descriptorSetLayout = layout.descriptorSetLayout(setIndex);
				if (!descriptorSetLayout.has_value())
				{
						return set;
				}

				vk::DescriptorSetAllocateInfo allocateInfo{};
				allocateInfo.descriptorPool = *pool_;
				auto layoutHandle = *descriptorSetLayout;
				allocateInfo.descriptorSetCount = 1;
				allocateInfo.pSetLayouts = &layoutHandle;

				auto allocatedSets = device_->get().allocateDescriptorSets(allocateInfo);
				if (!allocatedSets.empty())
				{
						set.set_ = allocatedSets.front();
				}
				return set;
		}

		[[nodiscard]] std::vector<ShaderBindingSet> allocateAll(const CursorPipelineLayout &layout) const
		{
				return layout.setIndices() |
				       std::views::transform([&](uint32_t setIndex) { return allocate(layout, setIndex); }) |
				       std::ranges::to<std::vector>();
		}

	private:
		std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
		vk::raii::DescriptorPool pool_ = {nullptr};
};

class VkShaderProgram
{
	public:
		[[nodiscard]] static VkShaderProgram create(const vk::raii::Device &device, const SlangProgram &slangProgram)
		{
				nrAssert(slangProgram.valid(), "VkShaderProgram::create requires a valid SlangProgram.");
				auto const *entryPoint = slangProgram.entryPointBinary();
				nrAssert(entryPoint != nullptr, "VkShaderProgram::create requires one compiled entrypoint binary.");
				nrAssert(!entryPoint->spirv.empty(), std::format("Entry point '{}' has empty SPIR-V payload.", entryPoint->entryPointName));

				VkShaderProgram result;
				result.modules_.reserve(1);
				result.stageCreateInfos_.reserve(1);

				vk::ShaderModuleCreateInfo moduleInfo{};
				moduleInfo.codeSize = static_cast<size_t>(entryPoint->spirv.size() * sizeof(uint32_t));
				moduleInfo.pCode = entryPoint->spirv.data();
				result.modules_.emplace_back(device, moduleInfo);

				result.entryPointNames_.push_back(entryPoint->entryPointName.empty() ? "main" : entryPoint->entryPointName);
				result.stages_.push_back(entryPoint->stage);

				vk::PipelineShaderStageCreateInfo stageInfo{};
				stageInfo.stage = toVkShaderStage(entryPoint->stage);
				stageInfo.module = *result.modules_.back();
				stageInfo.pName = result.entryPointNames_.back().c_str();
				result.stageCreateInfos_.push_back(stageInfo);

				return result;
		}

		[[nodiscard]] bool valid() const noexcept { return !stageCreateInfos_.empty(); }
		[[nodiscard]] const vk::PipelineShaderStageCreateInfo &stageCreateInfo(uint32_t index) const noexcept { return stageCreateInfos_[index]; }
		[[nodiscard]] std::span<const SlangStage> stages() const noexcept { return stages_; }

	private:
		std::vector<vk::raii::ShaderModule> modules_;
		std::vector<std::string> entryPointNames_;
		std::vector<SlangStage> stages_;
		std::vector<vk::PipelineShaderStageCreateInfo> stageCreateInfos_;
};

class GraphicsPipeline
{
	public:
		[[nodiscard]] static GraphicsPipeline create(
				const vk::raii::Device &,
				const CursorPipelineLayout &,
				const VkShaderProgram &,
				const PipelineRuntimeCapabilities &runtimeCaps = {},
				const GraphicsPipelineDesc & = {})
		{
				if (runtimeCaps.dynamicRendering)
				{
					nrInfo<LogLevel::warning>("GraphicsPipeline::create is in cursor-first transition. Dynamic rendering capability is enabled but graphics pipeline assembly is still backend TODO.");
				}
				else
				{
					nrInfo<LogLevel::warning>("GraphicsPipeline::create is in cursor-first transition and currently returns an empty pipeline.");
				}
				return {};
		}

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
				const PipelineRuntimeCapabilities &,
				const ComputePipelineDesc &desc = {})
		{
				nrAssert(layout.valid(), "ComputePipeline::create requires a valid pipeline layout.");
				nrAssert(shaderProgram.valid(), "ComputePipeline::create requires a valid shader program.");

				auto indices = std::views::iota(uint32_t{0}, static_cast<uint32_t>(shaderProgram.stages().size()));
				auto it = std::ranges::find_if(indices, [&](uint32_t index) { return shaderProgram.stages()[index] == SLANG_STAGE_COMPUTE; });
				nrAssert(it != std::ranges::end(indices), "ComputePipeline::create requires at least one compute entry point.");
				auto stageIndex = *it;

				vk::ComputePipelineCreateInfo createInfo{};
				createInfo.flags = desc.flags;
				createInfo.stage = shaderProgram.stageCreateInfo(stageIndex);
				createInfo.layout = layout.raw();

				ComputePipeline pipeline;
				pipeline.pipeline_ = vk::raii::Pipeline(device, nullptr, createInfo);
				return pipeline;
		}

	private:
		vk::raii::Pipeline pipeline_ = {nullptr};
};

class RayTracingPipeline
{
	public:
		[[nodiscard]] static RayTracingPipeline create(
				const vk::raii::Device &,
				const CursorPipelineLayout &,
				const VkShaderProgram &,
				const PipelineRuntimeCapabilities &runtimeCaps = {},
				const RayTracingPipelineDesc & = {})
		{
				if (runtimeCaps.pipelineLibrary)
				{
					nrInfo<LogLevel::warning>("RayTracingPipeline::create is in cursor-first transition. Pipeline-library capability is enabled for future modular RT pipeline assembly.");
				}
				else
				{
					nrInfo<LogLevel::warning>("RayTracingPipeline::create is in cursor-first transition and currently returns an empty pipeline.");
				}
				return {};
		}

	private:
		vk::raii::Pipeline pipeline_ = {nullptr};
};

template <typename TPipeline>
struct PipelineState
{
		CursorPipelineLayout layout;
		ShaderDescriptorLayout descriptorLayout;
		ShaderBindingPool bindingPool;
		PipelineRuntimeCapabilities runtimeCaps;
		TPipeline pipeline;
};

} // namespace nr::rhi