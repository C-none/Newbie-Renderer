export module nr.renderer:rendererType;

import nr.rhi;
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
    CooperativeVectorConvertRead,
    CooperativeVectorConvertWrite,
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
    ShaderStorageReadWrite,
    VertexRead,
    IndexRead,
    IndirectRead,
    TexelRead,
    TexelWrite,
    TexelReadWrite,
    AccelerationStructureRead,
    AccelerationStructureWrite,
    AccelerationStructureBuildInputRead,
    AccelerationStructureScratchReadWrite,
    ShaderBindingTableRead,
    HostRead,
    HostWrite,
    CooperativeVectorConvertRead,
    CooperativeVectorConvertWrite,
};

enum class AccelerationStructureUsageIntent : std::uint8_t
{
    BuildInput,
    BuildOutput,
    TraceInput,
    CopySource,
    CopyDestination,
};

enum class AccelerationStructureAccessIntent : std::uint8_t
{
    None,
    BuildRead,
    BuildWrite,
    TraceRead,
    CopyRead,
    CopyWrite,
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
    ColorAttachmentReadWrite,
    DepthStencilRead,
    DepthStencilWrite,
    DepthStencilReadWrite,
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

enum class DependencyStrength : std::uint8_t
{
    InOrder,
    BarrierRequired,
    ReleaseAcquireRequired,
};

[[nodiscard]] ResourceOwnershipDomain ownershipDomainFromQueue(QueueDomain queue) noexcept;

[[nodiscard]] nr::rhi::QueueRole rhiQueueRoleFromDomain(QueueDomain queue) noexcept;
} // namespace nr::renderer
