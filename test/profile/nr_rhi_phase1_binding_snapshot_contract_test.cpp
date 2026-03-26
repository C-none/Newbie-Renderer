import std;
import nr.rhi;

namespace
{
[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }
    return true;
}

[[nodiscard]] nr::rhi::SlangProgram compileProgram(std::string_view sourcePath)
{
    auto request = nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path(sourcePath),
    };
    return nr::rhi::ShaderService::instance().compileProgramByFile(request);
}

[[nodiscard]] std::optional<uint64_t> extractLogicalResourceId(const nr::rhi::ShaderBindingRecord &record)
{
    return std::visit(
        [](const auto &payload) -> std::optional<uint64_t> {
            using PayloadT = std::remove_cvref_t<decltype(payload)>;
            if constexpr (std::same_as<PayloadT, nr::rhi::LogicalResourceDescriptorWrite>)
            {
                return payload.logicalResourceId;
            }
            else
            {
                return std::nullopt;
            }
        },
        record.payload);
}

[[nodiscard]] bool verifySnapshotContracts()
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();

    auto program = compileProgram("test/main/resourceBindingReflection");
    if (!require(program.valid(), "phase1 contract test failed to compile test/main/resourceBindingReflection."))
    {
        return false;
    }

    auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
    if (!require(layout.valid(), "phase1 contract test failed to build descriptor layout."))
    {
        return false;
    }

    auto root = layout.rootCursor();
    if (!require(root.valid(), "phase1 contract test requires a valid root cursor."))
    {
        return false;
    }

    auto rootCopy = root;

    if (!require(root["tex2d"].setObject(nr::rhi::LogicalResourceDescriptorWrite{
                     .logicalResourceId = 11u,
                     .debugName = "tex2d.first",
                     .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                 }),
                 "failed to capture tex2d logical binding from root cursor."))
    {
        return false;
    }

    if (!require(rootCopy["tex2d"].setObject(nr::rhi::LogicalResourceDescriptorWrite{
                     .logicalResourceId = 22u,
                     .debugName = "tex2d.override",
                     .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                 }),
                 "failed to capture tex2d logical override from copied root cursor."))
    {
        return false;
    }

    if (!require(root["rwTex2d"].setObject(nr::rhi::LogicalResourceDescriptorWrite{
                     .logicalResourceId = 33u,
                     .debugName = "rwTex2d.logical",
                     .imageLayout = vk::ImageLayout::eGeneral,
                 }),
                 "failed to capture rwTex2d logical binding."))
    {
        return false;
    }

    if (!require(root["rawBuffer"].setObject(nr::rhi::LogicalResourceDescriptorWrite{
                     .logicalResourceId = 44u,
                     .debugName = "rawBuffer.logical",
                     .offset = 16,
                     .range = 64,
                 }),
                 "failed to capture rawBuffer logical binding."))
    {
        return false;
    }

    if (!require(rootCopy["linearSampler"].setObject(nr::rhi::LogicalResourceDescriptorWrite{
                     .logicalResourceId = 55u,
                     .debugName = "linearSampler.logical",
                 }),
                 "failed to capture linearSampler logical binding."))
    {
        return false;
    }

    if (!require(root["typedBuffer"].setObject(nr::rhi::LogicalResourceDescriptorWrite{
                     .logicalResourceId = 66u,
                     .debugName = "typedBuffer.logical",
                 }),
                 "failed to capture typedBuffer logical binding."))
    {
        return false;
    }

    if (!require(root["pushData"]["bias"].setData(uint32_t{7u}), "failed to capture pushData.bias from root cursor."))
    {
        return false;
    }

    if (!require(rootCopy["pushData"]["bias"].setData(uint32_t{9u}), "failed to overwrite pushData.bias from copied root cursor."))
    {
        return false;
    }

    if (!require(root["pushData"]["scale"].setData(1.25f), "failed to capture pushData.scale from root cursor."))
    {
        return false;
    }

    auto snapshot = root.snapshot();

    if (!require(snapshot.descriptorWriteCount() == 5u, "descriptor snapshot should contain 5 deduplicated records."))
    {
        return false;
    }

    if (!require(snapshot.pushConstantWriteCount() == 2u, "push-constant snapshot should contain bias + scale records."))
    {
        return false;
    }

    auto sampledImageLogicalId = uint64_t{0};
    auto sampledImageCount = size_t{0};

    std::ranges::for_each(snapshot.descriptorWrites(), [&](const nr::rhi::ShaderBindingRecord &record) {
        if (record.binding.descriptorType != vk::DescriptorType::eSampledImage)
        {
            return;
        }

        ++sampledImageCount;
        auto logicalId = extractLogicalResourceId(record);
        if (logicalId.has_value())
        {
            sampledImageLogicalId = *logicalId;
        }
    });

    if (!require(sampledImageCount == 1u && sampledImageLogicalId == 22u,
                 "copied cursor writes should coalesce into one sampled-image record with latest logical id."))
    {
        return false;
    }

    auto pushBiasOverwritten = false;
    std::ranges::for_each(snapshot.pushConstantWrites(), [&](const nr::rhi::PushConstantWriteRecord &record) {
        if (record.offset != 0u || record.data.size() != sizeof(uint32_t))
        {
            return;
        }

        auto value = uint32_t{0};
        std::memcpy(&value, record.data.data(), sizeof(uint32_t));
        if (value == 9u)
        {
            pushBiasOverwritten = true;
        }
    });

    if (!require(pushBiasOverwritten, "copied cursor pushData.bias write should overwrite previous value."))
    {
        return false;
    }

    auto logicalResolver = [](const nr::rhi::LogicalResourceDescriptorWrite &logical,
                              const nr::rhi::DescriptorBindingInfo &binding,
                              uint32_t) -> std::optional<nr::rhi::DescriptorWritePayload> {
        switch (binding.descriptorType)
        {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eUniformBufferDynamic:
        case vk::DescriptorType::eStorageBuffer:
            return nr::rhi::DescriptorWritePayload{
                nr::rhi::BufferDescriptorWrite{
                    .buffer = {},
                    .offset = logical.offset,
                    .range = logical.range,
                }};

        case vk::DescriptorType::eUniformTexelBuffer:
        case vk::DescriptorType::eStorageTexelBuffer:
            return nr::rhi::DescriptorWritePayload{nr::rhi::TexelBufferDescriptorWrite{.view = {}}};

        case vk::DescriptorType::eSampledImage:
        case vk::DescriptorType::eStorageImage:
        case vk::DescriptorType::eInputAttachment:
        case vk::DescriptorType::eSampler:
        case vk::DescriptorType::eCombinedImageSampler:
            return nr::rhi::DescriptorWritePayload{
                nr::rhi::ImageDescriptorWrite{
                    .imageView = {},
                    .imageLayout = logical.imageLayout,
                    .sampler = logical.sampler,
                }};

        case vk::DescriptorType::eAccelerationStructureKHR:
            return nr::rhi::DescriptorWritePayload{
                nr::rhi::AccelerationStructureDescriptorWrite{.accelerationStructure = {}}};

        default:
            return std::nullopt;
        }
    };

    auto resolvedWriteRequests = nr::rhi::resolveDescriptorWriteRequests(snapshot, logicalResolver);
    if (!require(resolvedWriteRequests.size() == snapshot.descriptorWriteCount(),
                 "resolved descriptor requests should match snapshot descriptor record count."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    try
    {
        if (!verifySnapshotContracts())
        {
            std::println("[FAIL] rhi phase1 binding snapshot contract test failed");
            return 1;
        }

        std::println("[OK] rhi phase1 binding snapshot contract test passed");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::println("[error] exception: {}", error.what());
        return 2;
    }
}
