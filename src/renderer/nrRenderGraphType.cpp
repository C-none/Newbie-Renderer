module nr.renderer;
import :renderGraphType;
import dependency.vulkan;
import nr.rhi;
import nr.utils;
import std;
import :rendererType;

namespace nr::renderer
{
[[nodiscard]] bool AccessScope::resolved() const noexcept
{
        return stages != vk::PipelineStageFlags2{};
    }

void RetainedImageState::reset() noexcept
{
        initialized = false;
        layout = ImageLayoutIntent::Undefined;
        ownership = ResourceOwnershipDomain::Undefined;
        access = {};
    }

[[nodiscard]] PassParallelRecordPlan ParallelRecordPlanner::planContiguousRanges(
        std::size_t itemCount,
        std::uint32_t availableRecordWorkers)
{
        auto workPlan = nr::threading::planContiguousRanges(itemCount, availableRecordWorkers);
        return PassParallelRecordPlan{
            .itemCount = workPlan.itemCount,
            .assignedThreadCount = workPlan.assignedWorkerCount,
            .ranges = workPlan.ranges |
                      std::views::transform([](const nr::threading::WorkRange& range) {
                          return ParallelRecordRange{
                              .begin = range.begin,
                              .end = range.end,
                          };
                      }) |
                      std::ranges::to<std::vector>(),
        };
    }
} // namespace nr::renderer

namespace nr::renderer
{
namespace use
{
[[nodiscard]] vk::PipelineStageFlags2 shaderStageScope(ShaderStageIntent intent) noexcept
{
    switch (intent)
    {
    case ShaderStageIntent::Vertex:
        return vk::PipelineStageFlagBits2::eVertexShader;
    case ShaderStageIntent::Fragment:
        return vk::PipelineStageFlagBits2::eFragmentShader;
    case ShaderStageIntent::Compute:
        return vk::PipelineStageFlagBits2::eComputeShader;
    case ShaderStageIntent::Task:
        return vk::PipelineStageFlagBits2::eTaskShaderEXT;
    case ShaderStageIntent::Mesh:
        return vk::PipelineStageFlagBits2::eMeshShaderEXT;
    case ShaderStageIntent::RayGen:
    case ShaderStageIntent::AnyHit:
    case ShaderStageIntent::ClosestHit:
    case ShaderStageIntent::Miss:
    case ShaderStageIntent::Intersection:
    case ShaderStageIntent::Callable:
        return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
    }
    return vk::PipelineStageFlags2{};
}

[[nodiscard]] vk::PipelineStageFlags2 shaderStageScope(std::span<const ShaderStageIntent> intents) noexcept
{
    auto stages = vk::PipelineStageFlags2{};
    std::ranges::for_each(intents, [&](ShaderStageIntent intent) {
        stages |= shaderStageScope(intent);
    });
    return stages;
}

[[nodiscard]] PassResourceUseDesc withShaderStages(
    PassResourceUseDesc use,
    vk::PipelineStageFlags2 stages) noexcept
{
    use.shaderStages = stages;
    return use;
}

[[nodiscard]] PassResourceUseDesc withShaderStages(
    PassResourceUseDesc use,
    ShaderStageIntent stage) noexcept
{
    return withShaderStages(use, shaderStageScope(stage));
}

[[nodiscard]] PassResourceUseDesc withShaderStages(
    PassResourceUseDesc use,
    std::initializer_list<ShaderStageIntent> stages) noexcept
{
    return withShaderStages(
        use,
        shaderStageScope(std::span<const ShaderStageIntent>{stages.begin(), stages.size()}));
}

[[nodiscard]] PassResourceUseDesc orderedAfterPrevious(PassResourceUseDesc use) noexcept
{
    use.requiresPreviousUseBarrier = true;
    return use;
}

[[nodiscard]] PassResourceUseDesc colorRead(GraphResourceHandle resource) noexcept
{
    return make<spec::ColorRead>(resource);
}

[[nodiscard]] PassResourceUseDesc colorWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::ColorWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc colorReadWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::ColorReadWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc depthRead(GraphResourceHandle resource) noexcept
{
    return make<spec::DepthRead>(resource);
}

[[nodiscard]] PassResourceUseDesc depthWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::DepthWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc depthReadWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::DepthReadWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc sampledRead(
    GraphResourceHandle resource,
    ImageAspectIntent aspect) noexcept
{
    auto result = make<spec::SampledRead>(resource);
    if (aspect != ImageAspectIntent::Color)
    {
        result.imageAspect = aspect;
    }
    return result;
}

[[nodiscard]] PassResourceUseDesc storageRead(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageRead>(resource);
}

[[nodiscard]] PassResourceUseDesc storageWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc storageReadWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageReadWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc inputAttachmentRead(
    GraphResourceHandle resource,
    ImageAspectIntent aspect) noexcept
{
    auto result = make<spec::InputAttachmentRead>(resource);
    if (aspect != ImageAspectIntent::Color)
    {
        result.imageAspect = aspect;
    }
    return result;
}

[[nodiscard]] PassResourceUseDesc uniformRead(GraphResourceHandle resource) noexcept
{
    return make<spec::UniformRead>(resource);
}

[[nodiscard]] PassResourceUseDesc bufferTransferSrc(GraphResourceHandle resource) noexcept
{
    return make<spec::BufferTransferSrc>(resource);
}

[[nodiscard]] PassResourceUseDesc bufferTransferDst(GraphResourceHandle resource) noexcept
{
    return make<spec::BufferTransferDst>(resource);
}

[[nodiscard]] PassResourceUseDesc storageBufferRead(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageBufferRead>(resource);
}

[[nodiscard]] PassResourceUseDesc storageBufferWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageBufferWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc storageBufferReadWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageBufferReadWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc vertexRead(GraphResourceHandle resource) noexcept
{
    return make<spec::VertexRead>(resource);
}

[[nodiscard]] PassResourceUseDesc indexRead(GraphResourceHandle resource) noexcept
{
    return make<spec::IndexRead>(resource);
}

[[nodiscard]] PassResourceUseDesc indirectRead(GraphResourceHandle resource) noexcept
{
    return make<spec::IndirectRead>(resource);
}

[[nodiscard]] PassResourceUseDesc uniformTexelRead(GraphResourceHandle resource) noexcept
{
    return make<spec::UniformTexelRead>(resource);
}

[[nodiscard]] PassResourceUseDesc storageTexelRead(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageTexelRead>(resource);
}

[[nodiscard]] PassResourceUseDesc storageTexelWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageTexelWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc storageTexelReadWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::StorageTexelReadWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildInputRead(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureBuildInputRead>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageRead(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureStorageRead>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureStorageWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureScratchWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureScratchWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildRead(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureBuildRead>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureBuildWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureTraceRead(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureTraceRead>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyRead(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureCopyRead>(resource);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::AccelerationStructureCopyWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc shaderBindingTableRead(GraphResourceHandle resource) noexcept
{
    return make<spec::ShaderBindingTableRead>(resource);
}

[[nodiscard]] PassResourceUseDesc hostUploadRead(GraphResourceHandle resource) noexcept
{
    return make<spec::HostUploadRead>(resource);
}

[[nodiscard]] PassResourceUseDesc readbackWrite(GraphResourceHandle resource) noexcept
{
    return make<spec::ReadbackWrite>(resource);
}

[[nodiscard]] PassResourceUseDesc imageTransferSrc(GraphResourceHandle resource) noexcept
{
    return make<spec::ImageTransferSrc>(resource);
}

[[nodiscard]] PassResourceUseDesc imageTransferDst(GraphResourceHandle resource) noexcept
{
    return make<spec::ImageTransferDst>(resource);
}

[[nodiscard]] PassResourceUseDesc copySource(GraphResourceHandle resource) noexcept
{
    return make<spec::CopySource>(resource);
}

[[nodiscard]] PassResourceUseDesc copyDestination(GraphResourceHandle resource) noexcept
{
    return make<spec::CopyDestination>(resource);
}

[[nodiscard]] PassResourceUseDesc resolveSrc(GraphResourceHandle resource) noexcept
{
    return make<spec::ResolveSrc>(resource);
}

[[nodiscard]] PassResourceUseDesc resolveDst(GraphResourceHandle resource) noexcept
{
    return make<spec::ResolveDst>(resource);
}

[[nodiscard]] PassResourceUseDesc presentRead(GraphResourceHandle resource) noexcept
{
    return make<spec::PresentRead>(resource);
}

[[nodiscard]] PassResourceUseDesc presentRead(
    GraphResourceHandle resource,
    ResourceOwnershipDomain ownershipDomain) noexcept
{
    return make<spec::PresentRead>(resource, ImageUseOptions{
                                                 .ownershipDomain = ownershipDomain,
                                             });
}
} // namespace use
} // namespace nr::renderer
