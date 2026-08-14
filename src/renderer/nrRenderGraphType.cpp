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

void RetainedExternalResourceState::reset() noexcept
{
    initialized = false;
    ownership = ResourceOwnershipDomain::Undefined;
    access = {};
    lastSubmissionTimelineValue = 0;
}

void RetainedBufferState::reset() noexcept
{
    common.reset();
}

void RetainedImageState::reset() noexcept
{
    common.reset();
    layout = ImageLayoutIntent::Undefined;
}

void RetainedAccelerationStructureState::reset() noexcept
{
    common.reset();
}

[[nodiscard]] PassParallelRecordPlan ParallelRecordPlanner::planContiguousRanges(std::size_t itemCount,
                                                                                 std::uint32_t availableRecordWorkers)
{
    auto workPlan = nr::threading::planContiguousRanges(itemCount, availableRecordWorkers);
    return PassParallelRecordPlan{
        .itemCount = workPlan.itemCount,
        .assignedThreadCount = workPlan.assignedWorkerCount,
        .ranges = workPlan.ranges | std::views::transform([](const nr::threading::WorkRange &range) {
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
namespace
{
[[nodiscard]] PassResourceUseDesc imageUse(GraphResourceHandle resource, ImageUsageIntent usage,
                                            ImageAccessIntent access, ImageLayoutIntent layout,
                                            ImageAspectIntent aspect = ImageAspectIntent::Color,
                                            ResourceOwnershipDomain ownershipDomain =
                                                ResourceOwnershipDomain::Undefined) noexcept
{
    auto result = PassResourceUseDesc{
        .resource = resource,
        .imageUsage = usage,
        .imageAccess = access,
        .imageLayout = layout,
        .imageAspect = aspect,
    };
    if (ownershipDomain != ResourceOwnershipDomain::Undefined)
    {
        result.ownershipDomain = ownershipDomain;
    }
    return result;
}

[[nodiscard]] PassResourceUseDesc bufferUse(GraphResourceHandle resource, BufferUsageIntent usage,
                                             BufferAccessIntent access,
                                             ResourceOwnershipDomain ownershipDomain =
                                                 ResourceOwnershipDomain::Undefined) noexcept
{
    auto result = PassResourceUseDesc{
        .resource = resource,
        .bufferUsage = usage,
        .bufferAccess = access,
    };
    if (ownershipDomain != ResourceOwnershipDomain::Undefined)
    {
        result.ownershipDomain = ownershipDomain;
    }
    return result;
}

[[nodiscard]] PassResourceUseDesc accelerationStructureUse(
    GraphResourceHandle resource, AccelerationStructureUsageIntent usage,
    AccelerationStructureAccessIntent access,
    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined) noexcept
{
    auto result = PassResourceUseDesc{
        .resource = resource,
        .accelerationStructureUsage = usage,
        .accelerationStructureAccess = access,
    };
    if (ownershipDomain != ResourceOwnershipDomain::Undefined)
    {
        result.ownershipDomain = ownershipDomain;
    }
    return result;
}
} // namespace

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
    std::ranges::for_each(intents, [&](ShaderStageIntent intent) { stages |= shaderStageScope(intent); });
    return stages;
}

[[nodiscard]] PassResourceUseDesc withShaderStages(PassResourceUseDesc use, vk::PipelineStageFlags2 stages) noexcept
{
    use.shaderStages = stages;
    return use;
}

[[nodiscard]] PassResourceUseDesc withShaderStages(PassResourceUseDesc use, ShaderStageIntent stage) noexcept
{
    return withShaderStages(use, shaderStageScope(stage));
}

[[nodiscard]] PassResourceUseDesc withShaderStages(PassResourceUseDesc use,
                                                   std::initializer_list<ShaderStageIntent> stages) noexcept
{
    return withShaderStages(use, shaderStageScope(std::span<const ShaderStageIntent>{stages.begin(), stages.size()}));
}

[[nodiscard]] PassResourceUseDesc orderedAfterPrevious(PassResourceUseDesc use) noexcept
{
    use.requiresPreviousUseBarrier = true;
    return use;
}

[[nodiscard]] PassResourceUseDesc colorRead(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::ColorAttachment, ImageAccessIntent::ColorAttachmentRead,
                    ImageLayoutIntent::ColorAttachment);
}

[[nodiscard]] PassResourceUseDesc colorWrite(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::ColorAttachment, ImageAccessIntent::ColorAttachmentWrite,
                    ImageLayoutIntent::ColorAttachment);
}

[[nodiscard]] PassResourceUseDesc colorReadWrite(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::ColorAttachment, ImageAccessIntent::ColorAttachmentReadWrite,
                    ImageLayoutIntent::ColorAttachment);
}

[[nodiscard]] PassResourceUseDesc depthRead(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::DepthStencilReadOnly, ImageAccessIntent::DepthStencilRead,
                    ImageLayoutIntent::DepthStencilReadOnly, ImageAspectIntent::Depth);
}

[[nodiscard]] PassResourceUseDesc depthWrite(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::DepthStencilAttachment, ImageAccessIntent::DepthStencilWrite,
                    ImageLayoutIntent::DepthStencilAttachment, ImageAspectIntent::Depth);
}

[[nodiscard]] PassResourceUseDesc depthReadWrite(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::DepthStencilAttachment, ImageAccessIntent::DepthStencilReadWrite,
                    ImageLayoutIntent::DepthStencilAttachment, ImageAspectIntent::Depth);
}

[[nodiscard]] PassResourceUseDesc sampledRead(GraphResourceHandle resource, ImageAspectIntent aspect) noexcept
{
    return imageUse(resource, ImageUsageIntent::Sampled, ImageAccessIntent::SampledRead,
                    ImageLayoutIntent::ShaderReadOnly, aspect);
}

[[nodiscard]] PassResourceUseDesc storageRead(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::StorageRead, ImageAccessIntent::StorageRead,
                    ImageLayoutIntent::General);
}

[[nodiscard]] PassResourceUseDesc storageWrite(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::StorageWrite, ImageAccessIntent::StorageWrite,
                    ImageLayoutIntent::General);
}

[[nodiscard]] PassResourceUseDesc storageReadWrite(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::StorageReadWrite, ImageAccessIntent::StorageReadWrite,
                    ImageLayoutIntent::General);
}

[[nodiscard]] PassResourceUseDesc inputAttachmentRead(GraphResourceHandle resource, ImageAspectIntent aspect) noexcept
{
    return imageUse(resource, ImageUsageIntent::InputAttachment, ImageAccessIntent::InputAttachmentRead,
                    ImageLayoutIntent::ShaderReadOnly, aspect);
}

[[nodiscard]] PassResourceUseDesc uniformRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::Uniform, BufferAccessIntent::UniformRead);
}

[[nodiscard]] PassResourceUseDesc bufferTransferSrc(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::TransferSrc, BufferAccessIntent::TransferRead);
}

[[nodiscard]] PassResourceUseDesc bufferTransferDst(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::TransferDst, BufferAccessIntent::TransferWrite);
}

[[nodiscard]] PassResourceUseDesc cooperativeVectorConvertRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::CooperativeVectorConvertRead,
                     BufferAccessIntent::CooperativeVectorConvertRead);
}

[[nodiscard]] PassResourceUseDesc cooperativeVectorConvertWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::CooperativeVectorConvertWrite,
                     BufferAccessIntent::CooperativeVectorConvertWrite);
}

[[nodiscard]] PassResourceUseDesc storageBufferRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::StorageRead, BufferAccessIntent::ShaderStorageRead);
}

[[nodiscard]] PassResourceUseDesc storageBufferWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::StorageWrite, BufferAccessIntent::ShaderStorageWrite);
}

[[nodiscard]] PassResourceUseDesc storageBufferReadWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::StorageReadWrite, BufferAccessIntent::ShaderStorageReadWrite);
}

[[nodiscard]] PassResourceUseDesc vertexRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::Vertex, BufferAccessIntent::VertexRead);
}

[[nodiscard]] PassResourceUseDesc indexRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::Index, BufferAccessIntent::IndexRead);
}

[[nodiscard]] PassResourceUseDesc indirectRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::Indirect, BufferAccessIntent::IndirectRead);
}

[[nodiscard]] PassResourceUseDesc uniformTexelRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::UniformTexel, BufferAccessIntent::TexelRead);
}

[[nodiscard]] PassResourceUseDesc storageTexelRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::StorageTexelRead, BufferAccessIntent::TexelRead);
}

[[nodiscard]] PassResourceUseDesc storageTexelWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::StorageTexelWrite, BufferAccessIntent::TexelWrite);
}

[[nodiscard]] PassResourceUseDesc storageTexelReadWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::StorageTexelReadWrite, BufferAccessIntent::TexelReadWrite);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildInputRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::AccelerationStructureBuildInput,
                     BufferAccessIntent::AccelerationStructureBuildInputRead);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::AccelerationStructureStorage,
                     BufferAccessIntent::AccelerationStructureRead);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::AccelerationStructureStorage,
                     BufferAccessIntent::AccelerationStructureWrite);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureScratchWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::AccelerationStructureScratch,
                     BufferAccessIntent::AccelerationStructureScratchReadWrite);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildRead(GraphResourceHandle resource) noexcept
{
    return accelerationStructureUse(resource, AccelerationStructureUsageIntent::BuildInput,
                                    AccelerationStructureAccessIntent::BuildRead);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildWrite(GraphResourceHandle resource) noexcept
{
    return accelerationStructureUse(resource, AccelerationStructureUsageIntent::BuildOutput,
                                    AccelerationStructureAccessIntent::BuildWrite);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureTraceRead(GraphResourceHandle resource) noexcept
{
    return accelerationStructureUse(resource, AccelerationStructureUsageIntent::TraceInput,
                                    AccelerationStructureAccessIntent::TraceRead);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyRead(GraphResourceHandle resource) noexcept
{
    return accelerationStructureUse(resource, AccelerationStructureUsageIntent::CopySource,
                                    AccelerationStructureAccessIntent::CopyRead);
}

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyWrite(GraphResourceHandle resource) noexcept
{
    return accelerationStructureUse(resource, AccelerationStructureUsageIntent::CopyDestination,
                                    AccelerationStructureAccessIntent::CopyWrite);
}

[[nodiscard]] PassResourceUseDesc shaderBindingTableRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::ShaderBindingTable, BufferAccessIntent::ShaderBindingTableRead);
}

[[nodiscard]] PassResourceUseDesc hostUploadRead(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::HostUpload, BufferAccessIntent::TransferRead);
}

[[nodiscard]] PassResourceUseDesc readbackWrite(GraphResourceHandle resource) noexcept
{
    return bufferUse(resource, BufferUsageIntent::Readback, BufferAccessIntent::TransferWrite);
}

[[nodiscard]] PassResourceUseDesc imageTransferSrc(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::TransferSrc, ImageAccessIntent::TransferRead,
                    ImageLayoutIntent::TransferSrc);
}

[[nodiscard]] PassResourceUseDesc imageTransferDst(GraphResourceHandle resource, ImageAspectIntent aspect) noexcept
{
    return imageUse(resource, ImageUsageIntent::TransferDst, ImageAccessIntent::TransferWrite,
                    ImageLayoutIntent::TransferDst, aspect);
}

[[nodiscard]] PassResourceUseDesc copySource(GraphResourceHandle resource, ImageAspectIntent aspect) noexcept
{
    return imageUse(resource, ImageUsageIntent::CopySource, ImageAccessIntent::TransferRead,
                    ImageLayoutIntent::TransferSrc, aspect);
}

[[nodiscard]] PassResourceUseDesc copyDestination(GraphResourceHandle resource, ImageAspectIntent aspect) noexcept
{
    return imageUse(resource, ImageUsageIntent::CopyDestination, ImageAccessIntent::TransferWrite,
                    ImageLayoutIntent::TransferDst, aspect);
}

[[nodiscard]] PassResourceUseDesc resolveSrc(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::ResolveSrc, ImageAccessIntent::TransferRead,
                    ImageLayoutIntent::TransferSrc);
}

[[nodiscard]] PassResourceUseDesc resolveDst(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::ResolveDst, ImageAccessIntent::TransferWrite,
                    ImageLayoutIntent::TransferDst);
}

[[nodiscard]] PassResourceUseDesc presentRead(GraphResourceHandle resource) noexcept
{
    return imageUse(resource, ImageUsageIntent::PresentSource, ImageAccessIntent::PresentRead,
                    ImageLayoutIntent::PresentSrc);
}

[[nodiscard]] PassResourceUseDesc presentRead(GraphResourceHandle resource,
                                              ResourceOwnershipDomain ownershipDomain) noexcept
{
    return imageUse(resource, ImageUsageIntent::PresentSource, ImageAccessIntent::PresentRead,
                    ImageLayoutIntent::PresentSrc, ImageAspectIntent::Color, ownershipDomain);
}
} // namespace use
} // namespace nr::renderer
