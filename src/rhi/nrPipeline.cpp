module nr.rhi;
import :pipeline;
import dependency.slang;
import dependency.vulkan;
import :type;
import nr.utils;
import :descriptor;
import :slang;
import std;

namespace nr::rhi
{
[[nodiscard]] std::optional<std::string> validateRayTracingPipelineDesc(const RayTracingPipelineDesc &desc)
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

[[nodiscard]] std::optional<std::string> validateRayTracingProgramAssemblyDesc(const RayTracingProgramAssemblyDesc &desc)
{
	if (desc.stages.empty())
	{
		return std::string{"RayTracingProgramAssemblyDesc requires at least one shader stage."};
	}
	if (desc.groups.empty())
	{
		return std::string{"RayTracingProgramAssemblyDesc requires at least one shader group."};
	}

	auto logicalEntryPointNames = std::set<std::string>{};
	auto logicalEntryPointStages = std::map<std::string, SlangStage>{};
	auto stageValidation = std::optional<std::string>{};
	std::ranges::for_each(desc.stages, [&](const RayTracingPipelineStageSelection &stage) {
		if (stageValidation.has_value())
		{
			return;
		}
		if (stage.logicalEntryPointName.empty())
		{
			stageValidation = "RayTracingProgramAssemblyDesc stages require non-empty logical entry-point names.";
			return;
		}
		if (!logicalEntryPointNames.insert(stage.logicalEntryPointName).second)
		{
			stageValidation = std::format("RayTracingProgramAssemblyDesc has duplicate logical entry point '{}'.", stage.logicalEntryPointName);
			return;
		}
		auto const *entryPointData = stage.program.get().entryPoint();
		if (entryPointData == nullptr)
		{
			stageValidation = std::format(
				"RayTracingProgramAssemblyDesc logical entry point '{}' references an invalid single-entry program.",
				stage.logicalEntryPointName);
			return;
		}
		if (!detail::isRayTracingStage(entryPointData->stage))
		{
			stageValidation = std::format(
				"RayTracingProgramAssemblyDesc logical entry point '{}' is not a ray-tracing stage.",
				stage.logicalEntryPointName);
			return;
		}
		logicalEntryPointStages.emplace(stage.logicalEntryPointName, entryPointData->stage);
	});
	if (stageValidation.has_value())
	{
		return stageValidation;
	}

	auto groupNames = std::set<std::string>{};
	auto groupValidation = std::optional<std::string>{};
	std::ranges::for_each(desc.groups, [&](const RayTracingShaderGroupDesc &group) {
		if (groupValidation.has_value())
		{
			return;
		}
		if (group.name.empty())
		{
			groupValidation = "RayTracingProgramAssemblyDesc groups require non-empty names.";
			return;
		}
		if (!groupNames.insert(group.name).second)
		{
			groupValidation = std::format("RayTracingProgramAssemblyDesc has duplicate group '{}'.", group.name);
			return;
		}

		auto validateEntryPointStage = [&](
			std::string_view entryPointName,
			std::initializer_list<SlangStage> expectedStages,
			std::string_view role) -> std::optional<std::string> {
			if (entryPointName.empty())
			{
				return std::nullopt;
			}
			auto const found = logicalEntryPointStages.find(std::string{entryPointName});
			if (found == logicalEntryPointStages.end())
			{
				return std::format(
					"RayTracingProgramAssemblyDesc group '{}' references unknown logical entry point '{}'.",
					group.name,
					entryPointName);
			}
			if (std::ranges::none_of(expectedStages, [&](SlangStage stage) { return stage == found->second; }))
			{
				return std::format(
					"RayTracingProgramAssemblyDesc group '{}' {} entry point '{}' has an incompatible shader stage.",
					group.name,
					role,
					entryPointName);
			}
			return std::nullopt;
		};

		auto validateGroupShape = [&]() -> std::optional<std::string> {
			switch (group.type)
			{
			case vk::RayTracingShaderGroupTypeKHR::eGeneral:
				if (group.generalEntryPoint.empty() || !group.closestHitEntryPoint.empty() || !group.anyHitEntryPoint.empty() || !group.intersectionEntryPoint.empty())
				{
					return std::format("RayTracingProgramAssemblyDesc general group '{}' must contain only a general entry point.", group.name);
				}
				return validateEntryPointStage(group.generalEntryPoint, {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CALLABLE}, "general");
			case vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup:
				if (!group.generalEntryPoint.empty() || !group.intersectionEntryPoint.empty() || (group.closestHitEntryPoint.empty() && group.anyHitEntryPoint.empty()))
				{
					return std::format("RayTracingProgramAssemblyDesc triangles hit group '{}' requires closest-hit and/or any-hit stages only.", group.name);
				}
				if (auto validation = validateEntryPointStage(group.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT}, "closest-hit"); validation.has_value())
				{
					return validation;
				}
				return validateEntryPointStage(group.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT}, "any-hit");
			case vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup:
				if (!group.generalEntryPoint.empty() || group.intersectionEntryPoint.empty())
				{
					return std::format("RayTracingProgramAssemblyDesc procedural hit group '{}' requires an intersection stage and no general stage.", group.name);
				}
				if (auto validation = validateEntryPointStage(group.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT}, "closest-hit"); validation.has_value())
				{
					return validation;
				}
				if (auto validation = validateEntryPointStage(group.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT}, "any-hit"); validation.has_value())
				{
					return validation;
				}
				return validateEntryPointStage(group.intersectionEntryPoint, {SLANG_STAGE_INTERSECTION}, "intersection");
			default:
				return std::format("RayTracingProgramAssemblyDesc group '{}' has an unsupported shader group type.", group.name);
			}
		};
		groupValidation = validateGroupShape();
	});
	if (groupValidation.has_value())
	{
		return groupValidation;
	}

	return std::nullopt;
}

namespace
{
[[nodiscard]] bool shaderStageFlagsCover(vk::ShaderStageFlags available, vk::ShaderStageFlags required) noexcept
{
	return (available & required) == required;
}

[[nodiscard]] std::string_view programDebugName(const SlangProgram &program) noexcept
{
	auto const *entryPoint = program.entryPoint();
	return entryPoint ? std::string_view{entryPoint->debugName} : std::string_view{"<invalid>"};
}

[[nodiscard]] std::optional<std::string> validateReflectionLayoutCoverage(
	const ShaderLayoutAbiSignature &owner,
	const ShaderLayoutAbiSignature &required,
	std::string_view ownerName,
	std::string_view requiredName)
{
	auto error = std::optional<std::string>{};
	std::ranges::for_each(required.descriptorBindings, [&](const ShaderDescriptorAbiBinding &requiredBinding) {
		if (error.has_value())
		{
			return;
		}

		auto ownerBinding = std::ranges::find_if(owner.descriptorBindings, [&](const ShaderDescriptorAbiBinding &candidate) {
			return candidate.set == requiredBinding.set && candidate.binding == requiredBinding.binding;
		});
		if (ownerBinding == owner.descriptorBindings.end())
		{
			error = std::format(
				"Reflection root '{}' does not expose descriptor set={}, binding={} required by '{}'.",
				ownerName,
				requiredBinding.set,
				requiredBinding.binding,
				requiredName);
			return;
		}

		if (ownerBinding->descriptorCount != requiredBinding.descriptorCount ||
			ownerBinding->isRuntimeSized != requiredBinding.isRuntimeSized ||
			ownerBinding->descriptorType != requiredBinding.descriptorType ||
			ownerBinding->bindingFlags != requiredBinding.bindingFlags ||
			!shaderStageFlagsCover(ownerBinding->stageFlags, requiredBinding.stageFlags))
		{
			error = std::format(
				"Reflection root '{}' has an incompatible descriptor at set={}, binding={} required by '{}'.",
				ownerName,
				requiredBinding.set,
				requiredBinding.binding,
				requiredName);
		}
	});
	if (error.has_value())
	{
		return error;
	}

	std::ranges::for_each(required.pushConstantRanges, [&](const ShaderPushConstantAbiRange &requiredRange) {
		if (error.has_value())
		{
			return;
		}

		auto const requiredEnd = static_cast<std::uint64_t>(requiredRange.offset) + requiredRange.size;
		auto ownerRange = std::ranges::find_if(owner.pushConstantRanges, [&](const ShaderPushConstantAbiRange &candidate) {
			auto const candidateEnd = static_cast<std::uint64_t>(candidate.offset) + candidate.size;
			return candidate.offset <= requiredRange.offset &&
			       candidateEnd >= requiredEnd &&
			       shaderStageFlagsCover(candidate.stageFlags, requiredRange.stageFlags);
		});
		if (ownerRange == owner.pushConstantRanges.end())
		{
			error = std::format(
				"Reflection root '{}' does not cover push-constant bytes [{}, {}) required by '{}'.",
				ownerName,
				requiredRange.offset,
				requiredEnd,
				requiredName);
		}
	});
	return error;
}

void assertReflectionLayoutCoverage(
	const ShaderLayoutAbiSignature &ownerSignature,
	const SlangProgram &ownerProgram,
	const SlangProgram &requiredProgram,
	const DescriptorBindingPolicy &policy)
{
	auto requiredLayout = ShaderDescriptorLayout::create(requiredProgram, policy);
	nrAssert(requiredLayout.valid(), "Pipeline reflection coverage validation requires a valid required-stage layout.");
	auto validation = validateReflectionLayoutCoverage(
		ownerSignature,
		requiredLayout.abiSignature(),
		programDebugName(ownerProgram),
		programDebugName(requiredProgram));
	nrAssert(!validation.has_value(), validation.value_or(std::string{}));
}
} // namespace

[[nodiscard]] CursorPipelineLayout CursorPipelineLayout::create(
				const vk::raii::Device &device,
				const ShaderDescriptorLayout &descriptorLayout,
				std::span<const SlangImmutableSamplerBinding> immutableSamplers)
{
				CursorPipelineLayout layout;
				layout.device_ = std::cref(device);

				auto setLayouts = descriptorLayout.descriptorSets();
				layout.immutableSamplerBindings_.reserve(immutableSamplers.size());

				auto findDescriptorBinding = [setLayouts](std::uint32_t setIndex, std::uint32_t bindingIndex) -> const DescriptorBindingInfo * {
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
							"CursorPipelineLayout::create immutable sampler target must be sampler/combined-image-sampler at set={}, binding={}, descriptorType={}",
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
					state.samplers.reserve(1u);

					auto samplerDebugName = std::format("immutable_sampler_s{}_b{}", state.set, state.binding);
					state.samplers.push_back(SlangSampler::create(device, immutableSamplerBinding.samplerDesc, samplerDebugName));
					nrAssert(state.samplers.back().valid(), std::format("CursorPipelineLayout::create failed to create immutable sampler '{}'.", samplerDebugName));
					state.rawSamplers.resize(immutableSamplerBinding.descriptorCount, state.samplers.back().raw());

					layout.immutableSamplerBindings_.push_back(std::move(state));
				});

				auto setInfoByIndex = std::map<std::uint32_t, std::reference_wrapper<const DescriptorSetLayoutInfo>>{};
				std::ranges::for_each(setLayouts, [&](const DescriptorSetLayoutInfo &setInfo) {
						setInfoByIndex.emplace(setInfo.set, std::cref(setInfo));
				});

				auto maxSetIndex = setInfoByIndex.empty() ? std::uint32_t{0} : setInfoByIndex.rbegin()->first;
				auto pipelineSetLayoutCount = setInfoByIndex.empty() ? std::size_t{0} : static_cast<std::size_t>(maxSetIndex) + 1u;
				layout.setLayouts_.reserve(pipelineSetLayoutCount);

				std::vector<vk::DescriptorSetLayout> pipelineSetLayouts;
				pipelineSetLayouts.reserve(pipelineSetLayoutCount);

				auto makeSetLayout = [&](std::uint32_t setIndex, const DescriptorSetLayoutInfo *setInfo) {
						auto bindings = setInfo ? descriptorLayout.makeVkSetLayoutBindings(setIndex) : std::vector<vk::DescriptorSetLayoutBinding>{};
						auto bindingFlags = setInfo ? descriptorLayout.makeVkSetLayoutBindingFlags(setIndex) : std::vector<vk::DescriptorBindingFlags>{};
						std::ranges::for_each(bindings, [&](vk::DescriptorSetLayoutBinding &binding) {
							auto immutableSamplerIt = std::ranges::find_if(layout.immutableSamplerBindings_, [&](ImmutableSamplerBindingState &state) {
								return state.set == setIndex && state.binding == binding.binding;
							});
							if (immutableSamplerIt == std::ranges::end(layout.immutableSamplerBindings_))
							{
								return;
							}

							nrAssert(
								binding.descriptorCount == immutableSamplerIt->rawSamplers.size(),
								std::format(
									"CursorPipelineLayout::create immutable sampler descriptorCount mismatch at set={}, binding={}, layoutCount={}, immutableCount={}",
									setIndex,
									binding.binding,
									binding.descriptorCount,
									immutableSamplerIt->rawSamplers.size()));

							binding.pImmutableSamplers = immutableSamplerIt->rawSamplers.data();
							immutableSamplerIt->isApplied = true;
						});

						vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
						vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
						setLayoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
						setLayoutInfo.pBindings = bindings.data();
						if (!bindingFlags.empty())
						{
							bindingFlagsInfo.bindingCount = static_cast<std::uint32_t>(bindingFlags.size());
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
								.set = setIndex,
								.layout = vk::raii::DescriptorSetLayout(device, setLayoutInfo),
								.isPlaceholder = setInfo == nullptr,
						});
						pipelineSetLayouts.push_back(*layout.setLayouts_.back().layout);
				};

				if (!setInfoByIndex.empty())
				{
					std::ranges::for_each(std::views::iota(std::uint32_t{0}, maxSetIndex + 1u), [&](std::uint32_t setIndex) {
						auto setIt = setInfoByIndex.find(setIndex);
						makeSetLayout(
							setIndex,
							setIt != setInfoByIndex.end() ? std::addressof(setIt->second.get()) : nullptr);
					});
				}

				std::ranges::for_each(layout.immutableSamplerBindings_, [](const ImmutableSamplerBindingState &state) {
					nrAssert(
						state.isApplied,
						std::format(
							"CursorPipelineLayout::create immutable sampler binding was not used by descriptor layout at set={}, binding={}",
							state.set,
							state.binding));
				});

				vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
				pipelineLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(pipelineSetLayouts.size());
				pipelineLayoutInfo.pSetLayouts = pipelineSetLayouts.data();
				auto pushConstantRanges = descriptorLayout.makeVkPushConstantRanges();
				std::ranges::for_each(pushConstantRanges, [](const vk::PushConstantRange &range) {
					nrAssert(
						range.size <= kMaxPushConstantBytes,
						std::format(
							"CursorPipelineLayout::create push constant range exceeds hard limit. size={} max={}",
							range.size,
							kMaxPushConstantBytes));
				});
				pipelineLayoutInfo.pushConstantRangeCount = static_cast<std::uint32_t>(pushConstantRanges.size());
				pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
				layout.pipelineLayout_ = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
				return layout;
		}

[[nodiscard]] bool CursorPipelineLayout::valid() const noexcept
{ return *pipelineLayout_ != nullptr; }

[[nodiscard]] vk::PipelineLayout CursorPipelineLayout::raw() const noexcept
{ return valid() ? *pipelineLayout_ : vk::PipelineLayout{}; }

[[nodiscard]] std::optional<vk::DescriptorSetLayout> CursorPipelineLayout::descriptorSetLayout(std::uint32_t setIndex) const noexcept
{
				auto it = std::ranges::find_if(setLayouts_, [setIndex](const DescriptorSetLayoutHandle &handle) {
						return handle.set == setIndex && !handle.isPlaceholder;
				});
				if (it == std::ranges::end(setLayouts_))
				{
						return std::nullopt;
				}
				return *it->layout;
		}

[[nodiscard]] std::vector<std::uint32_t> CursorPipelineLayout::setIndices() const
{
				return setLayouts_ |
				       std::views::filter([](const DescriptorSetLayoutHandle &handle) { return !handle.isPlaceholder; }) |
				       std::views::transform([](const DescriptorSetLayoutHandle &handle) { return handle.set; }) |
				       std::ranges::to<std::vector>();
		}

void CursorPipelineLayout::pushConstants(
				const vk::raii::CommandBuffer &commandBuffer,
				vk::ShaderStageFlags stageFlags,
				std::uint32_t offset,
				std::span<const std::uint8_t> bytes) const
{
			nrAssert(valid(), "CursorPipelineLayout::pushConstants requires a valid pipeline layout.");
			nrAssert(*commandBuffer != nullptr, "CursorPipelineLayout::pushConstants requires a valid command buffer.");
			if (bytes.empty())
			{
				return;
			}
			nrAssert(
				bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
				std::format("CursorPipelineLayout::pushConstants payload too large: {} bytes", bytes.size()));
			nrAssert(
				static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(bytes.size()) <= kMaxPushConstantBytes,
				std::format(
					"CursorPipelineLayout::pushConstants write exceeds hard limit. offset={}, size={}, max={}",
					offset,
					bytes.size(),
					kMaxPushConstantBytes));
			commandBuffer.pushConstants(raw(), stageFlags, offset, vk::ArrayProxy<const std::uint8_t>(static_cast<std::uint32_t>(bytes.size()), bytes.data()));
		}

void CursorPipelineLayout::pushConstants(
				const vk::raii::CommandBuffer &commandBuffer,
				const ShaderCursor &cursor,
				std::span<const std::uint8_t> bytes) const
{
			nrAssert(valid(), "CursorPipelineLayout::pushConstants requires a valid pipeline layout.");
			nrAssert(cursor.valid(), "CursorPipelineLayout::pushConstants requires a valid shader cursor.");
			nrAssert(*commandBuffer != nullptr, "CursorPipelineLayout::pushConstants requires a valid command buffer.");

			auto pushConstantRange = cursor.pushConstantRange();
			nrAssert(
				pushConstantRange.has_value(),
				"CursorPipelineLayout::pushConstants requires cursor to reference push-constant storage.");

			auto cursorOffset = cursor.address().uniformOffset;
			nrAssert(
				cursorOffset <= std::numeric_limits<std::uint32_t>::max(),
				std::format("CursorPipelineLayout::pushConstants cursor offset overflow: {}", cursorOffset));

			auto offset = static_cast<std::uint32_t>(cursorOffset);
			auto rangeBegin = static_cast<std::uint64_t>(pushConstantRange->offset);
			auto rangeEnd = rangeBegin + static_cast<std::uint64_t>(pushConstantRange->size);
			auto writeBegin = static_cast<std::uint64_t>(offset);
			auto writeEnd = writeBegin + static_cast<std::uint64_t>(bytes.size());

			nrAssert(
				writeBegin >= rangeBegin && writeEnd <= rangeEnd,
				std::format(
					"CursorPipelineLayout::pushConstants write outside push-constant range. offset={}, size={}, rangeBegin={}, rangeEnd={}",
					offset,
					bytes.size(),
					pushConstantRange->offset,
					pushConstantRange->offset + pushConstantRange->size));

			pushConstants(commandBuffer, pushConstantRange->stageFlags, offset, bytes);
		}

void CursorPipelineLayout::bindDescriptorSet(
		const vk::raii::CommandBuffer &commandBuffer,
		vk::PipelineBindPoint bindPoint,
		const ShaderBindingSet &set,
		std::span<const std::uint32_t> dynamicOffsets) const
{
	nrAssert(valid(), "CursorPipelineLayout::bindDescriptorSet requires a valid pipeline layout.");
	nrAssert(set.valid(), std::format("CursorPipelineLayout::bindDescriptorSet received invalid set {}.", set.setIndex()));
	nrAssert(*commandBuffer != nullptr, "CursorPipelineLayout::bindDescriptorSet requires a valid command buffer.");

	auto handle = set.raw();
	commandBuffer.bindDescriptorSets(bindPoint, raw(), set.setIndex(), {handle}, dynamicOffsets);
}

void CursorPipelineLayout::bindDescriptorSets(
		const vk::raii::CommandBuffer &commandBuffer,
		vk::PipelineBindPoint bindPoint,
		std::span<const ShaderBindingSet> sets,
		std::span<const std::uint32_t> dynamicOffsets) const
{
	nrAssert(valid(), "CursorPipelineLayout::bindDescriptorSets requires a valid pipeline layout.");
	nrAssert(*commandBuffer != nullptr, "CursorPipelineLayout::bindDescriptorSets requires a valid command buffer.");
	nrAssert(
		dynamicOffsets.empty(),
		"CursorPipelineLayout::bindDescriptorSets with multiple sets does not accept shared dynamic offsets. Bind per-set when using dynamic offsets.");

	auto runFirstSetIndex = std::optional<std::uint32_t>{};
	auto runDescriptorSets = std::vector<vk::DescriptorSet>{};
	runDescriptorSets.reserve(sets.size());

	auto flushRun = [&]() {
		if (!runFirstSetIndex.has_value() || runDescriptorSets.empty())
		{
			return;
		}

		commandBuffer.bindDescriptorSets(bindPoint, raw(), *runFirstSetIndex, runDescriptorSets, {});
		runFirstSetIndex.reset();
		runDescriptorSets.clear();
	};

	std::ranges::for_each(sets, [&](const ShaderBindingSet &set) {
		if (!set.valid())
		{
			flushRun();
			return;
		}

		auto const setIndex = set.setIndex();
		if (!runFirstSetIndex.has_value())
		{
			runFirstSetIndex = setIndex;
		}
		else
		{
			auto const expectedSetIndex = *runFirstSetIndex + static_cast<std::uint32_t>(runDescriptorSets.size());
			if (setIndex != expectedSetIndex)
			{
				flushRun();
				runFirstSetIndex = setIndex;
			}
		}

		runDescriptorSets.push_back(set.raw());
	});
	flushRun();
}

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(
	const CursorPipelineLayout &layout,
	ShaderBindingPool &pool,
	const std::map<std::uint32_t, std::uint32_t> &variableDescriptorCountsBySet)
{
	nrAssert(layout.valid(), "allocateBindingSetsForLayout requires a valid cursor pipeline layout.");

	auto setIndices = layout.setIndices();
	auto sets = std::vector<ShaderBindingSet>{};
	sets.reserve(setIndices.size());

	std::ranges::for_each(setIndices, [&](std::uint32_t setIndex) {
		auto descriptorSetLayout = layout.descriptorSetLayout(setIndex);
		nrAssert(
			descriptorSetLayout.has_value(),
			std::format("allocateBindingSetsForLayout missing descriptor set layout for set {}.", setIndex));

		auto requestedVariableCount = variableDescriptorCountsBySet.find(setIndex);
		auto set = pool.allocate(
			*descriptorSetLayout,
			setIndex,
			requestedVariableCount != variableDescriptorCountsBySet.end()
				? std::optional<std::uint32_t>(requestedVariableCount->second)
				: std::nullopt);
		nrAssert(
			set.valid(),
			std::format("allocateBindingSetsForLayout failed to allocate descriptor set for set {}.", setIndex));
		sets.push_back(set);
	});

	return sets;
}

std::vector<ShaderBindingSet> allocateBindingSetsForLayout(const CursorPipelineLayout &layout, ShaderBindingPool &pool)
{
	return allocateBindingSetsForLayout(layout, pool, {});
}

void updateResourcesForBindingSnapshot(
	ShaderBindingPool &pool,
	std::span<const ShaderBindingSet> sets,
	DescriptorWriteCache &descriptorWriteCache,
	const ShaderBindingSnapshot &snapshot,
	LogicalDescriptorResolver logicalResolver)
{
	auto writeRequests = resolveDescriptorWriteRequests(snapshot, std::move(logicalResolver));
	auto changedWriteRequests = filterChangedDescriptorWrites(descriptorWriteCache, writeRequests);
	if (!changedWriteRequests.empty())
	{
		auto requestsBySet = std::map<std::uint32_t, std::vector<DescriptorWriteRequest>>{};
		std::ranges::for_each(changedWriteRequests, [&](const DescriptorWriteRequest &request) {
			requestsBySet[request.binding.set].push_back(request);
		});

		std::ranges::for_each(sets, [&](const ShaderBindingSet &set) {
			if (!set.valid())
			{
				return;
			}

			auto it = requestsBySet.find(set.setIndex());
			if (it == requestsBySet.end())
			{
				return;
			}

			pool.update(set, it->second);
			requestsBySet.erase(it);
		});

		nrAssert(
			requestsBySet.empty(),
			"updateResourcesForBindingSnapshot could not find descriptor sets for one or more snapshot writes.");
		commitDescriptorWrites(descriptorWriteCache, changedWriteRequests);
	}
}

void bindPreparedResourcesToCommandBuffer(
	const vk::raii::CommandBuffer &commandBuffer,
	vk::PipelineBindPoint bindPoint,
	const CursorPipelineLayout &layout,
	std::span<const ShaderBindingSet> sets)
{
	nrAssert(layout.valid(), "bindPreparedResourcesToCommandBuffer requires a valid cursor pipeline layout.");
	nrAssert(*commandBuffer != nullptr, "bindPreparedResourcesToCommandBuffer requires a valid command buffer.");

	if (!sets.empty())
	{
		layout.bindDescriptorSets(commandBuffer, bindPoint, sets);
	}
}

void pushConstantsToCommandBuffer(
	const vk::raii::CommandBuffer &commandBuffer,
	const CursorPipelineLayout &layout,
	const ShaderBindingSnapshot &snapshot)
{
	nrAssert(layout.valid(), "pushConstantsToCommandBuffer requires a valid cursor pipeline layout.");
	nrAssert(*commandBuffer != nullptr, "pushConstantsToCommandBuffer requires a valid command buffer.");

	std::ranges::for_each(snapshot.pushConstantWrites(), [&](const PushConstantWriteRecord &record) {
		if (record.data.empty())
		{
			return;
		}

		layout.pushConstants(
			commandBuffer,
			record.range.stageFlags,
			record.offset,
			std::span<const std::uint8_t>{record.data.data(), record.data.size()});
	});
}

void setShaderModuleDebugName(const vk::raii::Device &device, const vk::raii::ShaderModule &shaderModule, std::string_view name)
{
	if constexpr (gpuDebugNamesEnabled)
	{
		if (name.empty())
		{
			return;
		}

		auto debugName = std::string{name};
		vk::DebugUtilsObjectNameInfoEXT objectNameInfo{};
		objectNameInfo.objectType = vk::ObjectType::eShaderModule;
		const auto rawHandle = *shaderModule;
		static_assert(sizeof(rawHandle) == sizeof(std::uint64_t), "VkShaderModule handle size must match std::uint64_t for debug naming.");
		objectNameInfo.objectHandle = std::bit_cast<std::uint64_t>(rawHandle);
		objectNameInfo.pObjectName = debugName.c_str();
		try
		{
			device.setDebugUtilsObjectNameEXT(objectNameInfo);
		}
		catch (const vk::SystemError &error)
		{
			auto errorText = std::string_view{error.what()};
			nrInfo<LogLevel::error>(std::vformat(
				"setShaderModuleDebugName failed to set debug name '{}': {}",
				std::make_format_args(debugName, errorText)));
			nrAssert(false, "setShaderModuleDebugName failed to set a Vulkan debug object name.");
		}
	}
}

[[nodiscard]] std::string makeShaderModuleDebugName(const SlangEntryPointData &entryPoint)
{
	if (entryPoint.debugName.empty())
	{
		return entryPoint.entryPointName.empty() ? std::string{"shader"} : entryPoint.entryPointName;
	}

	return entryPoint.debugName;
}

void VkShaderProgram::appendStage(
	VkShaderProgram &program,
	const vk::raii::Device &device,
	const SlangEntryPointData &entryPoint,
	std::string logicalEntryPointName)
{
	nrAssert(entryPoint.valid(), std::format("Entry point '{}' has no valid SPIR-V artifact.", entryPoint.entryPointName));

	vk::ShaderModuleCreateInfo moduleInfo{};
	moduleInfo.codeSize = entryPoint.spirv->size() * sizeof(std::uint32_t);
	moduleInfo.pCode = entryPoint.spirv->data();
	program.modules_.emplace_back(device, moduleInfo);
	setShaderModuleDebugName(device, program.modules_.back(), makeShaderModuleDebugName(entryPoint));

	program.shaderEntryPointNames_.push_back(entryPoint.entryPointName);
	program.logicalEntryPointNames_.push_back(std::move(logicalEntryPointName));
	program.stages_.push_back(entryPoint.stage);

	auto stageInfo = vk::PipelineShaderStageCreateInfo{};
	stageInfo.stage = toVkShaderStage(entryPoint.stage);
	stageInfo.module = *program.modules_.back();
	stageInfo.pName = program.shaderEntryPointNames_.back().c_str();
	program.stageCreateInfos_.push_back(stageInfo);
}

[[nodiscard]] VkShaderProgram VkShaderProgram::create(
	const vk::raii::Device &device,
	std::span<const SlangProgram> programs)
{
	nrAssert(!programs.empty(), "VkShaderProgram::create requires at least one single-entry program.");

	VkShaderProgram result;
	result.modules_.reserve(programs.size());
	result.shaderEntryPointNames_.reserve(programs.size());
	result.logicalEntryPointNames_.reserve(programs.size());
	result.stages_.reserve(programs.size());
	result.stageCreateInfos_.reserve(programs.size());
	std::ranges::for_each(programs, [&](const SlangProgram &program) {
		auto const *entryPoint = program.entryPoint();
		nrAssert(entryPoint != nullptr, "VkShaderProgram::create received an invalid single-entry program.");
		appendStage(result, device, *entryPoint, entryPoint->entryPointName);
	});
	return result;
}

[[nodiscard]] VkShaderProgram VkShaderProgram::create(const vk::raii::Device &device, std::span<const RayTracingPipelineStageSelection> selectedEntryPoints)
{
	nrAssert(!selectedEntryPoints.empty(), "VkShaderProgram::create requires at least one selected entrypoint.");

	VkShaderProgram result;
	result.modules_.reserve(selectedEntryPoints.size());
	result.shaderEntryPointNames_.reserve(selectedEntryPoints.size());
	result.logicalEntryPointNames_.reserve(selectedEntryPoints.size());
	result.stages_.reserve(selectedEntryPoints.size());
	result.stageCreateInfos_.reserve(selectedEntryPoints.size());
	std::ranges::for_each(selectedEntryPoints, [&](const RayTracingPipelineStageSelection &selection) {
		auto const *entryPoint = selection.program.get().entryPoint();
		nrAssert(entryPoint != nullptr, "VkShaderProgram::create received an invalid single-entry RT program.");
		appendStage(result, device, *entryPoint, selection.logicalEntryPointName);
	});
	return result;
}

[[nodiscard]] bool VkShaderProgram::valid() const noexcept
{ return !stageCreateInfos_.empty(); }

[[nodiscard]] const vk::PipelineShaderStageCreateInfo &VkShaderProgram::stageCreateInfo(std::uint32_t index) const noexcept
{ return stageCreateInfos_[index]; }

[[nodiscard]] std::span<const SlangStage> VkShaderProgram::stages() const noexcept
{ return stages_; }

[[nodiscard]] std::span<const std::string> VkShaderProgram::logicalEntryPointNames() const noexcept
{ return logicalEntryPointNames_; }

[[nodiscard]] GraphicsPipeline GraphicsPipeline::create(
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
				const VkShaderProgram &shaderProgram,
				const GraphicsPipelineDesc &desc,
				const vk::raii::PipelineCache *pipelineCache)
{
				nrAssert(
					!desc.colorAttachmentFormats.empty() || desc.depthAttachmentFormat.has_value() || desc.stencilAttachmentFormat.has_value(),
					"GraphicsPipeline::create requires at least one attachment format when using dynamic rendering.");
				nrAssert(layout.valid(), "GraphicsPipeline::create requires a valid pipeline layout.");
				nrAssert(shaderProgram.valid(), "GraphicsPipeline::create requires a valid shader program.");

				auto graphicsStageIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(shaderProgram.stages().size())) |
				                          std::views::filter([&](std::uint32_t index) { return detail::isGraphicsStage(shaderProgram.stages()[index]); }) |
				                          std::ranges::to<std::vector>();
				nrAssert(!graphicsStageIndices.empty(), "GraphicsPipeline::create requires at least one graphics shader stage.");

				auto stageCreateInfos = graphicsStageIndices |
				                       std::views::transform([&](std::uint32_t stageIndex) { return shaderProgram.stageCreateInfo(stageIndex); }) |
				                       std::ranges::to<std::vector>();

				auto hasStage = [&](SlangStage stage) {
					return std::ranges::any_of(
						graphicsStageIndices,
						[&](std::uint32_t stageIndex) { return shaderProgram.stages()[stageIndex] == stage; });
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
				vertexInputInfo.vertexBindingDescriptionCount = static_cast<std::uint32_t>(desc.vertexBindings.size());
				vertexInputInfo.pVertexBindingDescriptions = desc.vertexBindings.empty() ? nullptr : desc.vertexBindings.data();
				vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(desc.vertexAttributes.size());
				vertexInputInfo.pVertexAttributeDescriptions = desc.vertexAttributes.empty() ? nullptr : desc.vertexAttributes.data();
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
				colorBlendInfo.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
				colorBlendInfo.pAttachments = colorBlendAttachments.data();

				vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
				dynamicStateInfo.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
				dynamicStateInfo.pDynamicStates = dynamicStates.data();

				auto renderingColorFormats = desc.colorAttachmentFormats;
				vk::PipelineRenderingCreateInfo renderingInfo{};
				renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(renderingColorFormats.size());
				renderingInfo.pColorAttachmentFormats = renderingColorFormats.data();
				renderingInfo.depthAttachmentFormat = desc.depthAttachmentFormat.value_or(vk::Format::eUndefined);
				renderingInfo.stencilAttachmentFormat = desc.stencilAttachmentFormat.value_or(vk::Format::eUndefined);

				vk::GraphicsPipelineCreateInfo createInfo{};
				createInfo.flags = desc.flags;
				createInfo.stageCount = static_cast<std::uint32_t>(stageCreateInfos.size());
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
				createInfo.pNext = &renderingInfo;

				GraphicsPipeline pipeline;
				auto pipelineCacheOptional =
						pipelineCache != nullptr ? vk::Optional<const vk::raii::PipelineCache>(*pipelineCache) : vk::Optional<const vk::raii::PipelineCache>(nullptr);
				pipeline.pipeline_ = vk::raii::Pipeline(device, pipelineCacheOptional, createInfo);
				return pipeline;
		}

[[nodiscard]] bool GraphicsPipeline::valid() const noexcept
{ return *pipeline_ != nullptr; }

[[nodiscard]] vk::Pipeline GraphicsPipeline::raw() const noexcept
{ return valid() ? *pipeline_ : vk::Pipeline{}; }

[[nodiscard]] ComputePipeline ComputePipeline::create(
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
				const VkShaderProgram &shaderProgram,
				const ComputePipelineDesc &desc,
				const vk::raii::PipelineCache *pipelineCache)
{
				nrAssert(layout.valid(), "ComputePipeline::create requires a valid pipeline layout.");
				nrAssert(shaderProgram.valid(), "ComputePipeline::create requires a valid shader program.");

				auto indices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(shaderProgram.stages().size()));
				auto it = std::ranges::find_if(indices, [&](std::uint32_t index) { return shaderProgram.stages()[index] == SLANG_STAGE_COMPUTE; });
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

[[nodiscard]] bool ComputePipeline::valid() const noexcept
{ return *pipeline_ != nullptr; }

[[nodiscard]] vk::Pipeline ComputePipeline::raw() const noexcept
{ return valid() ? *pipeline_ : vk::Pipeline{}; }

[[nodiscard]] RayTracingPipeline RayTracingPipeline::create(
				const vk::raii::Device &device,
				const CursorPipelineLayout &layout,
				const VkShaderProgram &shaderProgram,
				const RayTracingPipelineDesc &desc,
				std::span<const RayTracingShaderGroupDesc> groupDescs,
				const vk::raii::PipelineCache *pipelineCache)
{
				nrAssert(layout.valid(), "RayTracingPipeline::create requires a valid pipeline layout.");
				nrAssert(shaderProgram.valid(), "RayTracingPipeline::create requires a valid shader program.");
				nrAssert(desc.maxRayRecursionDepth > 0u, "RayTracingPipeline::create requires maxRayRecursionDepth > 0.");
				nrAssert(!groupDescs.empty(), "RayTracingPipeline::create requires at least one named shader group.");
				nrAssert(
					groupDescs.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
					"RayTracingPipeline::create shader group count exceeds uint32 ABI.");
				auto descValidation = validateRayTracingPipelineDesc(desc);
				nrAssert(!descValidation.has_value(), std::format("RayTracingPipeline::create invalid desc: {}", descValidation.value_or(std::string{})));

				auto createFlags = desc.flags;
				if (desc.createAsLibrary)
				{
					createFlags |= vk::PipelineCreateFlags{VK_PIPELINE_CREATE_LIBRARY_BIT_KHR};
				}

				auto hasCaptureReplayHandles = std::ranges::any_of(groupDescs, [](const RayTracingShaderGroupDesc &groupDesc) {
					return !groupDesc.captureReplayHandle.empty();
				});
				if (hasCaptureReplayHandles)
				{
					createFlags |= vk::PipelineCreateFlagBits::eRayTracingShaderGroupHandleCaptureReplayKHR;
				}

				auto rtStageIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(shaderProgram.stages().size())) |
				                  std::views::filter([&](std::uint32_t index) { return detail::isRayTracingStage(shaderProgram.stages()[index]); }) |
				                  std::ranges::to<std::vector>();
				nrAssert(!rtStageIndices.empty(), "RayTracingPipeline::create requires at least one ray tracing shader stage.");

				auto stageCreateInfos = rtStageIndices |
				                       std::views::transform([&](std::uint32_t stageIndex) { return shaderProgram.stageCreateInfo(stageIndex); }) |
				                       std::ranges::to<std::vector>();

				constexpr std::uint32_t shaderUnused = std::numeric_limits<std::uint32_t>::max();
				auto groups = std::vector<vk::RayTracingShaderGroupCreateInfoKHR>{};

				auto findLocalStageIndex = [&](std::string_view entryPointName, std::initializer_list<SlangStage> expectedStages) {
					if (entryPointName.empty())
					{
						return shaderUnused;
					}
					auto it = std::ranges::find(shaderProgram.logicalEntryPointNames(), entryPointName);
					nrAssert(it != std::ranges::end(shaderProgram.logicalEntryPointNames()), std::format("RayTracingPipeline::create unknown logical entrypoint '{}' in custom group.", entryPointName));
					auto globalIndex = static_cast<std::uint32_t>(std::distance(std::ranges::begin(shaderProgram.logicalEntryPointNames()), it));
					auto localIt = std::ranges::find(rtStageIndices, globalIndex);
					nrAssert(localIt != std::ranges::end(rtStageIndices), std::format("RayTracingPipeline::create entrypoint '{}' is not a RT stage.", entryPointName));
					auto stage = shaderProgram.stages()[globalIndex];
					nrAssert(
						std::ranges::find(expectedStages, stage) != expectedStages.end(),
						std::format("RayTracingPipeline::create entrypoint '{}' stage mismatch for custom group.", entryPointName));
					return static_cast<std::uint32_t>(std::distance(std::ranges::begin(rtStageIndices), localIt));
				};

				groups.reserve(groupDescs.size());
				std::ranges::for_each(groupDescs, [&](const RayTracingShaderGroupDesc &groupDesc) {
					vk::RayTracingShaderGroupCreateInfoKHR group{};
					group.type = groupDesc.type;
					group.generalShader = findLocalStageIndex(groupDesc.generalEntryPoint, {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CALLABLE});
					group.closestHitShader = findLocalStageIndex(groupDesc.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT});
					group.anyHitShader = findLocalStageIndex(groupDesc.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT});
					group.intersectionShader = findLocalStageIndex(groupDesc.intersectionEntryPoint, {SLANG_STAGE_INTERSECTION});
					if (!groupDesc.captureReplayHandle.empty())
					{
						group.pShaderGroupCaptureReplayHandle = groupDesc.captureReplayHandle.data();
					}
					groups.push_back(group);
				});

				auto dynamicStates = std::array{vk::DynamicState::eRayTracingPipelineStackSizeKHR};
				vk::PipelineDynamicStateCreateInfo dynamicStateInfo{};
				if (desc.dynamicPipelineStackSize)
				{
					dynamicStateInfo.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
					dynamicStateInfo.pDynamicStates = dynamicStates.data();
				}

				vk::RayTracingPipelineCreateInfoKHR createInfo{};
				createInfo.flags = createFlags;
				createInfo.stageCount = static_cast<std::uint32_t>(stageCreateInfos.size());
				createInfo.pStages = stageCreateInfos.data();
				createInfo.groupCount = static_cast<std::uint32_t>(groups.size());
				createInfo.pGroups = groups.data();
				createInfo.maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth;
				createInfo.layout = layout.raw();
				createInfo.pDynamicState = desc.dynamicPipelineStackSize ? &dynamicStateInfo : nullptr;

				vk::PipelineLibraryCreateInfoKHR libraryInfo{};
				if (!desc.linkedLibraries.empty())
				{
					libraryInfo.libraryCount = static_cast<std::uint32_t>(desc.linkedLibraries.size());
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
				auto deferredOperation = desc.deferredOperation.has_value()
				                             ? vk::Optional<const vk::raii::DeferredOperationKHR>(desc.deferredOperation->get())
				                             : vk::Optional<const vk::raii::DeferredOperationKHR>(nullptr);
				auto pipelineCacheOptional =
						pipelineCache != nullptr ? vk::Optional<const vk::raii::PipelineCache>(*pipelineCache) : vk::Optional<const vk::raii::PipelineCache>(nullptr);
				pipeline.pipeline_ = vk::raii::Pipeline(device, deferredOperation, pipelineCacheOptional, createInfo);
				pipeline.device_ = std::cref(device);
				pipeline.shaderGroupCount_ = static_cast<std::uint32_t>(groups.size());
				auto groupIndices = std::views::iota(std::size_t{0u}, groupDescs.size());
				std::ranges::for_each(groupIndices, [&](std::size_t groupIndex) {
					nrAssert(
						groupIndex <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
						"RayTracingPipeline::create group index exceeds uint32 ABI.");
					auto const [_, inserted] = pipeline.shaderGroupIndices_.emplace(
						groupDescs[groupIndex].name,
						static_cast<std::uint32_t>(groupIndex));
					nrAssert(inserted, "RayTracingPipeline::create requires unique shader group names.");
				});
				pipeline.dynamicPipelineStackSize_ = desc.dynamicPipelineStackSize;
				return pipeline;
		}

[[nodiscard]] bool RayTracingPipeline::valid() const noexcept
{ return *pipeline_ != nullptr; }

[[nodiscard]] vk::Pipeline RayTracingPipeline::raw() const noexcept
{ return valid() ? *pipeline_ : vk::Pipeline{}; }

[[nodiscard]] std::uint32_t RayTracingPipeline::shaderGroupCount() const noexcept
{ return shaderGroupCount_; }

[[nodiscard]] std::uint32_t RayTracingPipeline::shaderGroupIndex(std::string_view name) const
{
			auto const found = shaderGroupIndices_.find(std::string{name});
			nrAssert(
				found != shaderGroupIndices_.end(),
				std::format("RayTracingPipeline::shaderGroupIndex unknown group '{}'.", name));
			return found->second;
}

[[nodiscard]] bool RayTracingPipeline::dynamicPipelineStackSize() const noexcept
{ return dynamicPipelineStackSize_; }

[[nodiscard]] std::vector<std::uint8_t> RayTracingPipeline::shaderGroupHandles(std::uint32_t firstGroup, std::uint32_t groupCount, std::uint32_t handleSize) const
{
			nrAssert(valid(), "RayTracingPipeline::shaderGroupHandles requires a valid pipeline.");
			nrAssert(device_.has_value(), "RayTracingPipeline::shaderGroupHandles requires a valid device reference.");
			nrAssert(groupCount > 0u, "RayTracingPipeline::shaderGroupHandles requires groupCount > 0.");
			nrAssert(handleSize > 0u, "RayTracingPipeline::shaderGroupHandles requires handleSize > 0.");
			nrAssert(firstGroup < shaderGroupCount_, "RayTracingPipeline::shaderGroupHandles firstGroup is out of range.");
			auto requestedEnd = static_cast<std::uint64_t>(firstGroup) + static_cast<std::uint64_t>(groupCount);
			nrAssert(requestedEnd <= static_cast<std::uint64_t>(shaderGroupCount_), "RayTracingPipeline::shaderGroupHandles range exceeds group count.");

			auto dataSize = static_cast<std::size_t>(handleSize) * static_cast<std::size_t>(groupCount);
			return pipeline_.getRayTracingShaderGroupHandlesKHR<std::uint8_t>(firstGroup, groupCount, dataSize);
		}

[[nodiscard]] std::vector<std::uint8_t> RayTracingPipeline::captureReplayShaderGroupHandles(std::uint32_t firstGroup, std::uint32_t groupCount, std::uint32_t captureReplayHandleSize) const
{
			nrAssert(valid(), "RayTracingPipeline::captureReplayShaderGroupHandles requires a valid pipeline.");
			nrAssert(groupCount > 0u, "RayTracingPipeline::captureReplayShaderGroupHandles requires groupCount > 0.");
			nrAssert(captureReplayHandleSize > 0u, "RayTracingPipeline::captureReplayShaderGroupHandles requires captureReplayHandleSize > 0.");
			nrAssert(firstGroup < shaderGroupCount_, "RayTracingPipeline::captureReplayShaderGroupHandles firstGroup is out of range.");
			auto requestedEnd = static_cast<std::uint64_t>(firstGroup) + static_cast<std::uint64_t>(groupCount);
			nrAssert(requestedEnd <= static_cast<std::uint64_t>(shaderGroupCount_), "RayTracingPipeline::captureReplayShaderGroupHandles range exceeds group count.");

			auto dataSize = static_cast<std::size_t>(captureReplayHandleSize) * static_cast<std::size_t>(groupCount);
			return pipeline_.getRayTracingCaptureReplayShaderGroupHandlesKHR<std::uint8_t>(firstGroup, groupCount, dataSize);
		}

[[nodiscard]] vk::DeviceSize RayTracingPipeline::shaderGroupStackSize(std::uint32_t group, vk::ShaderGroupShaderKHR groupShader) const
{
			nrAssert(valid(), "RayTracingPipeline::shaderGroupStackSize requires a valid pipeline.");
			nrAssert(group < shaderGroupCount_, "RayTracingPipeline::shaderGroupStackSize group is out of range.");
			return pipeline_.getRayTracingShaderGroupStackSizeKHR(group, groupShader);
		}

void setPipelineDebugName(const vk::raii::Device &device, vk::Pipeline pipeline, std::string_view name)
{
	if constexpr (gpuDebugNamesEnabled)
	{
		if (pipeline == vk::Pipeline{} || name.empty())
		{
			return;
		}

		auto debugName = std::string{name};
		vk::DebugUtilsObjectNameInfoEXT objectNameInfo{};
		objectNameInfo.objectType = vk::ObjectType::ePipeline;
		const auto rawHandle = static_cast<VkPipeline>(pipeline);
		static_assert(sizeof(rawHandle) == sizeof(std::uint64_t), "VkPipeline handle size must match std::uint64_t for debug naming.");
		objectNameInfo.objectHandle = std::bit_cast<std::uint64_t>(rawHandle);
		objectNameInfo.pObjectName = debugName.c_str();
		try
		{
			device.setDebugUtilsObjectNameEXT(objectNameInfo);
		}
		catch (const vk::SystemError &error)
		{
			auto errorText = std::string_view{error.what()};
			nrInfo<LogLevel::error>(std::vformat(
				"setPipelineDebugName failed to set debug name '{}': {}",
				std::make_format_args(debugName, errorText)));
			nrAssert(false, "setPipelineDebugName failed to set a Vulkan debug object name.");
		}
	}
}

namespace
{
[[nodiscard]] std::filesystem::path pipelineCachePath(const PipelineCacheConfig &config)
{
	if (!config.persistent())
	{
		return {};
	}
	return config.directory / config.fileName;
}

[[nodiscard]] std::vector<std::uint8_t> readPipelineCacheBlob(const PipelineCacheConfig &config)
{
	auto const path = pipelineCachePath(config);
	if (path.empty())
	{
		return {};
	}

	auto error = std::error_code{};
	if (!std::filesystem::exists(path, error))
	{
		return {};
	}
	if (error)
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService failed to query pipeline cache path '{}': {}",
			path.string(),
			error.message()));
		return {};
	}

	if (std::filesystem::is_directory(path, error))
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService ignored pipeline cache path '{}' because it is a directory.",
			path.string()));
		return {};
	}

	auto stream = std::ifstream{path, std::ios::binary | std::ios::ate};
	if (!stream)
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService failed to open pipeline cache file '{}'.",
			path.string()));
		return {};
	}

	auto const size = stream.tellg();
	if (size <= std::streampos{0})
	{
		return {};
	}

	auto data = std::vector<std::uint8_t>(static_cast<std::size_t>(size));
	stream.seekg(0, std::ios::beg);
	stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
	if (!stream)
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService failed to read pipeline cache file '{}'.",
			path.string()));
		return {};
	}

	return data;
}

[[nodiscard]] bool writePipelineCacheBlob(
	const PipelineCacheConfig &config,
	std::span<const std::uint8_t> data)
{
	auto const path = pipelineCachePath(config);
	if (path.empty() || data.empty())
	{
		return false;
	}

	auto error = std::error_code{};
	std::filesystem::create_directories(path.parent_path(), error);
	if (error)
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService failed to create pipeline cache directory '{}': {}",
			path.parent_path().string(),
			error.message()));
		return false;
	}

	auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
	if (!stream)
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService failed to open pipeline cache file '{}' for writing.",
			path.string()));
		return false;
	}

	stream.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
	if (!stream)
	{
		nrInfo<LogLevel::warning>(std::format(
			"PipelineService failed to write pipeline cache file '{}'.",
			path.string()));
		return false;
	}

	return true;
}
} // namespace

void PipelineService::bindDevice(
		const vk::raii::Device &device,
		std::optional<std::reference_wrapper<const RayTracingCapabilitySnapshot>> rtCapabilities,
		PipelineCacheConfig cacheConfig)
{
		device_ = std::cref(device);
		cacheConfig_ = std::move(cacheConfig);
		if (rtCapabilities.has_value())
		{
			rtCapabilities_ = rtCapabilities->get();
		}
		else
		{
			rtCapabilities_.reset();
		}
		auto cacheBlob = readPipelineCacheBlob(cacheConfig_);
		vk::PipelineCacheCreateInfo cacheCreateInfo{};
		if (cacheConfig_.enabled && !cacheBlob.empty())
		{
			cacheCreateInfo.initialDataSize = cacheBlob.size();
			cacheCreateInfo.pInitialData = cacheBlob.data();
		}
		pipelineCache_ = cacheConfig_.enabled
			? vk::raii::PipelineCache(device, cacheCreateInfo)
			: vk::raii::PipelineCache{nullptr};
	}

[[nodiscard]] bool PipelineService::savePipelineCache() const
{
		if (!cacheConfig_.saveOnIdle || !cacheConfig_.persistent() || *pipelineCache_ == nullptr)
		{
			return false;
		}

		auto data = std::vector<std::uint8_t>{};
		try
		{
			data = pipelineCache_.getData();
		}
		catch (const vk::SystemError &error)
		{
			nrInfo<LogLevel::warning>(std::format(
				"PipelineService failed to export Vulkan pipeline cache data: {}",
				error.what()));
			return false;
		}

		return writePipelineCacheBlob(cacheConfig_, data);
	}

[[nodiscard]] ShaderBindingPool PipelineService::createBindingPool(const ShaderDescriptorLayout &descriptorLayout, ShaderBindingPoolConfig config) const
{
		nrAssert(device_.has_value(), "PipelineService::createBindingPool requires a bound logical device.");
		return ShaderBindingPool::create(device_->get(), descriptorLayout, config);
	}

[[nodiscard]] ShaderBindingSet PipelineService::allocateBindingSet(const CursorPipelineLayout &layout, ShaderBindingPool &bindingPool, std::uint32_t setIndex, std::optional<std::uint32_t> variableDescriptorCount) const
{
		nrAssert(device_.has_value(), "PipelineService::allocateBindingSet requires a bound logical device.");
		auto descriptorSetLayout = layout.descriptorSetLayout(setIndex);
		if (!descriptorSetLayout.has_value())
		{
			return {};
		}
		return bindingPool.allocate(*descriptorSetLayout, setIndex, variableDescriptorCount);
	}

[[nodiscard]] std::vector<ShaderBindingSet> PipelineService::allocateBindingSets(const CursorPipelineLayout &layout, ShaderBindingPool &bindingPool) const
{
		nrAssert(device_.has_value(), "PipelineService::allocateBindingSets requires a bound logical device.");
		return layout.setIndices() |
		       std::views::transform([&](std::uint32_t setIndex) { return allocateBindingSet(layout, bindingPool, setIndex); }) |
		       std::ranges::to<std::vector>();
	}

[[nodiscard]] SlangSampler PipelineService::createSampler(SlangSamplerDesc desc, std::string_view debugName) const
{
		nrAssert(device_.has_value(), "PipelineService::createSampler requires a bound logical device.");
		return SlangSampler::create(device_->get(), std::move(desc), debugName);
	}

[[nodiscard]] PipelineService::PipelineLayoutBundle PipelineService::createPipelineLayoutBundle(
		const SlangProgram &slangProgram,
		const DescriptorBindingPolicy &descriptorBindingPolicy,
		std::span<const SlangImmutableSamplerBinding> immutableSamplers) const
{
		const auto &device = device_->get();
		auto descriptorLayout = ShaderDescriptorLayout::create(slangProgram, descriptorBindingPolicy);
		auto layout = CursorPipelineLayout::create(device, descriptorLayout, immutableSamplers);
		return PipelineLayoutBundle{
			.descriptorLayout = std::move(descriptorLayout),
			.layout = std::move(layout),
		};
	}

[[nodiscard]] const vk::raii::PipelineCache *PipelineService::pipelineCacheOrNull() const noexcept
{
		return *pipelineCache_ != nullptr ? &pipelineCache_ : nullptr;
	}

[[nodiscard]] PipelineState<GraphicsPipeline> PipelineService::createGraphicsPipeline(
	std::span<const SlangProgram> programs,
	const GraphicsPipelineDesc &desc,
	std::uint32_t descriptorMaxSets,
	std::span<const SlangImmutableSamplerBinding> immutableSamplers) const
{
		nrAssert(device_.has_value(), "PipelineService::createGraphicsPipeline requires a bound logical device.");
		nrAssert(!programs.empty(), "PipelineService::createGraphicsPipeline requires at least one single-entry program.");
		nrAssert(
			std::ranges::all_of(programs, [](const SlangProgram &program) {
				auto const *entryPoint = program.entryPoint();
				return entryPoint && detail::isGraphicsStage(entryPoint->stage);
			}),
			"PipelineService::createGraphicsPipeline requires valid graphics-stage programs.");
		auto uniqueStages = std::set<SlangStage>{};
		nrAssert(
			std::ranges::all_of(programs, [&](const SlangProgram &program) {
				return uniqueStages.insert(program.entryPoint()->stage).second;
			}),
			"PipelineService::createGraphicsPipeline requires at most one program for each graphics stage.");
		auto const &reflectionProgram = programs.front();

		const auto &device = device_->get();
		auto effectiveDesc = desc;
		auto layoutBundle = createPipelineLayoutBundle(reflectionProgram, effectiveDesc.descriptorBindingPolicy, immutableSamplers);
		auto const reflectionSignature = layoutBundle.descriptorLayout.abiSignature();
		std::ranges::for_each(programs | std::views::drop(1), [&](const SlangProgram &program) {
			assertReflectionLayoutCoverage(
				reflectionSignature,
				reflectionProgram,
				program,
				effectiveDesc.descriptorBindingPolicy);
		});

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

		auto shaderProgram = VkShaderProgram::create(device, programs);
		auto pipeline = GraphicsPipeline::create(device, layoutBundle.layout, shaderProgram, effectiveDesc, pipelineCacheOrNull());
		auto state = makePipelineState(reflectionProgram, std::move(layoutBundle), descriptorMaxSets, std::move(pipeline));
		state.graphicsDesc = std::move(effectiveDesc);
		return state;
	}

[[nodiscard]] PipelineState<ComputePipeline> PipelineService::createComputePipeline(const SlangProgram &slangProgram, const ComputePipelineDesc &desc, std::uint32_t descriptorMaxSets, std::span<const SlangImmutableSamplerBinding> immutableSamplers) const
{
		nrAssert(device_.has_value(), "PipelineService::createComputePipeline requires a bound logical device.");
		nrAssert(slangProgram.valid(), "PipelineService::createComputePipeline requires a valid SlangProgram.");
		auto const *entryPoint = slangProgram.entryPoint();
		nrAssert(
			entryPoint && entryPoint->stage == SLANG_STAGE_COMPUTE,
			"PipelineService::createComputePipeline requires a compute-stage single-entry program.");

		const auto &device = device_->get();
		auto layoutBundle = createPipelineLayoutBundle(slangProgram, desc.descriptorBindingPolicy, immutableSamplers);

		auto programs = std::array{slangProgram};
		auto shaderProgram = VkShaderProgram::create(device, programs);
		auto pipeline = ComputePipeline::create(device, layoutBundle.layout, shaderProgram, desc, pipelineCacheOrNull());
		return makePipelineState(slangProgram, std::move(layoutBundle), descriptorMaxSets, std::move(pipeline));
	}

[[nodiscard]] PipelineState<RayTracingPipeline> PipelineService::createRayTracingPipeline(
	const SlangProgram &reflectionProgram,
	const RayTracingProgramAssemblyDesc &assembly,
	const RayTracingPipelineDesc &desc,
	std::uint32_t descriptorMaxSets,
	std::span<const SlangImmutableSamplerBinding> immutableSamplers) const
{
		nrAssert(device_.has_value(), "PipelineService::createRayTracingPipeline requires a bound logical device.");
		nrAssert(reflectionProgram.valid(), "PipelineService::createRayTracingPipeline requires a valid reflection SlangProgram.");
		auto assemblyValidation = validateRayTracingProgramAssemblyDesc(assembly);
		nrAssert(
			!assemblyValidation.has_value(),
			std::format(
				"PipelineService::createRayTracingPipeline invalid program assembly: {}",
				assemblyValidation.value_or(std::string{})));

		const auto &device = device_->get();
		auto layoutBundle = createPipelineLayoutBundle(reflectionProgram, desc.descriptorBindingPolicy, immutableSamplers);
		auto const reflectionSignature = layoutBundle.descriptorLayout.abiSignature();
		std::ranges::for_each(assembly.stages, [&](const RayTracingPipelineStageSelection &selection) {
			assertReflectionLayoutCoverage(
				reflectionSignature,
				reflectionProgram,
				selection.program.get(),
				desc.descriptorBindingPolicy);
		});

		auto hasCaptureReplayHandles = std::ranges::any_of(assembly.groups, [](const RayTracingShaderGroupDesc &groupDesc) {
			return !groupDesc.captureReplayHandle.empty();
		});
		if (rtCapabilities_.has_value())
		{
			nrAssert(
				desc.maxRayRecursionDepth <= rtCapabilities_->maxRayRecursionDepth,
				std::format(
					"PipelineService::createRayTracingPipeline recursion depth {} exceeds device max {}.",
					desc.maxRayRecursionDepth,
					rtCapabilities_->maxRayRecursionDepth));

			auto wantsCaptureReplay =
				hasCaptureReplayHandles ||
				((desc.flags & vk::PipelineCreateFlagBits::eRayTracingShaderGroupHandleCaptureReplayKHR) != vk::PipelineCreateFlags{});
			if (wantsCaptureReplay)
			{
				nrAssert(
					rtCapabilities_->rayTracingPipelineShaderGroupHandleCaptureReplay,
					"PipelineService::createRayTracingPipeline requires rayTracingPipelineShaderGroupHandleCaptureReplay for capture/replay handles.");
			}

			if (hasCaptureReplayHandles)
			{
				nrAssert(
					rtCapabilities_->shaderGroupHandleCaptureReplaySize > 0,
					"PipelineService::createRayTracingPipeline requires a non-zero shaderGroupHandleCaptureReplaySize.");

				auto invalidHandleIt = std::ranges::find_if(assembly.groups, [&](const RayTracingShaderGroupDesc &groupDesc) {
					return !groupDesc.captureReplayHandle.empty() &&
					       groupDesc.captureReplayHandle.size() != static_cast<std::size_t>(rtCapabilities_->shaderGroupHandleCaptureReplaySize);
				});
				nrAssert(
					invalidHandleIt == std::ranges::end(assembly.groups),
					std::format(
						"PipelineService::createRayTracingPipeline capture replay handles must be {} bytes.",
						rtCapabilities_->shaderGroupHandleCaptureReplaySize));
			}
		}

		std::ranges::for_each(assembly.stages, [](const RayTracingPipelineStageSelection &selection) {
			auto const *entryPointData = selection.program.get().entryPoint();
			nrAssert(entryPointData != nullptr, "PipelineService::createRayTracingPipeline received an invalid single-entry program.");
			nrAssert(
				detail::isRayTracingStage(entryPointData->stage),
				std::format("PipelineService::createRayTracingPipeline entrypoint '{}' is not a ray-tracing stage.", entryPointData->entryPointName));
		});

		auto shaderProgram = VkShaderProgram::create(device, assembly.stages);
		auto pipeline = RayTracingPipeline::create(
			device,
			layoutBundle.layout,
			shaderProgram,
			desc,
			assembly.groups,
			pipelineCacheOrNull());
		return makePipelineState(reflectionProgram, std::move(layoutBundle), descriptorMaxSets, std::move(pipeline));
	}
} // namespace nr::rhi

namespace nr::rhi
{
namespace detail
{
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
} // namespace detail
} // namespace nr::rhi

namespace nr::rhi
{
namespace mesh
{
void applyRasterState(const vk::raii::CommandBuffer &commandBuffer, const MeshRasterState &state)
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

void drawTasks(const vk::raii::CommandBuffer &commandBuffer, std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ)
{
	nrAssert(*commandBuffer != nullptr, "mesh::drawTasks requires a valid command buffer.");
	nrAssert(groupCountX > 0 && groupCountY > 0 && groupCountZ > 0, "mesh::drawTasks requires non-zero dispatch group counts.");
	commandBuffer.drawMeshTasksEXT(groupCountX, groupCountY, groupCountZ);
}
} // namespace mesh
} // namespace nr::rhi
