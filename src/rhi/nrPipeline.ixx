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
		std::vector<std::string> entryPointNames;
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
		std::string entryPointName;
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

struct RayTracingPipelineLibraryInterfaceDesc
{
		uint32_t maxPipelineRayPayloadSize = 0;
		uint32_t maxPipelineRayHitAttributeSize = 0;
};

struct RayTracingPipelineDesc
{
		std::vector<std::string> entryPointNames;
		uint32_t maxRayRecursionDepth = 1;
		std::vector<RayTracingShaderGroupDesc> groups;
		vk::PipelineCreateFlags flags = {};
		bool createAsLibrary = false;
		std::vector<vk::Pipeline> linkedLibraries;
		std::optional<RayTracingPipelineLibraryInterfaceDesc> libraryInterface;
};

[[nodiscard]] inline std::optional<std::string> validateRayTracingPipelineDesc(const RayTracingPipelineDesc &desc)
{
	const auto createAsLibrary = desc.createAsLibrary || ((desc.flags & vk::PipelineCreateFlags{VK_PIPELINE_CREATE_LIBRARY_BIT_KHR}) != vk::PipelineCreateFlags{});
	const auto usesLinkedLibraries = !desc.linkedLibraries.empty();
	const auto hasLibraryInterface = desc.libraryInterface.has_value();

	if ((createAsLibrary || usesLinkedLibraries) && !hasLibraryInterface)
	{
		return std::string{"RayTracingPipelineDesc library flow requires libraryInterface when creating/linking libraries."};
	}

	if (std::ranges::any_of(desc.linkedLibraries, [](vk::Pipeline pipelineHandle) { return pipelineHandle == vk::Pipeline{}; }))
	{
		return std::string{"RayTracingPipelineDesc::linkedLibraries contains VK_NULL_HANDLE."};
	}

	return std::nullopt;
}

namespace detail
{
[[nodiscard]] constexpr bool isStageInSet(SlangStage stage, std::initializer_list<SlangStage> stages)
{
	return std::ranges::any_of(stages, [stage](SlangStage s) { return s == stage; });
}

[[nodiscard]] bool isGraphicsStage(SlangStage stage)
{
	return isStageInSet(stage, {SLANG_STAGE_VERTEX, SLANG_STAGE_FRAGMENT, SLANG_STAGE_GEOMETRY,
	                             SLANG_STAGE_HULL, SLANG_STAGE_DOMAIN, SLANG_STAGE_AMPLIFICATION, SLANG_STAGE_MESH});
}

[[nodiscard]] bool isRayTracingStage(SlangStage stage)
{
	return isStageInSet(stage, {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CLOSEST_HIT,
	                             SLANG_STAGE_ANY_HIT, SLANG_STAGE_INTERSECTION, SLANG_STAGE_CALLABLE});
}
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
inline void applyRasterState(const vk::raii::CommandBuffer &commandBuffer, const MeshRasterState &state)
{
	nrAssert(*commandBuffer != nullptr, "mesh::applyRasterState requires a valid command buffer.");
	commandBuffer.setCullMode(state.cullMode);
	commandBuffer.setFrontFace(state.frontFace);
	commandBuffer.setDepthTestEnable(state.depthTestEnable);
	commandBuffer.setDepthWriteEnable(state.depthWriteEnable);
	commandBuffer.setDepthCompareOp(state.depthCompareOp);
	commandBuffer.setPolygonModeEXT(state.polygonMode);
	commandBuffer.setRasterizationSamplesEXT(state.rasterizationSamples);
}

inline void drawTasks(const vk::raii::CommandBuffer &commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	nrAssert(*commandBuffer != nullptr, "mesh::drawTasks requires a valid command buffer.");
	nrAssert(groupCountX > 0 && groupCountY > 0 && groupCountZ > 0, "mesh::drawTasks requires non-zero dispatch group counts.");
	commandBuffer.drawMeshTasksEXT(groupCountX, groupCountY, groupCountZ);
}
} // namespace mesh

class CursorPipelineLayout
{
	public:
		[[nodiscard]] static CursorPipelineLayout create(
				const vk::raii::Device &device,
				const ShaderDescriptorLayout &descriptorLayout,
				std::span<const SlangImmutableSamplerBinding> immutableSamplers = {})
		{
				CursorPipelineLayout layout;
				layout.device_ = std::cref(device);

				auto setLayouts = descriptorLayout.descriptorSets();
				layout.immutableSamplerBindings_.reserve(immutableSamplers.size());

				auto findDescriptorBinding = [setLayouts](uint32_t setIndex, uint32_t bindingIndex) -> const DescriptorBindingInfo * {
					auto setIt = std::ranges::find_if(setLayouts, [setIndex](const DescriptorSetLayoutInfo &setInfo) {
						return setInfo.set == setIndex;
					});
					if (setIt == std::ranges::end(setLayouts))
					{
						return nullptr;
					}

					auto bindingIt = std::ranges::find_if(setIt->bindings, [bindingIndex](const DescriptorBindingInfo &bindingInfo) {
						return bindingInfo.binding == bindingIndex;
					});
					if (bindingIt == std::ranges::end(setIt->bindings))
					{
						return nullptr;
					}

					return &(*bindingIt);
				};

				std::ranges::for_each(immutableSamplers, [&](const SlangImmutableSamplerBinding &immutableSamplerBinding) {
					nrAssert(
						std::ranges::none_of(layout.immutableSamplerBindings_, [&](const ImmutableSamplerBindingState &state) {
							return state.set == immutableSamplerBinding.set && state.binding == immutableSamplerBinding.binding;
						}),
						std::format(
							"CursorPipelineLayout::create duplicate immutable sampler binding at set={}, binding={}",
							immutableSamplerBinding.set,
							immutableSamplerBinding.binding));

					nrAssert(
						immutableSamplerBinding.descriptorCount > 0,
						std::format(
							"CursorPipelineLayout::create immutable sampler binding must have descriptorCount > 0 at set={}, binding={}",
							immutableSamplerBinding.set,
							immutableSamplerBinding.binding));

					auto const *bindingInfo = findDescriptorBinding(immutableSamplerBinding.set, immutableSamplerBinding.binding);
					nrAssert(
						bindingInfo != nullptr,
						std::format(
							"CursorPipelineLayout::create immutable sampler target not found at set={}, binding={}",
							immutableSamplerBinding.set,
							immutableSamplerBinding.binding));

					nrAssert(
						bindingInfo->descriptorType == vk::DescriptorType::eSampler || bindingInfo->descriptorType == vk::DescriptorType::eCombinedImageSampler,
						std::format(
							"CursorPipelineLayout::create immutable sampler target must be sampler/combinded-image-sampler at set={}, binding={}, descriptorType={}",
							immutableSamplerBinding.set,
							immutableSamplerBinding.binding,
							vk::to_string(bindingInfo->descriptorType)));

					nrAssert(
						bindingInfo->descriptorCount == immutableSamplerBinding.descriptorCount,
						std::format(
							"CursorPipelineLayout::create immutable sampler descriptorCount mismatch at set={}, binding={}, layoutCount={}, immutableCount={}",
							immutableSamplerBinding.set,
							immutableSamplerBinding.binding,
							bindingInfo->descriptorCount,
							immutableSamplerBinding.descriptorCount));

					ImmutableSamplerBindingState state{};
					state.set = immutableSamplerBinding.set;
					state.binding = immutableSamplerBinding.binding;
					state.samplers.reserve(immutableSamplerBinding.descriptorCount);
					state.rawSamplers.reserve(immutableSamplerBinding.descriptorCount);

					std::ranges::for_each(std::views::iota(uint32_t{0}, immutableSamplerBinding.descriptorCount), [&](uint32_t arrayIndex) {
						auto samplerDebugName = std::format("immutable_sampler_s{}_b{}_i{}", state.set, state.binding, arrayIndex);
						state.samplers.push_back(SlangSampler::create(device, immutableSamplerBinding.samplerDesc, samplerDebugName));
						nrAssert(state.samplers.back().valid(), std::format("CursorPipelineLayout::create failed to create immutable sampler '{}'.", samplerDebugName));
						state.rawSamplers.push_back(state.samplers.back().raw());
					});

					layout.immutableSamplerBindings_.push_back(std::move(state));
				});

				layout.setLayouts_.reserve(setLayouts.size());

				std::vector<vk::DescriptorSetLayout> pipelineSetLayouts;
				pipelineSetLayouts.reserve(setLayouts.size());

				std::ranges::for_each(setLayouts, [&](const DescriptorSetLayoutInfo &setInfo) {
						auto bindings = descriptorLayout.makeVkSetLayoutBindings(setInfo.set);
						auto bindingFlags = descriptorLayout.makeVkSetLayoutBindingFlags(setInfo.set);
						std::ranges::for_each(bindings, [&](vk::DescriptorSetLayoutBinding &binding) {
							auto immutableSamplerIt = std::ranges::find_if(layout.immutableSamplerBindings_, [&](ImmutableSamplerBindingState &state) {
								return state.set == setInfo.set && state.binding == binding.binding;
							});
							if (immutableSamplerIt == std::ranges::end(layout.immutableSamplerBindings_))
							{
								return;
							}

							nrAssert(
								binding.descriptorCount == immutableSamplerIt->rawSamplers.size(),
								std::format(
									"CursorPipelineLayout::create immutable sampler descriptorCount mismatch at set={}, binding={}, layoutCount={}, immutableCount={}",
									setInfo.set,
									binding.binding,
									binding.descriptorCount,
									immutableSamplerIt->rawSamplers.size()));

							binding.pImmutableSamplers = immutableSamplerIt->rawSamplers.data();
							immutableSamplerIt->isApplied = true;
						});

						vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
						vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
						setLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
						setLayoutInfo.pBindings = bindings.data();
						if (!bindingFlags.empty())
						{
							bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
							bindingFlagsInfo.pBindingFlags = bindingFlags.data();
							setLayoutInfo.pNext = &bindingFlagsInfo;
							if (std::ranges::any_of(bindingFlags, [](vk::DescriptorBindingFlags flags) {
								return (flags & vk::DescriptorBindingFlagBits::eUpdateAfterBind) == vk::DescriptorBindingFlagBits::eUpdateAfterBind;
							}))
							{
								setLayoutInfo.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
							}
						}

						layout.setLayouts_.push_back(DescriptorSetLayoutHandle{
								.set = setInfo.set,
								.layout = vk::raii::DescriptorSetLayout(device, setLayoutInfo),
						});
						pipelineSetLayouts.push_back(*layout.setLayouts_.back().layout);
				});

				std::ranges::for_each(layout.immutableSamplerBindings_, [](const ImmutableSamplerBindingState &state) {
					nrAssert(
						state.isApplied,
						std::format(
							"CursorPipelineLayout::create immutable sampler binding was not used by descriptor layout at set={}, binding={}",
							state.set,
							state.binding));
				});

				vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
				pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(pipelineSetLayouts.size());
				pipelineLayoutInfo.pSetLayouts = pipelineSetLayouts.data();
				auto pushConstantRanges = descriptorLayout.makeVkPushConstantRanges();
				pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
				pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
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

		void bindDescriptorSet(
				vk::CommandBuffer commandBuffer,
				vk::PipelineBindPoint bindPoint,
				const ShaderBindingSet &set,
				std::span<const uint32_t> dynamicOffsets = {}) const;

		void bindDescriptorSets(
				vk::CommandBuffer commandBuffer,
				vk::PipelineBindPoint bindPoint,
				std::span<const ShaderBindingSet> sets,
				std::span<const uint32_t> dynamicOffsets = {}) const;

		void pushConstants(
				vk::CommandBuffer commandBuffer,
				vk::ShaderStageFlags stageFlags,
				uint32_t offset,
				std::span<const uint8_t> bytes) const
		{
			nrAssert(valid(), "CursorPipelineLayout::pushConstants requires a valid pipeline layout.");
			if (bytes.empty())
			{
				return;
			}
			nrAssert(
				bytes.size() <= std::numeric_limits<uint32_t>::max(),
				std::format("CursorPipelineLayout::pushConstants payload too large: {} bytes", bytes.size()));
			commandBuffer.pushConstants(raw(), stageFlags, offset, static_cast<uint32_t>(bytes.size()), bytes.data());
		}

	private:
		struct DescriptorSetLayoutHandle
		{
				uint32_t set = 0;
				vk::raii::DescriptorSetLayout layout = {nullptr};
		};

		struct ImmutableSamplerBindingState
		{
				uint32_t set = 0;
				uint32_t binding = 0;
				std::vector<SlangSampler> samplers;
				std::vector<vk::Sampler> rawSamplers;
				bool isApplied = false;
		};

		std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
		std::vector<DescriptorSetLayoutHandle> setLayouts_;
		std::vector<ImmutableSamplerBindingState> immutableSamplerBindings_;
		vk::raii::PipelineLayout pipelineLayout_ = {nullptr};
};

inline void CursorPipelineLayout::bindDescriptorSet(
		vk::CommandBuffer commandBuffer,
		vk::PipelineBindPoint bindPoint,
		const ShaderBindingSet &set,
		std::span<const uint32_t> dynamicOffsets) const
{
	nrAssert(valid(), "CursorPipelineLayout::bindDescriptorSet requires a valid pipeline layout.");
	nrAssert(set.valid(), std::format("CursorPipelineLayout::bindDescriptorSet received invalid set {}.", set.setIndex()));

	auto handle = set.raw();
	commandBuffer.bindDescriptorSets(bindPoint, raw(), set.setIndex(), {handle}, dynamicOffsets);
}

inline void CursorPipelineLayout::bindDescriptorSets(
		vk::CommandBuffer commandBuffer,
		vk::PipelineBindPoint bindPoint,
		std::span<const ShaderBindingSet> sets,
		std::span<const uint32_t> dynamicOffsets) const
{
	nrAssert(valid(), "CursorPipelineLayout::bindDescriptorSets requires a valid pipeline layout.");
	nrAssert(
		dynamicOffsets.empty(),
		"CursorPipelineLayout::bindDescriptorSets with multiple sets does not accept shared dynamic offsets. Bind per-set when using dynamic offsets.");

	for (const auto &set : sets)
	{
		if (!set.valid())
		{
			continue;
		}
		bindDescriptorSet(commandBuffer, bindPoint, set, {});
	}
}

class VkShaderProgram
{
	public:
		[[nodiscard]] static VkShaderProgram create(const vk::raii::Device &device, std::span<const SlangEntryPointData *const> selectedEntryPoints)
		{
				nrAssert(!selectedEntryPoints.empty(), "VkShaderProgram::create requires at least one selected entrypoint.");

				VkShaderProgram result;
				result.modules_.reserve(selectedEntryPoints.size());
				result.entryPointNames_.reserve(selectedEntryPoints.size());
				result.stages_.reserve(selectedEntryPoints.size());
				result.stageCreateInfos_.reserve(selectedEntryPoints.size());

				std::ranges::for_each(selectedEntryPoints, [&](const SlangEntryPointData *entryPoint) {
					nrAssert(entryPoint != nullptr, "VkShaderProgram::create received a null entrypoint selection.");
					nrAssert(entryPoint->codeBlob != nullptr, std::format("Entry point '{}' has null code blob.", entryPoint->entryPointName));

					auto codeSize = entryPoint->codeBlob->getBufferSize();
					nrAssert(codeSize > 0, std::format("Entry point '{}' has empty code blob.", entryPoint->entryPointName));
					nrAssert(
						codeSize % sizeof(uint32_t) == 0,
						std::format("Entry point '{}' code size is not uint32-aligned: {} bytes.", entryPoint->entryPointName, codeSize));

					vk::ShaderModuleCreateInfo moduleInfo{};
					moduleInfo.codeSize = codeSize;
					moduleInfo.pCode = static_cast<const uint32_t *>(entryPoint->codeBlob->getBufferPointer());
					result.modules_.emplace_back(device, moduleInfo);

					result.entryPointNames_.push_back(entryPoint->entryPointName.empty() ? "main" : entryPoint->entryPointName);
					result.stages_.push_back(entryPoint->stage);

					vk::PipelineShaderStageCreateInfo stageInfo{};
					stageInfo.stage = toVkShaderStage(entryPoint->stage);
					stageInfo.module = *result.modules_.back();
					stageInfo.pName = result.entryPointNames_.back().c_str();
					result.stageCreateInfos_.push_back(stageInfo);
				});

				return result;
		}

		[[nodiscard]] bool valid() const noexcept { return !stageCreateInfos_.empty(); }
		[[nodiscard]] const vk::PipelineShaderStageCreateInfo &stageCreateInfo(uint32_t index) const noexcept { return stageCreateInfos_[index]; }
		[[nodiscard]] std::span<const SlangStage> stages() const noexcept { return stages_; }
		[[nodiscard]] std::span<const std::string> entryPointNames() const noexcept { return entryPointNames_; }

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
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
				const VkShaderProgram &shaderProgram,
				const GraphicsPipelineDesc &desc = {},
				const vk::raii::PipelineCache *pipelineCache = nullptr)
		{
				nrAssert(
					!desc.colorAttachmentFormats.empty() || desc.depthAttachmentFormat.has_value() || desc.stencilAttachmentFormat.has_value(),
					"GraphicsPipeline::create requires at least one attachment format when using dynamic rendering.");
				nrAssert(layout.valid(), "GraphicsPipeline::create requires a valid pipeline layout.");
				nrAssert(shaderProgram.valid(), "GraphicsPipeline::create requires a valid shader program.");

				auto graphicsStageIndices = std::views::iota(uint32_t{0}, static_cast<uint32_t>(shaderProgram.stages().size())) |
				                          std::views::filter([&](uint32_t index) { return detail::isGraphicsStage(shaderProgram.stages()[index]); }) |
				                          std::ranges::to<std::vector>();
				nrAssert(!graphicsStageIndices.empty(), "GraphicsPipeline::create requires at least one graphics shader stage.");

				auto stageCreateInfos = graphicsStageIndices |
				                       std::views::transform([&](uint32_t stageIndex) { return shaderProgram.stageCreateInfo(stageIndex); }) |
				                       std::ranges::to<std::vector>();

				auto hasStage = [&](SlangStage stage) {
					return std::ranges::any_of(
						graphicsStageIndices,
						[&](uint32_t stageIndex) { return shaderProgram.stages()[stageIndex] == stage; });
				};
				auto const hasVertexStage = hasStage(SLANG_STAGE_VERTEX);
				auto const hasMeshStage = hasStage(SLANG_STAGE_MESH);

				if (desc.mode == GraphicsPipelineMode::Mesh)
				{
					nrAssert(hasMeshStage, "GraphicsPipeline::create Mesh mode requires a mesh shader stage.");
					nrAssert(!hasVertexStage, "GraphicsPipeline::create Mesh mode cannot include a vertex shader stage.");
				}
				else
				{
					nrAssert(hasVertexStage, "GraphicsPipeline::create StandardGraphics mode requires a vertex shader stage.");
					nrAssert(!hasMeshStage, "GraphicsPipeline::create StandardGraphics mode cannot include mesh shader stages.");
				}

				auto colorBlendAttachments = desc.colorBlendAttachments;
				if (colorBlendAttachments.empty())
				{
					colorBlendAttachments = desc.colorAttachmentFormats |
					                        std::views::transform([](vk::Format) {
						                        return vk::PipelineColorBlendAttachmentState{
							                        vk::False,
							                        vk::BlendFactor::eOne,
							                        vk::BlendFactor::eZero,
							                        vk::BlendOp::eAdd,
							                        vk::BlendFactor::eOne,
							                        vk::BlendFactor::eZero,
							                        vk::BlendOp::eAdd,
							                        vk::ColorComponentFlagBits::eR |
							                                vk::ColorComponentFlagBits::eG |
							                                vk::ColorComponentFlagBits::eB |
							                                vk::ColorComponentFlagBits::eA,
						                        };
					                        }) |
					                        std::ranges::to<std::vector>();
				}
				nrAssert(
					colorBlendAttachments.size() == desc.colorAttachmentFormats.size(),
					std::format(
						"GraphicsPipeline::create color blend attachment count mismatch. formats={}, blends={}",
						desc.colorAttachmentFormats.size(),
						colorBlendAttachments.size()));

				auto dynamicStates = desc.dynamicStates;
				if (dynamicStates.empty())
				{
					dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
				}

				vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
				vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
				inputAssemblyInfo.topology = desc.topology;
				inputAssemblyInfo.primitiveRestartEnable = vk::False;

				vk::PipelineViewportStateCreateInfo viewportStateInfo{};
				viewportStateInfo.viewportCount = 1;
				viewportStateInfo.scissorCount = 1;

				vk::PipelineRasterizationStateCreateInfo rasterizationInfo{};
				rasterizationInfo.depthClampEnable = vk::False;
				rasterizationInfo.rasterizerDiscardEnable = vk::False;
				rasterizationInfo.polygonMode = desc.polygonMode;
				rasterizationInfo.cullMode = desc.cullMode;
				rasterizationInfo.frontFace = desc.frontFace;
				rasterizationInfo.depthBiasEnable = vk::False;
				rasterizationInfo.lineWidth = 1.0f;

				vk::PipelineMultisampleStateCreateInfo multisampleInfo{};
				multisampleInfo.rasterizationSamples = desc.sampleCount;
				multisampleInfo.sampleShadingEnable = vk::False;

				vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{};
				depthStencilInfo.depthTestEnable = desc.depthTestEnable ? vk::True : vk::False;
				depthStencilInfo.depthWriteEnable = desc.depthWriteEnable ? vk::True : vk::False;
				depthStencilInfo.depthCompareOp = desc.depthCompareOp;
				depthStencilInfo.depthBoundsTestEnable = vk::False;
				depthStencilInfo.stencilTestEnable = desc.stencilAttachmentFormat.has_value() ? vk::True : vk::False;

				vk::PipelineColorBlendStateCreateInfo colorBlendInfo{};
				colorBlendInfo.logicOpEnable = vk::False;
				colorBlendInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
				colorBlendInfo.pAttachments = colorBlendAttachments.data();

				vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
				dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
				dynamicStateInfo.pDynamicStates = dynamicStates.data();

				auto renderingColorFormats = desc.colorAttachmentFormats;
				vk::PipelineRenderingCreateInfo renderingInfo{};
				renderingInfo.colorAttachmentCount = static_cast<uint32_t>(renderingColorFormats.size());
				renderingInfo.pColorAttachmentFormats = renderingColorFormats.data();
				renderingInfo.depthAttachmentFormat = desc.depthAttachmentFormat.value_or(vk::Format::eUndefined);
				renderingInfo.stencilAttachmentFormat = desc.stencilAttachmentFormat.value_or(vk::Format::eUndefined);

				vk::GraphicsPipelineCreateInfo createInfo{};
				createInfo.flags = desc.flags;
				createInfo.stageCount = static_cast<uint32_t>(stageCreateInfos.size());
				createInfo.pStages = stageCreateInfos.data();
				auto const usesMeshPipeline = desc.mode == GraphicsPipelineMode::Mesh;
				createInfo.pVertexInputState = usesMeshPipeline ? nullptr : &vertexInputInfo;
				createInfo.pInputAssemblyState = usesMeshPipeline ? nullptr : &inputAssemblyInfo;
				createInfo.pViewportState = &viewportStateInfo;
				createInfo.pRasterizationState = &rasterizationInfo;
				createInfo.pMultisampleState = &multisampleInfo;
				createInfo.pDepthStencilState = &depthStencilInfo;
				createInfo.pColorBlendState = &colorBlendInfo;
				createInfo.pDynamicState = &dynamicStateInfo;
				createInfo.layout = layout.raw();
				createInfo.renderPass = nullptr;
				createInfo.subpass = 0;
				createInfo.pNext = &renderingInfo;

				GraphicsPipeline pipeline;
				auto pipelineCacheOptional =
						pipelineCache != nullptr ? vk::Optional<const vk::raii::PipelineCache>(*pipelineCache) : vk::Optional<const vk::raii::PipelineCache>(nullptr);
				pipeline.pipeline_ = vk::raii::Pipeline(device, pipelineCacheOptional, createInfo);
				return pipeline;
		}

		[[nodiscard]] bool valid() const noexcept { return pipeline_ != nullptr; }
		[[nodiscard]] vk::Pipeline raw() const noexcept { return valid() ? *pipeline_ : vk::Pipeline{}; }

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
				const vk::raii::PipelineCache *pipelineCache = nullptr)
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
				auto pipelineCacheOptional =
						pipelineCache != nullptr ? vk::Optional<const vk::raii::PipelineCache>(*pipelineCache) : vk::Optional<const vk::raii::PipelineCache>(nullptr);
				pipeline.pipeline_ = vk::raii::Pipeline(device, pipelineCacheOptional, createInfo);
				return pipeline;
		}

		[[nodiscard]] bool valid() const noexcept { return pipeline_ != nullptr; }
		[[nodiscard]] vk::Pipeline raw() const noexcept { return valid() ? *pipeline_ : vk::Pipeline{}; }

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
				const vk::raii::PipelineCache *pipelineCache = nullptr)
		{
				nrAssert(layout.valid(), "RayTracingPipeline::create requires a valid pipeline layout.");
				nrAssert(shaderProgram.valid(), "RayTracingPipeline::create requires a valid shader program.");
				nrAssert(desc.maxRayRecursionDepth > 0u, "RayTracingPipeline::create requires maxRayRecursionDepth > 0.");
				auto descValidation = validateRayTracingPipelineDesc(desc);
				nrAssert(!descValidation.has_value(), std::format("RayTracingPipeline::create invalid desc: {}", descValidation.value_or(std::string{})));

				auto createFlags = desc.flags;
				if (desc.createAsLibrary)
				{
					createFlags |= vk::PipelineCreateFlags{VK_PIPELINE_CREATE_LIBRARY_BIT_KHR};
				}

				auto rtStageIndices = std::views::iota(uint32_t{0}, static_cast<uint32_t>(shaderProgram.stages().size())) |
				                  std::views::filter([&](uint32_t index) { return detail::isRayTracingStage(shaderProgram.stages()[index]); }) |
				                  std::ranges::to<std::vector>();
				nrAssert(!rtStageIndices.empty(), "RayTracingPipeline::create requires at least one ray tracing shader stage.");

				auto stageCreateInfos = rtStageIndices |
				                       std::views::transform([&](uint32_t stageIndex) { return shaderProgram.stageCreateInfo(stageIndex); }) |
				                       std::ranges::to<std::vector>();

				constexpr uint32_t shaderUnused = std::numeric_limits<uint32_t>::max();
				auto groups = std::vector<vk::RayTracingShaderGroupCreateInfoKHR>{};

				auto findLocalStageIndex = [&](std::string_view entryPointName, std::initializer_list<SlangStage> expectedStages) {
					if (entryPointName.empty())
					{
						return shaderUnused;
					}
					auto it = std::ranges::find(shaderProgram.entryPointNames(), entryPointName);
					nrAssert(it != std::ranges::end(shaderProgram.entryPointNames()), std::format("RayTracingPipeline::create unknown entrypoint '{}' in custom group.", entryPointName));
					auto globalIndex = static_cast<uint32_t>(std::distance(std::ranges::begin(shaderProgram.entryPointNames()), it));
					auto localIt = std::ranges::find(rtStageIndices, globalIndex);
					nrAssert(localIt != std::ranges::end(rtStageIndices), std::format("RayTracingPipeline::create entrypoint '{}' is not a RT stage.", entryPointName));
					auto stage = shaderProgram.stages()[globalIndex];
					nrAssert(
						std::ranges::find(expectedStages, stage) != expectedStages.end(),
						std::format("RayTracingPipeline::create entrypoint '{}' stage mismatch for custom group.", entryPointName));
					return static_cast<uint32_t>(std::distance(std::ranges::begin(rtStageIndices), localIt));
				};

				if (desc.groups.empty())
				{
					groups.reserve(stageCreateInfos.size());
					for (uint32_t localIndex = 0; localIndex < static_cast<uint32_t>(rtStageIndices.size()); ++localIndex)
					{
						auto stage = shaderProgram.stages()[rtStageIndices[localIndex]];
						vk::RayTracingShaderGroupCreateInfoKHR group{};
						group.generalShader = shaderUnused;
						group.closestHitShader = shaderUnused;
						group.anyHitShader = shaderUnused;
						group.intersectionShader = shaderUnused;

						switch (stage)
						{
						case SLANG_STAGE_RAY_GENERATION:
						case SLANG_STAGE_MISS:
						case SLANG_STAGE_CALLABLE:
							group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
							group.generalShader = localIndex;
							break;
						case SLANG_STAGE_CLOSEST_HIT:
							group.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
							group.closestHitShader = localIndex;
							break;
						case SLANG_STAGE_ANY_HIT:
							group.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
							group.anyHitShader = localIndex;
							break;
						case SLANG_STAGE_INTERSECTION:
							group.type = vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup;
							group.intersectionShader = localIndex;
							break;
						default:
							nrAssert(false, "Unexpected non-ray-tracing stage found during RT pipeline assembly.");
							break;
						}

						groups.push_back(group);
					}
				}
				else
				{
					groups.reserve(desc.groups.size());
					for (const auto &groupDesc : desc.groups)
					{
						vk::RayTracingShaderGroupCreateInfoKHR group{};
						group.type = groupDesc.type;
						group.generalShader = findLocalStageIndex(groupDesc.generalEntryPoint, {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CALLABLE});
						group.closestHitShader = findLocalStageIndex(groupDesc.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT});
						group.anyHitShader = findLocalStageIndex(groupDesc.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT});
						group.intersectionShader = findLocalStageIndex(groupDesc.intersectionEntryPoint, {SLANG_STAGE_INTERSECTION});
						groups.push_back(group);
					}
				}

				vk::RayTracingPipelineCreateInfoKHR createInfo{};
				createInfo.flags = createFlags;
				createInfo.stageCount = static_cast<uint32_t>(stageCreateInfos.size());
				createInfo.pStages = stageCreateInfos.data();
				createInfo.groupCount = static_cast<uint32_t>(groups.size());
				createInfo.pGroups = groups.data();
				createInfo.maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth;
				createInfo.layout = layout.raw();

				vk::PipelineLibraryCreateInfoKHR libraryInfo{};
				if (!desc.linkedLibraries.empty())
				{
					libraryInfo.libraryCount = static_cast<uint32_t>(desc.linkedLibraries.size());
					libraryInfo.pLibraries = desc.linkedLibraries.data();
					createInfo.pLibraryInfo = &libraryInfo;
				}

				vk::RayTracingPipelineInterfaceCreateInfoKHR libraryInterface{};
				if (desc.libraryInterface.has_value())
				{
					libraryInterface.maxPipelineRayPayloadSize = desc.libraryInterface->maxPipelineRayPayloadSize;
					libraryInterface.maxPipelineRayHitAttributeSize = desc.libraryInterface->maxPipelineRayHitAttributeSize;
					createInfo.pLibraryInterface = &libraryInterface;
				}

				RayTracingPipeline pipeline;
				auto deferredOperation = vk::Optional<const vk::raii::DeferredOperationKHR>(nullptr);
				auto pipelineCacheOptional =
						pipelineCache != nullptr ? vk::Optional<const vk::raii::PipelineCache>(*pipelineCache) : vk::Optional<const vk::raii::PipelineCache>(nullptr);
				pipeline.pipeline_ = vk::raii::Pipeline(device, deferredOperation, pipelineCacheOptional, createInfo);
				pipeline.device_ = std::cref(device);
				pipeline.shaderGroupCount_ = static_cast<uint32_t>(groups.size());
				return pipeline;
		}

		[[nodiscard]] bool valid() const noexcept { return pipeline_ != nullptr; }
		[[nodiscard]] vk::Pipeline raw() const noexcept { return valid() ? *pipeline_ : vk::Pipeline{}; }
		[[nodiscard]] uint32_t shaderGroupCount() const noexcept { return shaderGroupCount_; }

		[[nodiscard]] std::vector<uint8_t> shaderGroupHandles(uint32_t firstGroup, uint32_t groupCount, uint32_t handleSize) const
		{
			nrAssert(valid(), "RayTracingPipeline::shaderGroupHandles requires a valid pipeline.");
			nrAssert(device_.has_value(), "RayTracingPipeline::shaderGroupHandles requires a valid device reference.");
			nrAssert(groupCount > 0u, "RayTracingPipeline::shaderGroupHandles requires groupCount > 0.");
			nrAssert(handleSize > 0u, "RayTracingPipeline::shaderGroupHandles requires handleSize > 0.");
			nrAssert(firstGroup < shaderGroupCount_, "RayTracingPipeline::shaderGroupHandles firstGroup is out of range.");
			auto requestedEnd = static_cast<uint64_t>(firstGroup) + static_cast<uint64_t>(groupCount);
			nrAssert(requestedEnd <= static_cast<uint64_t>(shaderGroupCount_), "RayTracingPipeline::shaderGroupHandles range exceeds group count.");

			auto dataSize = static_cast<size_t>(handleSize) * static_cast<size_t>(groupCount);
			return pipeline_.getRayTracingShaderGroupHandlesKHR<uint8_t>(firstGroup, groupCount, dataSize);
		}

	private:
		std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
		uint32_t shaderGroupCount_ = 0;
		vk::raii::Pipeline pipeline_ = {nullptr};
};

template <typename TPipeline>
struct PipelineState
{
		CursorPipelineLayout layout;
		ShaderDescriptorLayout descriptorLayout;
		ShaderBindingPool bindingPool;
		TPipeline pipeline;
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
	void bindDevice(const vk::raii::Device &device, const RayTracingCapabilitySnapshot *rtCapabilities = nullptr)
	{
		device_ = std::cref(device);
		if (rtCapabilities != nullptr)
		{
			rtCapabilities_ = *rtCapabilities;
		}
		else
		{
			rtCapabilities_.reset();
		}
		vk::PipelineCacheCreateInfo cacheCreateInfo{};
		pipelineCache_ = vk::raii::PipelineCache(device, cacheCreateInfo);
	}

	[[nodiscard]] ShaderBindingPool createBindingPool(const ShaderDescriptorLayout &descriptorLayout, ShaderBindingPoolConfig config = {}) const
	{
		nrAssert(device_.has_value(), "PipelineService::createBindingPool requires a bound logical device.");
		return ShaderBindingPool::create(device_->get(), descriptorLayout, config);
	}

	[[nodiscard]] ShaderBindingSet allocateBindingSet(const CursorPipelineLayout &layout, ShaderBindingPool &bindingPool, uint32_t setIndex, std::optional<uint32_t> variableDescriptorCount = std::nullopt) const
	{
		nrAssert(device_.has_value(), "PipelineService::allocateBindingSet requires a bound logical device.");
		auto descriptorSetLayout = layout.descriptorSetLayout(setIndex);
		if (!descriptorSetLayout.has_value())
		{
			return {};
		}
		return bindingPool.allocate(*descriptorSetLayout, setIndex, variableDescriptorCount);
	}

	[[nodiscard]] std::vector<ShaderBindingSet> allocateBindingSets(const CursorPipelineLayout &layout, ShaderBindingPool &bindingPool) const
	{
		nrAssert(device_.has_value(), "PipelineService::allocateBindingSets requires a bound logical device.");
		return layout.setIndices() |
		       std::views::transform([&](uint32_t setIndex) { return allocateBindingSet(layout, bindingPool, setIndex); }) |
		       std::ranges::to<std::vector>();
	}

	[[nodiscard]] SlangSampler createSampler(SlangSamplerDesc desc = {}, std::string_view debugName = {}) const
	{
		nrAssert(device_.has_value(), "PipelineService::createSampler requires a bound logical device.");
		return SlangSampler::create(device_->get(), std::move(desc), debugName);
	}

	[[nodiscard]] PipelineState<GraphicsPipeline> createGraphicsPipeline(const SlangProgram &slangProgram, const GraphicsPipelineDesc &desc = {}, uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const
	{
		nrAssert(device_.has_value(), "PipelineService::createGraphicsPipeline requires a bound logical device.");
		nrAssert(slangProgram.valid(), "PipelineService::createGraphicsPipeline requires a valid SlangProgram.");

		(void)immutableSamplers;

		const auto &device = device_->get();
		auto descriptorLayout = ShaderDescriptorLayout::create(slangProgram);
		auto layout = CursorPipelineLayout::create(device, descriptorLayout, immutableSamplers);

		auto effectiveDesc = desc;
		auto appendDynamicState = [&effectiveDesc](vk::DynamicState state) {
			if (std::ranges::none_of(effectiveDesc.dynamicStates, [state](vk::DynamicState current) { return current == state; }))
			{
				effectiveDesc.dynamicStates.push_back(state);
			}
		};
		appendDynamicState(vk::DynamicState::eCullModeEXT);
		appendDynamicState(vk::DynamicState::eFrontFaceEXT);
		if (effectiveDesc.mode != GraphicsPipelineMode::Mesh)
		{
			appendDynamicState(vk::DynamicState::ePrimitiveTopologyEXT);
		}
		appendDynamicState(vk::DynamicState::eDepthTestEnableEXT);
		appendDynamicState(vk::DynamicState::eDepthWriteEnableEXT);
		appendDynamicState(vk::DynamicState::eDepthCompareOpEXT);
		appendDynamicState(vk::DynamicState::ePolygonModeEXT);
		appendDynamicState(vk::DynamicState::eRasterizationSamplesEXT);

		auto selectedEntryPoints = std::vector<const SlangEntryPointData *>{};
		if (!effectiveDesc.entryPointNames.empty())
		{
			selectedEntryPoints.reserve(effectiveDesc.entryPointNames.size());
			std::ranges::for_each(effectiveDesc.entryPointNames, [&](const std::string &entryPointName) {
				auto const *entryPointData = slangProgram.entryPointData(entryPointName);
				nrAssert(entryPointData != nullptr, std::format("PipelineService::createGraphicsPipeline unknown entrypoint '{}'.", entryPointName));
				nrAssert(detail::isGraphicsStage(entryPointData->stage), std::format("PipelineService::createGraphicsPipeline entrypoint '{}' is not a graphics stage.", entryPointName));
				selectedEntryPoints.push_back(entryPointData);
			});
		}
		else
		{
			selectedEntryPoints = slangProgram.entryPoints() |
			                      std::views::filter([](const SlangEntryPointData &entryPoint) { return detail::isGraphicsStage(entryPoint.stage); }) |
			                      std::views::transform([](const SlangEntryPointData &entryPoint) { return &entryPoint; }) |
			                      std::ranges::to<std::vector>();
		}
		nrAssert(!selectedEntryPoints.empty(), "PipelineService::createGraphicsPipeline requires at least one selected graphics entrypoint.");

		auto shaderProgram = VkShaderProgram::create(device, selectedEntryPoints);
		auto pipeline = GraphicsPipeline::create(device, layout, shaderProgram, effectiveDesc, pipelineCache_ != nullptr ? &pipelineCache_ : nullptr);
		auto bindingPool = ShaderBindingPool::create(device, descriptorLayout, ShaderBindingPoolConfig{.maxSets = descriptorMaxSets});
		return PipelineState<GraphicsPipeline>{
			.layout = std::move(layout),
			.descriptorLayout = std::move(descriptorLayout),
			.bindingPool = std::move(bindingPool),
			.pipeline = std::move(pipeline),
		};
	}

	[[nodiscard]] PipelineState<ComputePipeline> createComputePipeline(const SlangProgram &slangProgram, const ComputePipelineDesc &desc = {}, uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const
	{
		nrAssert(device_.has_value(), "PipelineService::createComputePipeline requires a bound logical device.");
		nrAssert(slangProgram.valid(), "PipelineService::createComputePipeline requires a valid SlangProgram.");

		(void)immutableSamplers;

		const auto &device = device_->get();
		auto descriptorLayout = ShaderDescriptorLayout::create(slangProgram);
		auto layout = CursorPipelineLayout::create(device, descriptorLayout, immutableSamplers);

		auto selectedEntryPoints = std::vector<const SlangEntryPointData *>{};
		if (!desc.entryPointName.empty())
		{
			auto const *entryPointData = slangProgram.entryPointData(desc.entryPointName);
			nrAssert(entryPointData != nullptr, std::format("PipelineService::createComputePipeline unknown entrypoint '{}'.", desc.entryPointName));
			nrAssert(entryPointData->stage == SLANG_STAGE_COMPUTE, std::format("PipelineService::createComputePipeline entrypoint '{}' is not a compute stage.", desc.entryPointName));
			selectedEntryPoints.push_back(entryPointData);
		}
		else
		{
			auto it = std::ranges::find_if(slangProgram.entryPoints(), [](const SlangEntryPointData &entryPoint) { return entryPoint.stage == SLANG_STAGE_COMPUTE; });
			nrAssert(it != std::ranges::end(slangProgram.entryPoints()), "PipelineService::createComputePipeline requires at least one compute entrypoint.");
			selectedEntryPoints.push_back(&(*it));
		}

		auto shaderProgram = VkShaderProgram::create(device, selectedEntryPoints);
		auto pipeline = ComputePipeline::create(device, layout, shaderProgram, desc, pipelineCache_ != nullptr ? &pipelineCache_ : nullptr);
		auto bindingPool = ShaderBindingPool::create(device, descriptorLayout, ShaderBindingPoolConfig{.maxSets = descriptorMaxSets});
		return PipelineState<ComputePipeline>{
			.layout = std::move(layout),
			.descriptorLayout = std::move(descriptorLayout),
			.bindingPool = std::move(bindingPool),
			.pipeline = std::move(pipeline),
		};
	}

	[[nodiscard]] PipelineState<RayTracingPipeline> createRayTracingPipeline(const SlangProgram &slangProgram, const RayTracingPipelineDesc &desc = {}, uint32_t descriptorMaxSets = 64, std::span<const SlangImmutableSamplerBinding> immutableSamplers = {}) const
	{
		nrAssert(device_.has_value(), "PipelineService::createRayTracingPipeline requires a bound logical device.");
		nrAssert(slangProgram.valid(), "PipelineService::createRayTracingPipeline requires a valid SlangProgram.");

		(void)immutableSamplers;

		const auto &device = device_->get();
		auto descriptorLayout = ShaderDescriptorLayout::create(slangProgram);
		auto layout = CursorPipelineLayout::create(device, descriptorLayout, immutableSamplers);

		auto effectiveDesc = desc;
		if (rtCapabilities_.has_value())
		{
			nrAssert(
				effectiveDesc.maxRayRecursionDepth <= rtCapabilities_->maxRayRecursionDepth,
				std::format(
					"PipelineService::createRayTracingPipeline recursion depth {} exceeds device max {}.",
					effectiveDesc.maxRayRecursionDepth,
					rtCapabilities_->maxRayRecursionDepth));
		}

		auto selectedEntryPoints = std::vector<const SlangEntryPointData *>{};
		if (!effectiveDesc.entryPointNames.empty())
		{
			selectedEntryPoints.reserve(effectiveDesc.entryPointNames.size());
			std::ranges::for_each(effectiveDesc.entryPointNames, [&](const std::string &entryPointName) {
				auto const *entryPointData = slangProgram.entryPointData(entryPointName);
				nrAssert(entryPointData != nullptr, std::format("PipelineService::createRayTracingPipeline unknown entrypoint '{}'.", entryPointName));
				nrAssert(detail::isRayTracingStage(entryPointData->stage), std::format("PipelineService::createRayTracingPipeline entrypoint '{}' is not a ray-tracing stage.", entryPointName));
				selectedEntryPoints.push_back(entryPointData);
			});
		}
		else
		{
			selectedEntryPoints = slangProgram.entryPoints() |
			                      std::views::filter([](const SlangEntryPointData &entryPoint) { return detail::isRayTracingStage(entryPoint.stage); }) |
			                      std::views::transform([](const SlangEntryPointData &entryPoint) { return &entryPoint; }) |
			                      std::ranges::to<std::vector>();
		}
		nrAssert(!selectedEntryPoints.empty(), "PipelineService::createRayTracingPipeline requires at least one selected ray-tracing entrypoint.");

		auto shaderProgram = VkShaderProgram::create(device, selectedEntryPoints);
		auto pipeline = RayTracingPipeline::create(device, layout, shaderProgram, effectiveDesc, pipelineCache_ != nullptr ? &pipelineCache_ : nullptr);
		auto bindingPool = ShaderBindingPool::create(device, descriptorLayout, ShaderBindingPoolConfig{.maxSets = descriptorMaxSets});
		return PipelineState<RayTracingPipeline>{
			.layout = std::move(layout),
			.descriptorLayout = std::move(descriptorLayout),
			.bindingPool = std::move(bindingPool),
			.pipeline = std::move(pipeline),
		};
	}

  private:
	std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
	std::optional<RayTracingCapabilitySnapshot> rtCapabilities_;
	vk::raii::PipelineCache pipelineCache_ = {nullptr};
};

} // namespace nr::rhi
