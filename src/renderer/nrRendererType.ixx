module;
export module nr.renderer:rendererType;

import std;

export namespace nr::renderer
{
enum class QueueDomain : std::uint8_t
{
    Graphics,
    Compute,
    Transfer,
};

enum class ResourceLifetime : std::uint8_t
{
    ScenePersistent,
    RendererPersistent,
    FrameLocal,
    GraphTransient,
    SwapchainRelative,
};

enum class ResourceResidency : std::uint8_t
{
    Imported,
    Managed,
    Swapchain,
};

enum class ResourceOwnershipDomain : std::uint8_t
{
    Undefined,
    Graphics,
    Compute,
    Transfer,
};

enum class BufferUsageIntent : std::uint8_t
{
    TransferSrc,
    TransferDst,
    Uniform,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    Vertex,
    Index,
    Indirect,
    ShaderDeviceAddress,
    UniformTexel,
    StorageTexelRead,
    StorageTexelWrite,
    StorageTexelReadWrite,
    AccelerationStructureBuildInput,
    AccelerationStructureStorage,
    AccelerationStructureScratch,
    ShaderBindingTable,
    HostUpload,
    Readback,
};

enum class BufferAccessIntent : std::uint8_t
{
    None,
    TransferRead,
    TransferWrite,
    UniformRead,
    ShaderSampleRead,
    ShaderStorageRead,
    ShaderStorageWrite,
    VertexRead,
    IndexRead,
    IndirectRead,
    TexelRead,
    TexelWrite,
    AccelerationStructureRead,
    AccelerationStructureWrite,
    HostRead,
    HostWrite,
};

enum class ImageUsageIntent : std::uint8_t
{
    TransferSrc,
    TransferDst,
    Sampled,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    TransientAttachment,
    InputAttachment,
    ResolveSrc,
    ResolveDst,
    PresentSource,
    CopySource,
    CopyDestination,
};

enum class ImageAccessIntent : std::uint8_t
{
    None,
    TransferRead,
    TransferWrite,
    SampledRead,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    ColorAttachmentRead,
    ColorAttachmentWrite,
    DepthStencilRead,
    DepthStencilWrite,
    InputAttachmentRead,
    PresentRead,
};

enum class ImageLayoutIntent : std::uint8_t
{
    Undefined,
    General,
    TransferSrc,
    TransferDst,
    ShaderReadOnly,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    PresentSrc,
};

enum class ImageAspectIntent : std::uint8_t
{
    Color,
    Depth,
    Stencil,
    DepthStencil,
};

enum class ShaderStageIntent : std::uint8_t
{
    Vertex,
    Fragment,
    Compute,
    Task,
    Mesh,
    RayGen,
    AnyHit,
    ClosestHit,
    Miss,
    Intersection,
    Callable,
};

enum class DescriptorBindingIntent : std::uint8_t
{
    UniformBuffer,
    StorageBuffer,
    UniformTexelBuffer,
    StorageTexelBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
    AccelerationStructure,
    InlineUniform,
};

enum class AttachmentLoadIntent : std::uint8_t
{
    Load,
    Clear,
    DontCare,
};

enum class AttachmentStoreIntent : std::uint8_t
{
    Store,
    DontCare,
};

enum class ClearValueKind : std::uint8_t
{
    None,
    ColorFloat,
    ColorUint,
    ColorSint,
    DepthStencil,
};

enum class SubmitBoundaryKind : std::uint8_t
{
    Explicit,
    QueueTransition,
    FrameFinal,
};

enum class DependencyStrength : std::uint8_t
{
    InOrder,
    BarrierRequired,
    ReleaseAcquireRequired,
};

[[nodiscard]] inline ResourceOwnershipDomain ownershipDomainFromQueue(QueueDomain queue) noexcept
{
    if (queue == QueueDomain::Graphics)
    {
        return ResourceOwnershipDomain::Graphics;
    }
    if (queue == QueueDomain::Compute)
    {
        return ResourceOwnershipDomain::Compute;
    }
    return ResourceOwnershipDomain::Transfer;
}
} // namespace nr::renderer
