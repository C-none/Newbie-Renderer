module;
export module nr.rhi:pipeline;
import dependency;
import :type;
import nr.utils;
import :slang;
import std;

namespace nr::rhi::detail
{
struct CursorPathResolution
{
    std::string canonicalPath;
    uint32_t inlineArrayElement = 0;
    uint32_t inlineArrayDimensions = 0;
};

[[nodiscard]] CursorPathResolution normalizeCursorPath(std::string_view path)
{
    CursorPathResolution result;
    result.canonicalPath.reserve(path.size());

    for (size_t i = 0; i < path.size();)
    {
        auto ch = path[i];
        if (ch != '[')
        {
            result.canonicalPath.push_back(ch);
            ++i;
            continue;
        }

        auto closeBracket = path.find(']', i + 1);
        if (closeBracket == std::string_view::npos)
        {
            result.canonicalPath.append(path.substr(i));
            break;
        }

        auto token = path.substr(i + 1, closeBracket - (i + 1));
        result.canonicalPath += "[]";

        if (!token.empty())
        {
            uint32_t parsedIndex = 0;
            auto parseResult = std::from_chars(token.data(), token.data() + token.size(), parsedIndex);
            if (parseResult.ec == std::errc{} && parseResult.ptr == token.data() + token.size())
            {
                if (result.inlineArrayDimensions == 0)
                {
                    result.inlineArrayElement = parsedIndex;
                }
                ++result.inlineArrayDimensions;
            }
        }

        i = closeBracket + 1;
    }

    return result;
}

[[nodiscard]] bool isGraphicsStage(SlangStage stage) noexcept
{
    switch (stage)
    {
    case SLANG_STAGE_VERTEX:
    case SLANG_STAGE_HULL:
    case SLANG_STAGE_DOMAIN:
    case SLANG_STAGE_GEOMETRY:
    case SLANG_STAGE_FRAGMENT:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isMeshStage(SlangStage stage) noexcept
{
    switch (stage)
    {
    case SLANG_STAGE_AMPLIFICATION:
    case SLANG_STAGE_MESH:
    case SLANG_STAGE_FRAGMENT:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isRayTracingStage(SlangStage stage) noexcept
{
    switch (stage)
    {
    case SLANG_STAGE_RAY_GENERATION:
    case SLANG_STAGE_ANY_HIT:
    case SLANG_STAGE_CLOSEST_HIT:
    case SLANG_STAGE_MISS:
    case SLANG_STAGE_INTERSECTION:
    case SLANG_STAGE_CALLABLE:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] vk::PipelineColorBlendAttachmentState defaultBlendAttachment()
{
    vk::PipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = vk::False;
    attachment.srcColorBlendFactor = vk::BlendFactor::eOne;
    attachment.dstColorBlendFactor = vk::BlendFactor::eZero;
    attachment.colorBlendOp = vk::BlendOp::eAdd;
    attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    attachment.alphaBlendOp = vk::BlendOp::eAdd;
    attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
                        | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    return attachment;
}

[[nodiscard]] bool containsStage(std::initializer_list<SlangStage> allowedStages, SlangStage stage)
{
    for (auto const candidate : allowedStages)
    {
        if (candidate == stage)
        {
            return true;
        }
    }
    return false;
}

constexpr int32_t VK_SHADER_UNUSED = static_cast<int32_t>(~0);  // VK_SHADER_UNUSED_KHR

} // namespace nr::rhi::detail

export namespace nr::rhi
{

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
    std::vector<vk::DynamicState> dynamicStates{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineCreateFlags flags = {};
    GraphicsPipelineMode mode = GraphicsPipelineMode::StandardGraphics;  // Optional mode selection
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

class ReflectedPipelineLayout;
class DescriptorPool;

class DescriptorSet
{
  public:
    DescriptorSet() = default;
    DescriptorSet(const DescriptorSet&) = delete;
    DescriptorSet& operator=(const DescriptorSet&) = delete;

    DescriptorSet(DescriptorSet&& other) noexcept
    {
        *this = std::move(other);
    }

    DescriptorSet& operator=(DescriptorSet&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            device_ = std::move(other.device_);
            descriptorPool_ = std::move(other.descriptorPool_);
            set_ = std::exchange(other.set_, vk::DescriptorSet{});
            setIndex_ = std::exchange(other.setIndex_, 0);
        }
        return *this;
    }

    ~DescriptorSet()
    {
        reset();
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return static_cast<bool>(set_);
    }

    [[nodiscard]] vk::DescriptorSet raw() const noexcept
    {
        return set_;
    }

    [[nodiscard]] uint32_t setIndex() const noexcept
    {
        return setIndex_;
    }

    void reset()
    {
        // RAII will handle cleanup automatically, just reset the handle
        set_ = vk::DescriptorSet{};
    }

  private:
    friend class DescriptorPool;

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    std::optional<std::reference_wrapper<const vk::raii::DescriptorPool>> descriptorPool_;
    vk::DescriptorSet set_{};
    uint32_t setIndex_ = 0;
};

class DescriptorWriter
{
  public:
    explicit DescriptorWriter(const ReflectedPipelineLayout& layout, DescriptorSet& descriptorSet)
        : layout_(std::cref(layout)), descriptorSet_(std::ref(descriptorSet))
    {
    }

    DescriptorWriter(const DescriptorWriter&) = delete;
    DescriptorWriter& operator=(const DescriptorWriter&) = delete;
    DescriptorWriter(DescriptorWriter&&) noexcept = default;
    DescriptorWriter& operator=(DescriptorWriter&&) noexcept = default;

    DescriptorWriter& bindUniformBuffer(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayElement = 0)
    {
        return bindBuffer(binding, vk::DescriptorType::eUniformBuffer, buffer, offset, range, arrayElement);
    }

    DescriptorWriter& bindUniformBuffer(std::string_view path, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eUniformBuffer, arrayElement);
        return bindBuffer(binding.binding, vk::DescriptorType::eUniformBuffer, buffer, offset, range, binding.arrayElement);
    }

    DescriptorWriter& bindStorageBuffer(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayElement = 0)
    {
        return bindBuffer(binding, vk::DescriptorType::eStorageBuffer, buffer, offset, range, arrayElement);
    }

    DescriptorWriter& bindStorageBuffer(std::string_view path, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eStorageBuffer, arrayElement);
        return bindBuffer(binding.binding, vk::DescriptorType::eStorageBuffer, buffer, offset, range, binding.arrayElement);
    }

    DescriptorWriter& bindSampler(uint32_t binding, vk::Sampler sampler, uint32_t arrayElement = 0)
    {
        return bindImage(binding, vk::DescriptorType::eSampler, sampler, {}, vk::ImageLayout::eUndefined, arrayElement);
    }

    DescriptorWriter& bindSampler(std::string_view path, vk::Sampler sampler, uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eSampler, arrayElement);
        return bindImage(binding.binding, vk::DescriptorType::eSampler, sampler, {}, vk::ImageLayout::eUndefined, binding.arrayElement);
    }

    DescriptorWriter& bindSampler(uint32_t binding, const SlangSampler& sampler, uint32_t arrayElement = 0)
    {
        nrAssert(sampler.valid(), "DescriptorWriter::bindSampler requires a valid SlangSampler.");
        return bindSampler(binding, *sampler.handle(), arrayElement);
    }

    DescriptorWriter& bindSampledImage(uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, uint32_t arrayElement = 0)
    {
        return bindImage(binding, vk::DescriptorType::eSampledImage, {}, imageView, layout, arrayElement);
    }

    DescriptorWriter& bindSampledImage(std::string_view path, vk::ImageView imageView, vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal, uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eSampledImage, arrayElement);
        return bindImage(binding.binding, vk::DescriptorType::eSampledImage, {}, imageView, layout, binding.arrayElement);
    }

    DescriptorWriter& bindCombinedImageSampler(
        uint32_t binding,
        vk::Sampler sampler,
        vk::ImageView imageView,
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        uint32_t arrayElement = 0)
    {
        return bindImage(binding, vk::DescriptorType::eCombinedImageSampler, sampler, imageView, layout, arrayElement);
    }

    DescriptorWriter& bindCombinedImageSampler(
        std::string_view path,
        vk::Sampler sampler,
        vk::ImageView imageView,
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eCombinedImageSampler, arrayElement);
        return bindImage(binding.binding, vk::DescriptorType::eCombinedImageSampler, sampler, imageView, layout, binding.arrayElement);
    }

    DescriptorWriter& bindCombinedImageSampler(
        uint32_t binding,
        const SlangSampler& sampler,
        vk::ImageView imageView,
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        uint32_t arrayElement = 0)
    {
        nrAssert(sampler.valid(), "DescriptorWriter::bindCombinedImageSampler requires a valid SlangSampler.");
        return bindCombinedImageSampler(binding, *sampler.handle(), imageView, layout, arrayElement);
    }

    DescriptorWriter& bindStorageImage(uint32_t binding, vk::ImageView imageView, vk::ImageLayout layout = vk::ImageLayout::eGeneral, uint32_t arrayElement = 0)
    {
        return bindImage(binding, vk::DescriptorType::eStorageImage, {}, imageView, layout, arrayElement);
    }

    DescriptorWriter& bindStorageImage(std::string_view path, vk::ImageView imageView, vk::ImageLayout layout = vk::ImageLayout::eGeneral, uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eStorageImage, arrayElement);
        return bindImage(binding.binding, vk::DescriptorType::eStorageImage, {}, imageView, layout, binding.arrayElement);
    }

    DescriptorWriter& bindAccelerationStructure(uint32_t binding, vk::AccelerationStructureKHR accelerationStructure, uint32_t arrayElement = 0)
    {
        requireBinding(binding, vk::DescriptorType::eAccelerationStructureKHR);
        accelerationStructures_.push_back(accelerationStructure);
        pendingWrites_.push_back(PendingWrite{
            .binding = binding,
            .arrayElement = arrayElement,
            .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
            .kind = PendingWriteKind::AccelerationStructure,
            .payloadIndex = static_cast<uint32_t>(accelerationStructures_.size() - 1),
        });
        return *this;
    }

    DescriptorWriter& bindAccelerationStructure(std::string_view path, vk::AccelerationStructureKHR accelerationStructure, uint32_t arrayElement = 0)
    {
        auto binding = resolveBindingPath(path, vk::DescriptorType::eAccelerationStructureKHR, arrayElement);
        return bindAccelerationStructure(binding.binding, accelerationStructure, binding.arrayElement);
    }

    void apply() const;

  private:
    enum class PendingWriteKind : uint8_t
    {
        Buffer,
        Image,
        AccelerationStructure,
    };

    struct PendingWrite
    {
        uint32_t binding = 0;
        uint32_t arrayElement = 0;
        vk::DescriptorType descriptorType = vk::DescriptorType::eUniformBuffer;
        PendingWriteKind kind = PendingWriteKind::Buffer;
        uint32_t payloadIndex = 0;
    };

    DescriptorWriter& bindBuffer(
        uint32_t binding,
        vk::DescriptorType descriptorType,
        vk::Buffer buffer,
        vk::DeviceSize offset,
        vk::DeviceSize range,
        uint32_t arrayElement)
    {
        requireBinding(binding, descriptorType);
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = offset;
        bufferInfo.range = range;
        bufferWrites_.push_back(bufferInfo);
        
        PendingWrite write{};
        write.binding = binding;
        write.arrayElement = arrayElement;
        write.descriptorType = descriptorType;
        write.kind = PendingWriteKind::Buffer;
        write.payloadIndex = static_cast<uint32_t>(bufferWrites_.size() - 1);
        pendingWrites_.push_back(write);
        return *this;
    }

    DescriptorWriter& bindImage(
        uint32_t binding,
        vk::DescriptorType descriptorType,
        vk::Sampler sampler,
        vk::ImageView imageView,
        vk::ImageLayout imageLayout,
        uint32_t arrayElement)
    {
        requireBinding(binding, descriptorType);
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = sampler;
        imageInfo.imageView = imageView;
        imageInfo.imageLayout = imageLayout;
        imageWrites_.push_back(imageInfo);
        
        PendingWrite write{};
        write.binding = binding;
        write.arrayElement = arrayElement;
        write.descriptorType = descriptorType;
        write.kind = PendingWriteKind::Image;
        write.payloadIndex = static_cast<uint32_t>(imageWrites_.size() - 1);
        pendingWrites_.push_back(write);
        return *this;
    }

    struct ResolvedPathBinding
    {
        uint32_t binding = 0;
        uint32_t arrayElement = 0;
    };

    [[nodiscard]] ResolvedPathBinding resolveBindingPath(std::string_view path, vk::DescriptorType descriptorType, uint32_t baseArrayElement) const;

    void requireBinding(uint32_t binding, vk::DescriptorType descriptorType) const;

    std::reference_wrapper<const ReflectedPipelineLayout> layout_;
    std::reference_wrapper<DescriptorSet> descriptorSet_;
    std::vector<PendingWrite> pendingWrites_;
    std::vector<vk::DescriptorBufferInfo> bufferWrites_;
    std::vector<vk::DescriptorImageInfo> imageWrites_;
    std::vector<vk::AccelerationStructureKHR> accelerationStructures_;
};

class DescriptorPool
{
  public:
    DescriptorPool() = default;
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    DescriptorPool(DescriptorPool&&) noexcept = default;
    DescriptorPool& operator=(DescriptorPool&&) noexcept = default;

    [[nodiscard]] static DescriptorPool create(
        const vk::raii::Device& device,
        const ReflectedPipelineLayout& layout,
        uint32_t maxSets = 64,
        vk::DescriptorPoolCreateFlags flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);

    [[nodiscard]] bool valid() const noexcept
    {
        return pool_ != nullptr;
    }

    [[nodiscard]] const vk::raii::DescriptorPool& handle() const noexcept
    {
        return pool_;
    }

    [[nodiscard]] DescriptorSet allocate(const ReflectedPipelineLayout& layout, uint32_t setIndex) const;

    [[nodiscard]] std::vector<DescriptorSet> allocateAll(const ReflectedPipelineLayout& layout) const;

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    vk::raii::DescriptorPool pool_ = {nullptr};
    uint32_t maxSets_ = 0;
    uint32_t layoutSetCount_ = 0;
};

class ReflectedPipelineLayout
{
  public:
    ReflectedPipelineLayout() = default;
    ReflectedPipelineLayout(const ReflectedPipelineLayout&) = delete;
    ReflectedPipelineLayout& operator=(const ReflectedPipelineLayout&) = delete;
    ReflectedPipelineLayout(ReflectedPipelineLayout&&) noexcept = default;
    ReflectedPipelineLayout& operator=(ReflectedPipelineLayout&&) noexcept = default;

    [[nodiscard]] static ReflectedPipelineLayout create(
        const vk::raii::Device& device,
        const SlangReflectionLayout& reflection)
    {
        ReflectedPipelineLayout result;
        result.reflection_ = reflection;
        result.device_ = std::cref(device);
        result.immutableSamplers_.reserve(reflection.immutableSamplers.size());

        auto sortedSets = reflection.descriptorSets;
        std::ranges::sort(sortedSets, {}, &SlangDescriptorSetLayoutInfo::set);

        uint32_t maxSetIndex = 0;
        for (auto const& setInfo : sortedSets)
        {
            maxSetIndex = std::max(maxSetIndex, setInfo.set);
        }
        result.setToLayoutIndex_.assign(static_cast<size_t>(maxSetIndex + 1), invalidLayoutIndex);

        for (auto const& setInfo : sortedSets)
        {
            if (setInfo.bindings.empty())
            {
                continue;
            }

            std::map<uint32_t, SlangDescriptorBinding> mergedBindings;
            for (auto const& binding : setInfo.bindings)
            {
                auto [it, inserted] = mergedBindings.try_emplace(binding.binding, binding);
                if (!inserted)
                {
                    it->second.stageFlags |= binding.stageFlags;
                    it->second.descriptorCount = std::max(it->second.descriptorCount, binding.descriptorCount);
                }
            }

            std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
            vkBindings.reserve(mergedBindings.size());
            for (auto const& [bindingIndex, binding] : mergedBindings)
            {
                const vk::Sampler* immutableSamplerArray = nullptr;
                for (auto const& immutableBinding : reflection.immutableSamplers)
                {
                    if (immutableBinding.set != setInfo.set || immutableBinding.binding != bindingIndex)
                    {
                        continue;
                    }

                    auto descriptorCount = std::max(immutableBinding.descriptorCount, 1u);
                    nrAssert(binding.descriptorType == vk::DescriptorType::eSampler || binding.descriptorType == vk::DescriptorType::eCombinedImageSampler,
                        std::format("Immutable sampler assigned to non-sampler descriptor at set={}, binding={}", setInfo.set, bindingIndex));

                    ImmutableSamplerStorage immutableStorage{};
                    immutableStorage.set = setInfo.set;
                    immutableStorage.binding = bindingIndex;
                    immutableStorage.samplers.reserve(descriptorCount);
                    immutableStorage.rawSamplers.reserve(descriptorCount);
                    for (uint32_t samplerIndex = 0; samplerIndex < descriptorCount; ++samplerIndex)
                    {
                        vk::SamplerCreateInfo samplerInfo{};
                        samplerInfo.magFilter = immutableBinding.samplerDesc.magFilter;
                        samplerInfo.minFilter = immutableBinding.samplerDesc.minFilter;
                        samplerInfo.mipmapMode = immutableBinding.samplerDesc.mipmapMode;
                        samplerInfo.addressModeU = immutableBinding.samplerDesc.addressModeU;
                        samplerInfo.addressModeV = immutableBinding.samplerDesc.addressModeV;
                        samplerInfo.addressModeW = immutableBinding.samplerDesc.addressModeW;
                        samplerInfo.mipLodBias = immutableBinding.samplerDesc.mipLodBias;
                        samplerInfo.anisotropyEnable = immutableBinding.samplerDesc.anisotropyEnable ? vk::True : vk::False;
                        samplerInfo.maxAnisotropy = immutableBinding.samplerDesc.maxAnisotropy;
                        samplerInfo.compareEnable = immutableBinding.samplerDesc.compareEnable ? vk::True : vk::False;
                        samplerInfo.compareOp = immutableBinding.samplerDesc.compareOp;
                        samplerInfo.minLod = immutableBinding.samplerDesc.minLod;
                        samplerInfo.maxLod = immutableBinding.samplerDesc.maxLod;
                        samplerInfo.borderColor = immutableBinding.samplerDesc.borderColor;
                        samplerInfo.unnormalizedCoordinates = immutableBinding.samplerDesc.unnormalizedCoordinates ? vk::True : vk::False;

                        immutableStorage.samplers.emplace_back(device, samplerInfo);
                        immutableStorage.rawSamplers.emplace_back(*immutableStorage.samplers.back());
                    }

                    result.immutableSamplers_.push_back(std::move(immutableStorage));
                    immutableSamplerArray = result.immutableSamplers_.back().rawSamplers.data();
                    break;
                }

                vk::DescriptorSetLayoutBinding vkBinding{};
                vkBinding.binding = bindingIndex;
                vkBinding.descriptorType = binding.descriptorType;
                vkBinding.descriptorCount = std::max(binding.descriptorCount, 1u);
                vkBinding.stageFlags = binding.stageFlags;
                vkBinding.pImmutableSamplers = immutableSamplerArray;
                vkBindings.push_back(vkBinding);
            }

            vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
            setLayoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
            setLayoutInfo.pBindings = vkBindings.data();

            auto layoutIndex = static_cast<uint32_t>(result.descriptorSetLayouts_.size());
            result.descriptorSetLayouts_.emplace_back(device, setLayoutInfo);
            result.setToLayoutIndex_[setInfo.set] = layoutIndex;
        }

        struct PushConstantKey
        {
            uint32_t offset = 0;
            uint32_t size = 0;

            auto operator<=>(const PushConstantKey&) const = default;
        };

        std::map<PushConstantKey, vk::ShaderStageFlags> mergedPushConstants;
        for (auto const& pushConstant : reflection.pushConstantRanges)
        {
            if (pushConstant.size == 0)
            {
                continue;
            }

            PushConstantKey key{
                .offset = pushConstant.offset,
                .size = pushConstant.size,
            };
            mergedPushConstants[key] |= pushConstant.stageFlags;
        }

        std::vector<vk::PushConstantRange> pushConstantRanges;
        pushConstantRanges.reserve(mergedPushConstants.size());
        for (auto const& [key, stageFlags] : mergedPushConstants)
        {
            vk::PushConstantRange range{};
            range.stageFlags = stageFlags;
            range.offset = key.offset;
            range.size = key.size;
            pushConstantRanges.push_back(range);
        }

        std::vector<vk::DescriptorSetLayout> rawSetLayouts;
        rawSetLayouts.reserve(result.descriptorSetLayouts_.size());
        for (auto const& setLayout : result.descriptorSetLayouts_)
        {
            rawSetLayouts.emplace_back(*setLayout);
        }

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(rawSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = rawSetLayouts.empty() ? nullptr : rawSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

        result.pipelineLayout_ = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        // Build an index for cursor path lookup so DescriptorWriter path binding does not
        // need to linearly scan reflection.cursorBindings every call.
        for (size_t i = 0; i < result.reflection_.cursorBindings.size(); ++i)
        {
            auto const& item = result.reflection_.cursorBindings[i];
            auto [it, inserted] = result.cursorBindingPathIndex_.try_emplace(
                CursorBindingKey{
                    .set = item.set,
                    .path = item.path,
                },
                i);

            if (!inserted)
            {
                auto const& existing = result.reflection_.cursorBindings[it->second];
                nrAssert(
                    existing.binding == item.binding && existing.descriptorType == item.descriptorType,
                    std::format(
                        "Conflicting cursor binding path '{}' in set {} ({}:{}, {}:{})",
                        item.path,
                        item.set,
                        existing.binding,
                        vk::to_string(existing.descriptorType),
                        item.binding,
                        vk::to_string(item.descriptorType)));
            }
        }

        return result;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return pipelineLayout_ != nullptr;
    }

    [[nodiscard]] const vk::raii::PipelineLayout& handle() const noexcept
    {
        return pipelineLayout_;
    }

    [[nodiscard]] vk::PipelineLayout raw() const noexcept
    {
        return *pipelineLayout_;
    }

    [[nodiscard]] std::span<const vk::raii::DescriptorSetLayout> descriptorSetLayouts() const noexcept
    {
        return descriptorSetLayouts_;
    }

    [[nodiscard]] const std::reference_wrapper<const vk::raii::Device>& device() const
    {
        nrAssert(device_.has_value(), "ReflectedPipelineLayout has no associated device.");
        return *device_;
    }

    [[nodiscard]] const vk::raii::DescriptorSetLayout* tryGetDescriptorSetLayout(uint32_t setIndex) const noexcept
    {
        if (setIndex >= setToLayoutIndex_.size())
        {
            return nullptr;
        }

        auto const layoutIndex = setToLayoutIndex_[setIndex];
        if (layoutIndex == invalidLayoutIndex || layoutIndex >= descriptorSetLayouts_.size())
        {
            return nullptr;
        }
        return &descriptorSetLayouts_[layoutIndex];
    }

    [[nodiscard]] const SlangDescriptorBinding* tryFindBinding(uint32_t setIndex, uint32_t binding) const noexcept
    {
        auto const setIt = std::ranges::find_if(reflection_.descriptorSets, [setIndex](SlangDescriptorSetLayoutInfo const& setInfo) { return setInfo.set == setIndex; });
        if (setIt == reflection_.descriptorSets.end())
        {
            return nullptr;
        }

        auto const bindingIt = std::ranges::find_if(setIt->bindings, [binding](SlangDescriptorBinding const& candidate) { return candidate.binding == binding; });
        if (bindingIt == setIt->bindings.end())
        {
            return nullptr;
        }

        return &(*bindingIt);
    }

    [[nodiscard]] const SlangReflectionLayout::CursorBinding* tryFindBindingByPath(uint32_t setIndex, std::string_view path) const noexcept
    {
        auto it = cursorBindingPathIndex_.find(CursorBindingKey{
            .set = setIndex,
            .path = std::string(path),
        });
        if (it == cursorBindingPathIndex_.end())
        {
            return nullptr;
        }
        return &reflection_.cursorBindings[it->second];
    }

    [[nodiscard]] const SlangReflectionLayout& reflection() const noexcept
    {
        return reflection_;
    }

  private:
    struct CursorBindingKey
    {
        uint32_t set = 0;
        std::string path;

        [[nodiscard]] bool operator==(const CursorBindingKey& rhs) const noexcept
        {
            return set == rhs.set && path == rhs.path;
        }
    };

    struct CursorBindingKeyHash
    {
        [[nodiscard]] size_t operator()(const CursorBindingKey& key) const noexcept
        {
            size_t hash = std::hash<uint32_t>{}(key.set);
            hash ^= std::hash<std::string>{}(key.path) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

        struct ImmutableSamplerStorage
        {
                uint32_t set = 0;
                uint32_t binding = 0;
                std::vector<vk::raii::Sampler> samplers;
                std::vector<vk::Sampler> rawSamplers;
        };

        static constexpr uint32_t invalidLayoutIndex = std::numeric_limits<uint32_t>::max();

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    vk::raii::PipelineLayout pipelineLayout_ = {nullptr};
    std::vector<vk::raii::DescriptorSetLayout> descriptorSetLayouts_;
        std::vector<uint32_t> setToLayoutIndex_;
        std::vector<ImmutableSamplerStorage> immutableSamplers_;
    std::unordered_map<CursorBindingKey, size_t, CursorBindingKeyHash> cursorBindingPathIndex_;
    SlangReflectionLayout reflection_;
};

inline void DescriptorWriter::requireBinding(uint32_t binding, vk::DescriptorType descriptorType) const
{
    auto const& descriptorSet = descriptorSet_.get();
    nrAssert(descriptorSet.valid(), "DescriptorWriter requires a valid descriptor set.");

    auto const* bindingInfo = layout_.get().tryFindBinding(descriptorSet.setIndex(), binding);
    nrAssert(bindingInfo != nullptr, std::format("Descriptor binding {} does not exist in set {}.", binding, descriptorSet.setIndex()));
    nrAssert(bindingInfo->descriptorType == descriptorType,
        std::format("Descriptor type mismatch at set={}, binding={}. Expected {}, got {}.",
            descriptorSet.setIndex(),
            binding,
            vk::to_string(bindingInfo->descriptorType),
            vk::to_string(descriptorType)));
}

inline DescriptorWriter::ResolvedPathBinding DescriptorWriter::resolveBindingPath(std::string_view path, vk::DescriptorType descriptorType, uint32_t baseArrayElement) const
{
    auto const& descriptorSet = descriptorSet_.get();
    nrAssert(descriptorSet.valid(), "DescriptorWriter requires a valid descriptor set.");

    auto pathResolution = detail::normalizeCursorPath(path);
    nrAssert(
        pathResolution.inlineArrayDimensions <= 1,
        std::format(
            "Path '{}' has more than one explicit array index. For nested arrays, keep path canonical ([]) and pass flattened arrayElement explicitly.",
            path));

    auto const* cursorBinding = layout_.get().tryFindBindingByPath(descriptorSet.setIndex(), pathResolution.canonicalPath);
    nrAssert(
        cursorBinding != nullptr,
        std::format("Descriptor path '{}' (canonical '{}') does not exist in set {}.", path, pathResolution.canonicalPath, descriptorSet.setIndex()));
    nrAssert(
        cursorBinding->descriptorType == descriptorType,
        std::format(
            "Descriptor type mismatch at set={}, path='{}'. Expected {}, got {}.",
            descriptorSet.setIndex(),
            pathResolution.canonicalPath,
            vk::to_string(cursorBinding->descriptorType),
            vk::to_string(descriptorType)));

    auto resolvedArrayElement = baseArrayElement + pathResolution.inlineArrayElement;
    nrAssert(
        resolvedArrayElement < std::max(cursorBinding->descriptorCount, 1u),
        std::format(
            "Descriptor array index {} out of range for set={}, path='{}' (descriptorCount={}).",
            resolvedArrayElement,
            descriptorSet.setIndex(),
            pathResolution.canonicalPath,
            cursorBinding->descriptorCount));

    return ResolvedPathBinding{
        .binding = cursorBinding->binding,
        .arrayElement = resolvedArrayElement,
    };
}

inline void DescriptorWriter::apply() const
{
    auto const& descriptorSet = descriptorSet_.get();
    nrAssert(descriptorSet.valid(), "DescriptorWriter::apply requires a valid DescriptorSet.");

    std::vector<vk::WriteDescriptorSet> writes;
    writes.reserve(pendingWrites_.size());

    std::vector<vk::DescriptorBufferInfo> bufferInfos;
    std::vector<vk::DescriptorImageInfo> imageInfos;
    std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> accelerationInfos;
    bufferInfos.reserve(bufferWrites_.size());
    imageInfos.reserve(imageWrites_.size());
    accelerationInfos.reserve(accelerationStructures_.size());

    for (auto const& pending : pendingWrites_)
    {
        vk::WriteDescriptorSet write{};
        write.dstSet = descriptorSet.raw();
        write.dstBinding = pending.binding;
        write.dstArrayElement = pending.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = pending.descriptorType;

        switch (pending.kind)
        {
        case PendingWriteKind::Buffer:
            bufferInfos.push_back(bufferWrites_[pending.payloadIndex]);
            write.pBufferInfo = &bufferInfos.back();
            break;
        case PendingWriteKind::Image:
            imageInfos.push_back(imageWrites_[pending.payloadIndex]);
            write.pImageInfo = &imageInfos.back();
            break;
        case PendingWriteKind::AccelerationStructure:
        {
            vk::WriteDescriptorSetAccelerationStructureKHR info{};
            info.accelerationStructureCount = 1;
            info.pAccelerationStructures = &accelerationStructures_[pending.payloadIndex];
            accelerationInfos.push_back(info);
            write.pNext = &accelerationInfos.back();
            break;
        }
        }

        writes.push_back(write);
    }

    if (writes.empty())
    {
        return;
    }

    layout_.get().device().get().updateDescriptorSets(writes, {});
}

inline DescriptorPool DescriptorPool::create(
    const vk::raii::Device& device,
    const ReflectedPipelineLayout& layout,
    uint32_t maxSets,
    vk::DescriptorPoolCreateFlags flags)
{
    nrAssert(maxSets > 0, "DescriptorPool::create requires maxSets > 0.");

    std::map<vk::DescriptorType, uint32_t> aggregated;
    for (auto const& setInfo : layout.reflection().descriptorSets)
    {
        for (auto const& binding : setInfo.bindings)
        {
            aggregated[binding.descriptorType] += std::max(binding.descriptorCount, 1u);
        }
    }

    std::vector<vk::DescriptorPoolSize> poolSizes;
    poolSizes.reserve(aggregated.size());
    for (auto const& [descriptorType, count] : aggregated)
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = descriptorType;
        poolSize.descriptorCount = std::max(count * maxSets, 1u);
        poolSizes.push_back(poolSize);
    }

    DescriptorPool result;
    result.device_ = std::cref(device);
    result.maxSets_ = maxSets;
    result.layoutSetCount_ = static_cast<uint32_t>(layout.descriptorSetLayouts().size());

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = flags;
    poolInfo.maxSets = std::max(result.layoutSetCount_ * maxSets, maxSets);
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.empty() ? nullptr : poolSizes.data();
    result.pool_ = vk::raii::DescriptorPool(device, poolInfo);

    return result;
}

inline DescriptorSet DescriptorPool::allocate(const ReflectedPipelineLayout& layout, uint32_t setIndex) const
{
    auto const* descriptorSetLayout = layout.tryGetDescriptorSetLayout(setIndex);
    nrAssert(descriptorSetLayout != nullptr, std::format("Descriptor set layout {} does not exist in reflected layout.", setIndex));

    vk::DescriptorSetLayout rawLayout = *(*descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.descriptorPool = *pool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &rawLayout;

    auto allocatedSets = device_->get().allocateDescriptorSets(allocateInfo);
    nrAssert(!allocatedSets.empty(), "Descriptor allocation returned an empty set list.");

    DescriptorSet result;
    result.device_ = std::cref(device_->get());
    result.descriptorPool_ = std::cref(pool_);
    result.set_ = allocatedSets.front();
    result.setIndex_ = setIndex;
    return result;
}

inline std::vector<DescriptorSet> DescriptorPool::allocateAll(const ReflectedPipelineLayout& layout) const
{
    std::vector<DescriptorSet> sets;
    sets.reserve(layout.descriptorSetLayouts().size());
    for (uint32_t setIndex = 0; setIndex < layout.descriptorSetLayouts().size(); ++setIndex)
    {
        sets.push_back(allocate(layout, setIndex));
    }
    return sets;
}

class VkShaderProgram
{
  public:
    VkShaderProgram() = default;
    VkShaderProgram(const VkShaderProgram&) = delete;
    VkShaderProgram& operator=(const VkShaderProgram&) = delete;
    VkShaderProgram(VkShaderProgram&&) noexcept = default;
    VkShaderProgram& operator=(VkShaderProgram&&) noexcept = default;

    [[nodiscard]] static VkShaderProgram create(const vk::raii::Device& device, const SlangProgram& slangProgram)
    {
        nrAssert(slangProgram.valid(), "VkShaderProgram::create requires a valid SlangProgram.");

        VkShaderProgram result;
        result.device_ = std::cref(device);

        auto const entryPoints = slangProgram.entryPoints();
        nrAssert(!entryPoints.empty(), "SlangProgram does not contain any entry point binaries.");

        result.modules_.reserve(entryPoints.size());
        result.entryPointNames_.reserve(entryPoints.size());
        result.stages_.reserve(entryPoints.size());

        for (auto const& entryPoint : entryPoints)
        {
            nrAssert(!entryPoint.spirv.empty(), std::format("Entry point '{}' has empty SPIR-V payload.", entryPoint.entryPointName));

            vk::ShaderModuleCreateInfo moduleInfo{};
            moduleInfo.codeSize = static_cast<size_t>(entryPoint.spirv.size() * sizeof(uint32_t));
            moduleInfo.pCode = entryPoint.spirv.data();

            result.modules_.emplace_back(device, moduleInfo);
            result.entryPointNames_.emplace_back(entryPoint.entryPointName.empty() ? "main" : entryPoint.entryPointName);
            result.stages_.emplace_back(entryPoint.stage);
        }

        result.stageCreateInfos_.reserve(result.modules_.size());
        for (size_t i = 0; i < result.modules_.size(); ++i)
        {
            vk::PipelineShaderStageCreateInfo stageInfo{};
            stageInfo.flags = {};
            stageInfo.stage = toVkShaderStage(result.stages_[i]);
            stageInfo.module = *result.modules_[i];
            stageInfo.pName = result.entryPointNames_[i].c_str();
            stageInfo.pSpecializationInfo = nullptr;
            result.stageCreateInfos_.push_back(stageInfo);
        }

        return result;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return !stageCreateInfos_.empty();
    }

    [[nodiscard]] size_t stageCount() const noexcept
    {
        return stageCreateInfos_.size();
    }

    [[nodiscard]] const vk::PipelineShaderStageCreateInfo& stageCreateInfo(uint32_t index) const noexcept
    {
        return stageCreateInfos_[index];
    }

    [[nodiscard]] SlangStage stageKind(uint32_t index) const noexcept
    {
        return stages_[index];
    }

    [[nodiscard]] std::string_view entryPointName(uint32_t index) const noexcept
    {
        return entryPointNames_[index];
    }

    [[nodiscard]] std::vector<uint32_t> collectStageIndices(PipelineType type) const
    {
        std::vector<uint32_t> indices;
        for (size_t i = 0; i < stages_.size(); ++i)
        {
            bool accepted = false;
            switch (type)
            {
            case PipelineType::Graphics:
                accepted = detail::isGraphicsStage(stages_[i]);
                break;
            case PipelineType::Compute:
                accepted = stages_[i] == SLANG_STAGE_COMPUTE;
                break;
            case PipelineType::Mesh:
                accepted = detail::isMeshStage(stages_[i]);
                break;
            case PipelineType::RayTracing:
                accepted = detail::isRayTracingStage(stages_[i]);
                break;
            }

            if (accepted)
            {
                indices.emplace_back(static_cast<uint32_t>(i));
            }
        }
        return indices;
    }

    [[nodiscard]] std::vector<uint32_t> deduplicatePerStageBit(std::span<const uint32_t> stageIndices) const
    {
        std::set<vk::ShaderStageFlagBits> seenStageBits;
        std::vector<uint32_t> deduplicated;
        deduplicated.reserve(stageIndices.size());

        for (auto const index : stageIndices)
        {
            auto const stageBit = stageCreateInfos_[index].stage;
            if (seenStageBits.contains(stageBit))
            {
                continue;
            }

            seenStageBits.insert(stageBit);
            deduplicated.emplace_back(index);
        }
        return deduplicated;
    }

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    std::vector<vk::raii::ShaderModule> modules_;
    std::vector<std::string> entryPointNames_;
    std::vector<SlangStage> stages_;
    std::vector<vk::PipelineShaderStageCreateInfo> stageCreateInfos_;
};

class GraphicsPipeline
{
  public:
    GraphicsPipeline() = default;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) noexcept = default;
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept = default;

    [[nodiscard]] static GraphicsPipeline create(
        const vk::raii::Device& device,
        const ReflectedPipelineLayout& layout,
        const VkShaderProgram& shaderProgram,
        const GraphicsPipelineDesc& desc = {},
        std::optional<std::reference_wrapper<const vk::raii::PipelineCache>> pipelineCache = std::nullopt)
    {
        nrAssert(layout.valid(), "GraphicsPipeline::create requires a valid pipeline layout.");
        nrAssert(shaderProgram.valid(), "GraphicsPipeline::create requires a valid shader program.");

        // Select pipeline type based on desc.mode
        PipelineType pipelineType = (desc.mode == GraphicsPipelineMode::Mesh) ? PipelineType::Mesh : PipelineType::Graphics;
        
        auto stageIndices = shaderProgram.collectStageIndices(pipelineType);
        nrAssert(!stageIndices.empty(), std::format("Pipeline requires at least one {} shader stage.", 
            desc.mode == GraphicsPipelineMode::Mesh ? "mesh" : "graphics"));
        stageIndices = shaderProgram.deduplicatePerStageBit(stageIndices);

        // Validate required shaders based on mode
        if (desc.mode == GraphicsPipelineMode::Mesh)
        {
            bool hasMesh = false;
            for (auto const index : stageIndices)
            {
                if (shaderProgram.stageKind(index) == SLANG_STAGE_MESH)
                {
                    hasMesh = true;
                    break;
                }
            }
            nrAssert(hasMesh, "Mesh pipeline requires a mesh shader entry point.");
        }
        else
        {
            bool hasVertex = false;
            for (auto const index : stageIndices)
            {
                if (shaderProgram.stageKind(index) == SLANG_STAGE_VERTEX)
                {
                    hasVertex = true;
                    break;
                }
            }
            nrAssert(hasVertex, "Graphics pipeline requires a vertex shader entry point.");
        }

        std::vector<vk::PipelineShaderStageCreateInfo> stages;
        stages.reserve(stageIndices.size());
        for (auto const index : stageIndices)
        {
            stages.emplace_back(shaderProgram.stageCreateInfo(index));
        }

        auto colorBlendAttachments = desc.colorBlendAttachments;
        if (colorBlendAttachments.empty())
        {
            colorBlendAttachments.resize(desc.colorAttachmentFormats.size(), detail::defaultBlendAttachment());
        }

        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(desc.colorAttachmentFormats.size());
        renderingInfo.pColorAttachmentFormats = desc.colorAttachmentFormats.empty() ? nullptr : desc.colorAttachmentFormats.data();
        if (desc.depthAttachmentFormat.has_value())
        {
            renderingInfo.depthAttachmentFormat = *desc.depthAttachmentFormat;
        }
        if (desc.stencilAttachmentFormat.has_value())
        {
            renderingInfo.stencilAttachmentFormat = *desc.stencilAttachmentFormat;
        }

        vk::PipelineVertexInputStateCreateInfo vertexInputState{};
        
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
        inputAssemblyState.flags = {};
        inputAssemblyState.topology = desc.topology;
        inputAssemblyState.primitiveRestartEnable = vk::False;
        
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.flags = {};
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr;
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr;
        
        vk::PipelineRasterizationStateCreateInfo rasterizationState{};
        rasterizationState.flags = {};
        rasterizationState.depthClampEnable = vk::False;
        rasterizationState.rasterizerDiscardEnable = vk::False;
        rasterizationState.polygonMode = desc.polygonMode;
        rasterizationState.cullMode = desc.cullMode;
        rasterizationState.frontFace = desc.frontFace;
        rasterizationState.depthBiasEnable = vk::False;
        rasterizationState.depthBiasConstantFactor = 0.0f;
        rasterizationState.depthBiasClamp = 0.0f;
        rasterizationState.depthBiasSlopeFactor = 0.0f;
        rasterizationState.lineWidth = 1.0f;
        
        vk::PipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.flags = {};
        multisampleState.rasterizationSamples = desc.sampleCount;
        multisampleState.sampleShadingEnable = vk::False;
        multisampleState.minSampleShading = 1.0f;
        multisampleState.pSampleMask = nullptr;
        multisampleState.alphaToCoverageEnable = vk::False;
        multisampleState.alphaToOneEnable = vk::False;
        
        vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
        depthStencilState.flags = {};
        depthStencilState.depthTestEnable = desc.depthTestEnable ? vk::True : vk::False;
        depthStencilState.depthWriteEnable = desc.depthWriteEnable ? vk::True : vk::False;
        depthStencilState.depthCompareOp = desc.depthCompareOp;
        depthStencilState.depthBoundsTestEnable = vk::False;
        depthStencilState.stencilTestEnable = vk::False;
        depthStencilState.front = vk::StencilOpState{};
        depthStencilState.back = vk::StencilOpState{};
        depthStencilState.minDepthBounds = 0.0f;
        depthStencilState.maxDepthBounds = 1.0f;
        
        vk::PipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.flags = {};
        colorBlendState.logicOpEnable = vk::False;
        colorBlendState.logicOp = vk::LogicOp::eCopy;
        colorBlendState.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlendState.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();
        for (size_t i = 0; i < 4; ++i)
        {
            colorBlendState.blendConstants[i] = 0.0f;
        }

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        if (!desc.dynamicStates.empty())
        {
            dynamicState.dynamicStateCount = static_cast<uint32_t>(desc.dynamicStates.size());
            dynamicState.pDynamicStates = desc.dynamicStates.data();
        }

        vk::GraphicsPipelineCreateInfo createInfo{};
        createInfo.flags = desc.flags;
        createInfo.stageCount = static_cast<uint32_t>(stages.size());
        createInfo.pStages = stages.data();
        createInfo.pVertexInputState = &vertexInputState;
        createInfo.pInputAssemblyState = &inputAssemblyState;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterizationState;
        createInfo.pMultisampleState = &multisampleState;
        createInfo.pDepthStencilState = (desc.depthAttachmentFormat.has_value() || desc.stencilAttachmentFormat.has_value() || desc.depthTestEnable || desc.depthWriteEnable) ? &depthStencilState : nullptr;
        createInfo.pColorBlendState = &colorBlendState;
        createInfo.pDynamicState = desc.dynamicStates.empty() ? nullptr : &dynamicState;
        createInfo.layout = layout.raw();
        createInfo.renderPass = vk::RenderPass{};
        createInfo.subpass = 0;
        createInfo.basePipelineHandle = vk::Pipeline{};
        createInfo.basePipelineIndex = -1;
        createInfo.pNext = &renderingInfo;

        GraphicsPipeline result;
        if (pipelineCache.has_value())
        {
            result.pipeline_ = std::move(vk::raii::Pipeline(device, pipelineCache->get(), createInfo));
        }
        else
        {
            result.pipeline_ = std::move(vk::raii::Pipeline(device, nullptr, createInfo));
        }
        return result;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return pipeline_ != nullptr;
    }

    [[nodiscard]] const vk::raii::Pipeline& handle() const noexcept
    {
        return pipeline_;
    }

    [[nodiscard]] vk::Pipeline raw() const noexcept
    {
        return *pipeline_;
    }

  private:
    vk::raii::Pipeline pipeline_ = {nullptr};
};

class ComputePipeline
{
  public:
    ComputePipeline() = default;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;
    ComputePipeline(ComputePipeline&&) noexcept = default;
    ComputePipeline& operator=(ComputePipeline&&) noexcept = default;

    [[nodiscard]] static ComputePipeline create(
        const vk::raii::Device& device,
        const ReflectedPipelineLayout& layout,
        const VkShaderProgram& shaderProgram,
        const ComputePipelineDesc& desc = {},
        std::optional<std::reference_wrapper<const vk::raii::PipelineCache>> pipelineCache = std::nullopt)
    {
        nrAssert(layout.valid(), "ComputePipeline::create requires a valid pipeline layout.");
        nrAssert(shaderProgram.valid(), "ComputePipeline::create requires a valid shader program.");

        auto stageIndices = shaderProgram.collectStageIndices(PipelineType::Compute);
        nrAssert(!stageIndices.empty(), "Compute pipeline requires a compute shader entry point.");
        stageIndices = shaderProgram.deduplicatePerStageBit(stageIndices);
        nrAssert(!stageIndices.empty(), "Compute pipeline stage list is empty after deduplication.");

        vk::ComputePipelineCreateInfo createInfo{};
        createInfo.flags = desc.flags;
        createInfo.stage = shaderProgram.stageCreateInfo(stageIndices.front());
        createInfo.layout = layout.raw();
        createInfo.basePipelineHandle = vk::Pipeline{};
        createInfo.basePipelineIndex = -1;

        ComputePipeline result;
        if (pipelineCache.has_value())
        {
            result.pipeline_ = vk::raii::Pipeline(device, pipelineCache->get(), createInfo);
        }
        else
        {
            result.pipeline_ = vk::raii::Pipeline(device, nullptr, createInfo);
        }
        return result;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return pipeline_ != nullptr;
    }

    [[nodiscard]] const vk::raii::Pipeline& handle() const noexcept
    {
        return pipeline_;
    }

    [[nodiscard]] vk::Pipeline raw() const noexcept
    {
        return *pipeline_;
    }

  private:
    vk::raii::Pipeline pipeline_ = {nullptr};
};

// MeshPipeline is now an alias to GraphicsPipeline for convenience
using MeshPipeline = GraphicsPipeline;

class RayTracingPipeline
{
  public:
    RayTracingPipeline() = default;
    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;
    RayTracingPipeline(RayTracingPipeline&&) noexcept = default;
    RayTracingPipeline& operator=(RayTracingPipeline&&) noexcept = default;

    [[nodiscard]] static RayTracingPipeline create(
        const vk::raii::Device& device,
        const ReflectedPipelineLayout& layout,
        const VkShaderProgram& shaderProgram,
        const RayTracingPipelineDesc& desc = {},
        std::optional<std::reference_wrapper<const vk::raii::PipelineCache>> pipelineCache = std::nullopt)
    {
        nrAssert(layout.valid(), "RayTracingPipeline::create requires a valid pipeline layout.");
        nrAssert(shaderProgram.valid(), "RayTracingPipeline::create requires a valid shader program.");

        auto stageIndices = shaderProgram.collectStageIndices(PipelineType::RayTracing);
        nrAssert(!stageIndices.empty(), "Ray tracing pipeline requires at least one ray tracing stage.");

        std::vector<vk::PipelineShaderStageCreateInfo> rtStages;
        rtStages.reserve(stageIndices.size());
        std::vector<SlangStage> rtStageKinds;
        rtStageKinds.reserve(stageIndices.size());
        std::vector<std::string_view> rtStageNames;
        rtStageNames.reserve(stageIndices.size());

        for (auto const index : stageIndices)
        {
            rtStages.emplace_back(shaderProgram.stageCreateInfo(index));
            rtStageKinds.emplace_back(shaderProgram.stageKind(index));
            rtStageNames.emplace_back(shaderProgram.entryPointName(index));
        }

        auto findStageIndex = [&](std::string_view name, std::initializer_list<SlangStage> allowedStages) -> int32_t {
            if (name.empty())
            {
                return static_cast<int32_t>(~0);  // VK_SHADER_UNUSED_KHR
            }

            for (size_t localIndex = 0; localIndex < rtStageNames.size(); ++localIndex)
            {
                if (rtStageNames[localIndex] != name)
                {
                    continue;
                }

                nrAssert(detail::containsStage(allowedStages, rtStageKinds[localIndex]), std::format("Entry point '{}' is present but does not match expected ray tracing shader category.", name));
                return static_cast<int32_t>(localIndex);
            }

            nrAssert(false, std::format("Entry point '{}' is not part of the ray tracing stage list.", name));
            return static_cast<int32_t>(~0);  // VK_SHADER_UNUSED_KHR
        };

        std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;
        if (desc.groups.empty())
        {
            int32_t firstClosestHit = detail::VK_SHADER_UNUSED;
            int32_t firstAnyHit = detail::VK_SHADER_UNUSED;
            int32_t firstIntersection = detail::VK_SHADER_UNUSED;

            for (size_t localIndex = 0; localIndex < rtStageKinds.size(); ++localIndex)
            {
                switch (rtStageKinds[localIndex])
                {
                case SLANG_STAGE_RAY_GENERATION:
                case SLANG_STAGE_MISS:
                case SLANG_STAGE_CALLABLE:
                {
                    vk::RayTracingShaderGroupCreateInfoKHR group{};
                    group.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
                    group.generalShader = static_cast<uint32_t>(localIndex);
                    group.closestHitShader = static_cast<uint32_t>(detail::VK_SHADER_UNUSED);
                    group.anyHitShader = static_cast<uint32_t>(detail::VK_SHADER_UNUSED);
                    group.intersectionShader = static_cast<uint32_t>(detail::VK_SHADER_UNUSED);
                    groups.push_back(group);
                    break;
                }
                case SLANG_STAGE_CLOSEST_HIT:
                    if (firstClosestHit == detail::VK_SHADER_UNUSED)
                    {
                        firstClosestHit = static_cast<int32_t>(localIndex);
                    }
                    break;
                case SLANG_STAGE_ANY_HIT:
                    if (firstAnyHit == detail::VK_SHADER_UNUSED)
                    {
                        firstAnyHit = static_cast<int32_t>(localIndex);
                    }
                    break;
                case SLANG_STAGE_INTERSECTION:
                    if (firstIntersection == detail::VK_SHADER_UNUSED)
                    {
                        firstIntersection = static_cast<int32_t>(localIndex);
                    }
                    break;
                default:
                    break;
                }
            }

            if (firstClosestHit != detail::VK_SHADER_UNUSED || firstAnyHit != detail::VK_SHADER_UNUSED || firstIntersection != detail::VK_SHADER_UNUSED)
            {
                vk::RayTracingShaderGroupCreateInfoKHR group{};
                group.type = (firstIntersection != detail::VK_SHADER_UNUSED) ? vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup : vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
                group.generalShader = static_cast<uint32_t>(detail::VK_SHADER_UNUSED);
                group.closestHitShader = static_cast<uint32_t>(firstClosestHit);
                group.anyHitShader = static_cast<uint32_t>(firstAnyHit);
                group.intersectionShader = static_cast<uint32_t>(firstIntersection);
                groups.push_back(group);
            }
        }
        else
        {
            groups.reserve(desc.groups.size());
            for (auto const& groupDesc : desc.groups)
            {
                auto const generalShader = findStageIndex(groupDesc.generalEntryPoint, {SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_MISS, SLANG_STAGE_CALLABLE});
                auto const closestHitShader = findStageIndex(groupDesc.closestHitEntryPoint, {SLANG_STAGE_CLOSEST_HIT});
                auto const anyHitShader = findStageIndex(groupDesc.anyHitEntryPoint, {SLANG_STAGE_ANY_HIT});
                auto const intersectionShader = findStageIndex(groupDesc.intersectionEntryPoint, {SLANG_STAGE_INTERSECTION});

                vk::RayTracingShaderGroupCreateInfoKHR group{};
                group.type = groupDesc.type;
                group.generalShader = static_cast<uint32_t>(generalShader);
                group.closestHitShader = static_cast<uint32_t>(closestHitShader);
                group.anyHitShader = static_cast<uint32_t>(anyHitShader);
                group.intersectionShader = static_cast<uint32_t>(intersectionShader);
                groups.push_back(group);
            }
        }

        nrAssert(!groups.empty(), "Ray tracing pipeline requires at least one shader group.");

        vk::RayTracingPipelineCreateInfoKHR createInfo{};
        createInfo.flags = desc.flags;
        createInfo.stageCount = static_cast<uint32_t>(rtStages.size());
        createInfo.pStages = rtStages.data();
        createInfo.groupCount = static_cast<uint32_t>(groups.size());
        createInfo.pGroups = groups.data();
        createInfo.maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth;
        createInfo.layout = layout.raw();

        RayTracingPipeline result;
        if (pipelineCache.has_value())
        {
            result.pipeline_ = vk::raii::Pipeline(device, nullptr, pipelineCache->get(), createInfo);
        }
        else
        {
            result.pipeline_ = vk::raii::Pipeline(device, nullptr, nullptr, createInfo);
        }
        return result;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return pipeline_ != nullptr;
    }

    [[nodiscard]] const vk::raii::Pipeline& handle() const noexcept
    {
        return pipeline_;
    }

    [[nodiscard]] vk::Pipeline raw() const noexcept
    {
        return *pipeline_;
    }

  private:
    vk::raii::Pipeline pipeline_ = {nullptr};
};

template <typename TPipeline>
struct PipelineState
{
        ReflectedPipelineLayout layout;
        DescriptorPool descriptorPool;
        TPipeline pipeline;
};

} // namespace nr::rhi
