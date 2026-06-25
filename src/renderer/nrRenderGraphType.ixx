export module nr.renderer:renderGraphType;
import dependency.vulkan;

import nr.rhi;
import nr.utils;
import std;
import :rendererType;

export namespace nr::renderer
{
inline constexpr std::uint32_t kInvalidGraphHandleValue = std::numeric_limits<std::uint32_t>::max();

template <typename TTag>
struct GraphHandle
{
    std::uint32_t value = kInvalidGraphHandleValue;

    [[nodiscard]] bool valid() const noexcept
    {
        return value != kInvalidGraphHandleValue;
    }

    auto operator<=>(const GraphHandle&) const = default;
};

using GraphResourceHandle = GraphHandle<struct GraphResourceTag>;
using GraphFrameDataHandle = GraphHandle<struct GraphFrameDataTag>;
using GraphPassHandle = GraphHandle<struct GraphPassTag>;
using GraphNodeHandle = GraphHandle<struct GraphNodeTag>;
using GraphSubmitHandle = GraphHandle<struct GraphSubmitTag>;

struct GraphFrameDataDesc
{
    GraphFrameDataHandle handle{};
    std::string debugName{};
    std::any payload{};
};

struct GraphImportedResourceDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
};

struct GraphImportedBufferDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    vk::DeviceSize size = 0;
    std::vector<BufferUsageIntent> usageIntents{};

    /// Optional reference to a pre-allocated buffer resource held by the node.
    /// When set, the render graph executor will use this buffer directly instead of
    /// looking it up in the importedBuffers map. This enables nodes to pre-allocate
    /// per-frame-slot resources at initialize time and import them into the graph
    /// at build time, avoiding runtime memory allocations.
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> importedResource{};
};

struct GraphImportedImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
    std::vector<ImageUsageIntent> usageIntents{};
    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    ImageAspectIntent aspect = ImageAspectIntent::Color;

    /// Optional reference to a pre-allocated image resource held by the node.
    /// When set, the render graph executor will use this image directly instead of
    /// looking it up in the importedImages map. This enables nodes to pre-allocate
    /// per-frame-slot resources at initialize time and import them into the graph
    /// at build time, avoiding runtime memory allocations.
    std::optional<std::reference_wrapper<const nr::rhi::Image>> importedResource{};
};

struct GraphImportedAccelerationStructureDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::ScenePersistent;
    ResourceResidency residency = ResourceResidency::Imported;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eTopLevel;
    vk::DeviceSize size = 0;
    std::vector<AccelerationStructureUsageIntent> usageIntents{};

    /// Optional reference to a pre-built acceleration structure held by the node or renderer cache.
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>> importedResource{};
};

struct GraphImportedSwapchainImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::SwapchainRelative;
    ResourceResidency residency = ResourceResidency::Swapchain;
    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Compute;
    std::uint32_t swapchainImageIndex = 0;
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
};

struct GraphTransientBufferDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    vk::DeviceSize size = 0;
    std::vector<BufferUsageIntent> usageIntents{};
    nr::rhi::MemoryUsage memoryUsage = nr::rhi::MemoryUsage::GpuOnly;
};

struct GraphTransientImageDesc
{
    std::string debugName{};
    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    vk::Extent3D extent{1, 1, 1};
    vk::Format format = vk::Format::eUndefined;
    std::vector<ImageUsageIntent> usageIntents{};
    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    ImageAspectIntent aspect = ImageAspectIntent::Color;
};

using GraphResourceDescVariant = std::variant<
    GraphImportedBufferDesc,
    GraphImportedImageDesc,
    GraphImportedAccelerationStructureDesc,
    GraphImportedSwapchainImageDesc,
    GraphTransientBufferDesc,
    GraphTransientImageDesc>;

struct GraphResourceDesc
{
    GraphResourceHandle handle{};
    GraphResourceDescVariant desc{};
};

struct PassResourceUseDesc
{
    GraphResourceHandle resource{};

    std::optional<BufferUsageIntent> bufferUsage{};
    std::optional<BufferAccessIntent> bufferAccess{};

    std::optional<AccelerationStructureUsageIntent> accelerationStructureUsage{};
    std::optional<AccelerationStructureAccessIntent> accelerationStructureAccess{};

    std::optional<ImageUsageIntent> imageUsage{};
    std::optional<ImageAccessIntent> imageAccess{};
    std::optional<ImageLayoutIntent> imageLayout{};
    std::optional<ImageAspectIntent> imageAspect{};

    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
    bool readOnly = false;
};

namespace use
{
struct ImageUseSpecDesc
{
    ImageUsageIntent usage;
    ImageAccessIntent access;
    ImageLayoutIntent layout;
    ImageAspectIntent aspect = ImageAspectIntent::Color;
    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
    bool readOnly = false;
};

struct BufferUseSpecDesc
{
    BufferUsageIntent usage;
    BufferAccessIntent access;
    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
    bool readOnly = false;
};

struct AccelerationStructureUseSpecDesc
{
    AccelerationStructureUsageIntent usage;
    AccelerationStructureAccessIntent access;
    ResourceOwnershipDomain ownershipDomain = ResourceOwnershipDomain::Undefined;
    bool readOnly = false;
};

struct ImageUseOptions
{
    std::optional<ImageAspectIntent> aspect{};
    std::optional<ResourceOwnershipDomain> ownershipDomain{};
};

struct BufferUseOptions
{
    std::optional<ResourceOwnershipDomain> ownershipDomain{};
};

template <typename TSpec>
concept ImageUseSpec = requires { TSpec::imageUse; } &&
                       std::same_as<std::remove_cvref_t<decltype(TSpec::imageUse)>, ImageUseSpecDesc>;

template <typename TSpec>
concept BufferUseSpec = requires { TSpec::bufferUse; } &&
                        std::same_as<std::remove_cvref_t<decltype(TSpec::bufferUse)>, BufferUseSpecDesc>;

template <typename TSpec>
concept AccelerationStructureUseSpec = requires { TSpec::accelerationStructureUse; } &&
                                       std::same_as<std::remove_cvref_t<decltype(TSpec::accelerationStructureUse)>, AccelerationStructureUseSpecDesc>;

namespace spec
{
struct ColorRead
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::ColorAttachment,
        .access = ImageAccessIntent::ColorAttachmentRead,
        .layout = ImageLayoutIntent::ColorAttachment,
        .readOnly = true,
    };
};

struct ColorWrite
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::ColorAttachment,
        .access = ImageAccessIntent::ColorAttachmentWrite,
        .layout = ImageLayoutIntent::ColorAttachment,
    };
};

struct ColorReadWrite
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::ColorAttachment,
        .access = ImageAccessIntent::ColorAttachmentReadWrite,
        .layout = ImageLayoutIntent::ColorAttachment,
    };
};

struct DepthRead
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::DepthStencilReadOnly,
        .access = ImageAccessIntent::DepthStencilRead,
        .layout = ImageLayoutIntent::DepthStencilReadOnly,
        .aspect = ImageAspectIntent::Depth,
        .readOnly = true,
    };
};

struct DepthWrite
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::DepthStencilAttachment,
        .access = ImageAccessIntent::DepthStencilWrite,
        .layout = ImageLayoutIntent::DepthStencilAttachment,
        .aspect = ImageAspectIntent::Depth,
    };
};

struct DepthReadWrite
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::DepthStencilAttachment,
        .access = ImageAccessIntent::DepthStencilReadWrite,
        .layout = ImageLayoutIntent::DepthStencilAttachment,
        .aspect = ImageAspectIntent::Depth,
    };
};

struct SampledRead
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::Sampled,
        .access = ImageAccessIntent::SampledRead,
        .layout = ImageLayoutIntent::ShaderReadOnly,
        .readOnly = true,
    };
};

struct StorageRead
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::StorageRead,
        .access = ImageAccessIntent::StorageRead,
        .layout = ImageLayoutIntent::General,
        .readOnly = true,
    };
};

struct StorageWrite
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::StorageWrite,
        .access = ImageAccessIntent::StorageWrite,
        .layout = ImageLayoutIntent::General,
    };
};

struct StorageReadWrite
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::StorageReadWrite,
        .access = ImageAccessIntent::StorageReadWrite,
        .layout = ImageLayoutIntent::General,
    };
};

struct InputAttachmentRead
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::InputAttachment,
        .access = ImageAccessIntent::InputAttachmentRead,
        .layout = ImageLayoutIntent::ShaderReadOnly,
        .readOnly = true,
    };
};

struct ImageTransferSrc
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::TransferSrc,
        .access = ImageAccessIntent::TransferRead,
        .layout = ImageLayoutIntent::TransferSrc,
        .readOnly = true,
    };
};

struct ImageTransferDst
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::TransferDst,
        .access = ImageAccessIntent::TransferWrite,
        .layout = ImageLayoutIntent::TransferDst,
    };
};

struct CopySource
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::CopySource,
        .access = ImageAccessIntent::TransferRead,
        .layout = ImageLayoutIntent::TransferSrc,
        .readOnly = true,
    };
};

struct CopyDestination
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::CopyDestination,
        .access = ImageAccessIntent::TransferWrite,
        .layout = ImageLayoutIntent::TransferDst,
    };
};

struct ResolveSrc
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::ResolveSrc,
        .access = ImageAccessIntent::TransferRead,
        .layout = ImageLayoutIntent::TransferSrc,
        .readOnly = true,
    };
};

struct ResolveDst
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::ResolveDst,
        .access = ImageAccessIntent::TransferWrite,
        .layout = ImageLayoutIntent::TransferDst,
    };
};

struct PresentRead
{
    static constexpr ImageUseSpecDesc imageUse{
        .usage = ImageUsageIntent::PresentSource,
        .access = ImageAccessIntent::PresentRead,
        .layout = ImageLayoutIntent::PresentSrc,
        .readOnly = true,
    };
};

struct UniformRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::Uniform,
        .access = BufferAccessIntent::UniformRead,
        .readOnly = true,
    };
};

struct BufferTransferSrc
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::TransferSrc,
        .access = BufferAccessIntent::TransferRead,
        .readOnly = true,
    };
};

struct BufferTransferDst
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::TransferDst,
        .access = BufferAccessIntent::TransferWrite,
    };
};

struct StorageBufferRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::StorageRead,
        .access = BufferAccessIntent::ShaderStorageRead,
        .readOnly = true,
    };
};

struct StorageBufferWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::StorageWrite,
        .access = BufferAccessIntent::ShaderStorageWrite,
    };
};

struct StorageBufferReadWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::StorageReadWrite,
        .access = BufferAccessIntent::ShaderStorageReadWrite,
    };
};

struct VertexRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::Vertex,
        .access = BufferAccessIntent::VertexRead,
        .readOnly = true,
    };
};

struct IndexRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::Index,
        .access = BufferAccessIntent::IndexRead,
        .readOnly = true,
    };
};

struct IndirectRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::Indirect,
        .access = BufferAccessIntent::IndirectRead,
        .readOnly = true,
    };
};

struct UniformTexelRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::UniformTexel,
        .access = BufferAccessIntent::TexelRead,
        .readOnly = true,
    };
};

struct StorageTexelRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::StorageTexelRead,
        .access = BufferAccessIntent::TexelRead,
        .readOnly = true,
    };
};

struct StorageTexelWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::StorageTexelWrite,
        .access = BufferAccessIntent::TexelWrite,
    };
};

struct StorageTexelReadWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::StorageTexelReadWrite,
        .access = BufferAccessIntent::TexelReadWrite,
    };
};

struct AccelerationStructureBuildInputRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::AccelerationStructureBuildInput,
        .access = BufferAccessIntent::AccelerationStructureRead,
        .readOnly = true,
    };
};

struct AccelerationStructureStorageRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::AccelerationStructureStorage,
        .access = BufferAccessIntent::AccelerationStructureRead,
        .readOnly = true,
    };
};

struct AccelerationStructureStorageWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::AccelerationStructureStorage,
        .access = BufferAccessIntent::AccelerationStructureWrite,
    };
};

struct AccelerationStructureScratchWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::AccelerationStructureScratch,
        .access = BufferAccessIntent::AccelerationStructureWrite,
    };
};

struct AccelerationStructureBuildRead
{
    static constexpr AccelerationStructureUseSpecDesc accelerationStructureUse{
        .usage = AccelerationStructureUsageIntent::BuildInput,
        .access = AccelerationStructureAccessIntent::BuildRead,
        .readOnly = true,
    };
};

struct AccelerationStructureBuildWrite
{
    static constexpr AccelerationStructureUseSpecDesc accelerationStructureUse{
        .usage = AccelerationStructureUsageIntent::BuildOutput,
        .access = AccelerationStructureAccessIntent::BuildWrite,
    };
};

struct AccelerationStructureTraceRead
{
    static constexpr AccelerationStructureUseSpecDesc accelerationStructureUse{
        .usage = AccelerationStructureUsageIntent::TraceInput,
        .access = AccelerationStructureAccessIntent::TraceRead,
        .readOnly = true,
    };
};

struct AccelerationStructureCopyRead
{
    static constexpr AccelerationStructureUseSpecDesc accelerationStructureUse{
        .usage = AccelerationStructureUsageIntent::CopySource,
        .access = AccelerationStructureAccessIntent::CopyRead,
        .readOnly = true,
    };
};

struct AccelerationStructureCopyWrite
{
    static constexpr AccelerationStructureUseSpecDesc accelerationStructureUse{
        .usage = AccelerationStructureUsageIntent::CopyDestination,
        .access = AccelerationStructureAccessIntent::CopyWrite,
    };
};

struct ShaderBindingTableRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::ShaderBindingTable,
        .access = BufferAccessIntent::ShaderBindingTableRead,
        .readOnly = true,
    };
};

struct HostUploadRead
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::HostUpload,
        .access = BufferAccessIntent::TransferRead,
        .readOnly = true,
    };
};

struct ReadbackWrite
{
    static constexpr BufferUseSpecDesc bufferUse{
        .usage = BufferUsageIntent::Readback,
        .access = BufferAccessIntent::TransferWrite,
    };
};
} // namespace spec

template <ImageUseSpec TSpec>
[[nodiscard]] inline PassResourceUseDesc make(GraphResourceHandle resource) noexcept
{
    auto result = PassResourceUseDesc{
        .resource = resource,
        .imageUsage = TSpec::imageUse.usage,
        .imageAccess = TSpec::imageUse.access,
        .imageLayout = TSpec::imageUse.layout,
        .imageAspect = TSpec::imageUse.aspect,
    };

    if constexpr (TSpec::imageUse.ownershipDomain != ResourceOwnershipDomain::Undefined)
    {
        result.ownershipDomain = TSpec::imageUse.ownershipDomain;
    }

    if constexpr (TSpec::imageUse.readOnly)
    {
        result.readOnly = true;
    }

    return result;
}

template <ImageUseSpec TSpec>
[[nodiscard]] inline PassResourceUseDesc make(GraphResourceHandle resource, ImageUseOptions options) noexcept
{
    auto result = make<TSpec>(resource);
    if (options.aspect.has_value())
    {
        result.imageAspect = *options.aspect;
    }
    if (options.ownershipDomain.has_value())
    {
        result.ownershipDomain = *options.ownershipDomain;
    }
    return result;
}

template <BufferUseSpec TSpec>
[[nodiscard]] inline PassResourceUseDesc make(GraphResourceHandle resource) noexcept
{
    auto result = PassResourceUseDesc{
        .resource = resource,
        .bufferUsage = TSpec::bufferUse.usage,
        .bufferAccess = TSpec::bufferUse.access,
    };

    if constexpr (TSpec::bufferUse.ownershipDomain != ResourceOwnershipDomain::Undefined)
    {
        result.ownershipDomain = TSpec::bufferUse.ownershipDomain;
    }

    if constexpr (TSpec::bufferUse.readOnly)
    {
        result.readOnly = true;
    }

    return result;
}

template <BufferUseSpec TSpec>
[[nodiscard]] inline PassResourceUseDesc make(GraphResourceHandle resource, BufferUseOptions options) noexcept
{
    auto result = make<TSpec>(resource);
    if (options.ownershipDomain.has_value())
    {
        result.ownershipDomain = *options.ownershipDomain;
    }
    return result;
}

template <AccelerationStructureUseSpec TSpec>
[[nodiscard]] inline PassResourceUseDesc make(GraphResourceHandle resource) noexcept
{
    auto result = PassResourceUseDesc{
        .resource = resource,
        .accelerationStructureUsage = TSpec::accelerationStructureUse.usage,
        .accelerationStructureAccess = TSpec::accelerationStructureUse.access,
    };

    if constexpr (TSpec::accelerationStructureUse.ownershipDomain != ResourceOwnershipDomain::Undefined)
    {
        result.ownershipDomain = TSpec::accelerationStructureUse.ownershipDomain;
    }

    if constexpr (TSpec::accelerationStructureUse.readOnly)
    {
        result.readOnly = true;
    }

    return result;
}

template <AccelerationStructureUseSpec TSpec>
[[nodiscard]] inline PassResourceUseDesc make(GraphResourceHandle resource, BufferUseOptions options) noexcept
{
    auto result = make<TSpec>(resource);
    if (options.ownershipDomain.has_value())
    {
        result.ownershipDomain = *options.ownershipDomain;
    }
    return result;
}

[[nodiscard]] PassResourceUseDesc colorRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc colorWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc colorReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc depthRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc depthWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc depthReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc sampledRead(
    GraphResourceHandle resource,
    ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc storageRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc inputAttachmentRead(
    GraphResourceHandle resource,
    ImageAspectIntent aspect = ImageAspectIntent::Color) noexcept;

[[nodiscard]] PassResourceUseDesc uniformRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc bufferTransferSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc bufferTransferDst(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageBufferRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageBufferWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageBufferReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc vertexRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc indexRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc indirectRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc uniformTexelRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageTexelRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageTexelWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc storageTexelReadWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildInputRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureStorageWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureScratchWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureBuildWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureTraceRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureCopyWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc accelerationStructureWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc shaderBindingTableRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc hostUploadRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc readbackWrite(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc imageTransferSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc imageTransferDst(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc transferSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc transferDst(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc copySource(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc copyDestination(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc resolveSrc(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc resolveDst(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc presentRead(GraphResourceHandle resource) noexcept;

[[nodiscard]] PassResourceUseDesc presentRead(
    GraphResourceHandle resource,
    ResourceOwnershipDomain ownershipDomain) noexcept;
} // namespace use

struct PassBufferResource
{
    vk::Buffer buffer = vk::Buffer{};
    vk::DeviceSize size = 0;
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> resource{};
};

struct PassImageResource
{
    vk::Image image = vk::Image{};
    vk::ImageView view = vk::ImageView{};
    vk::Extent3D extent{1, 1, 1};
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,
        0,
        1,
        0,
        1,
    };
    std::optional<std::reference_wrapper<const nr::rhi::Image>> resource{};
};

struct PassAccelerationStructureResource
{
    vk::AccelerationStructureKHR accelerationStructure{};
    vk::AccelerationStructureTypeKHR type = vk::AccelerationStructureTypeKHR::eTopLevel;
    vk::DeviceSize size = 0;
    vk::Buffer storageBuffer{};
    vk::DeviceSize storageOffset = 0;
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>> resource{};
};

struct PassPrepareContext
{
    std::uint32_t frameIndex = 0;
    std::optional<std::reference_wrapper<nr::rhi::Device>> device{};

    std::function<std::optional<PassBufferResource>(GraphResourceHandle)> resolveBuffer{};
    std::function<std::optional<PassImageResource>(GraphResourceHandle)> resolveImage{};
    std::function<std::optional<PassAccelerationStructureResource>(GraphResourceHandle)> resolveAccelerationStructure{};
    std::function<std::optional<std::reference_wrapper<const std::any>>(GraphFrameDataHandle)> resolveFrameDataPayload{};

    template <typename TPayload>
    [[nodiscard]] std::optional<std::reference_wrapper<const std::remove_cvref_t<TPayload>>> resolveFrameData(
        GraphFrameDataHandle handle) const
    {
        using Payload = std::remove_cvref_t<TPayload>;

        nrAssert(handle.valid(), "PassPrepareContext::resolveFrameData requires a valid frame data handle.");
        nrAssert(
            static_cast<bool>(resolveFrameDataPayload),
            "PassPrepareContext::resolveFrameData requires a frame data resolver callback.");

        auto payload = resolveFrameDataPayload(handle);
        if (!payload.has_value())
        {
            return {};
        }

        auto const typedPayload = std::any_cast<Payload>(&payload->get());
        nrAssert(
            typedPayload != nullptr,
            std::format(
                "PassPrepareContext::resolveFrameData resolved unexpected payload type for frame data handle {}.",
                handle.value));
        return std::cref(*typedPayload);
    }

    template <typename TPayload>
    [[nodiscard]] const std::remove_cvref_t<TPayload>& frameData(GraphFrameDataHandle handle) const
    {
        auto resolved = resolveFrameData<TPayload>(handle);
        nrAssert(
            resolved.has_value(),
            std::format("PassPrepareContext::frameData failed to resolve frame data handle {}.", handle.value));
        return resolved->get();
    }
};

using PassPrepareCallback = std::function<void(const PassPrepareContext&)>;

struct PassRecordContext
{
    std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer{};
    std::uint32_t frameIndex = 0;
    std::optional<std::reference_wrapper<nr::rhi::Device>> device{};

    std::function<std::optional<PassBufferResource>(GraphResourceHandle)> resolveBuffer{};
    std::function<std::optional<PassImageResource>(GraphResourceHandle)> resolveImage{};
    std::function<std::optional<PassAccelerationStructureResource>(GraphResourceHandle)> resolveAccelerationStructure{};
    std::function<std::optional<std::reference_wrapper<const std::any>>(GraphFrameDataHandle)> resolveFrameDataPayload{};

    template <typename TPayload>
    [[nodiscard]] std::optional<std::reference_wrapper<const std::remove_cvref_t<TPayload>>> resolveFrameData(
        GraphFrameDataHandle handle) const
    {
        using Payload = std::remove_cvref_t<TPayload>;

        nrAssert(handle.valid(), "PassRecordContext::resolveFrameData requires a valid frame data handle.");
        nrAssert(
            static_cast<bool>(resolveFrameDataPayload),
            "PassRecordContext::resolveFrameData requires a frame data resolver callback.");

        auto payload = resolveFrameDataPayload(handle);
        if (!payload.has_value())
        {
            return {};
        }

        auto const typedPayload = std::any_cast<Payload>(&payload->get());
        nrAssert(
            typedPayload != nullptr,
            std::format(
                "PassRecordContext::resolveFrameData resolved unexpected payload type for frame data handle {}.",
                handle.value));
        return std::cref(*typedPayload);
    }

    template <typename TPayload>
    [[nodiscard]] const std::remove_cvref_t<TPayload>& frameData(GraphFrameDataHandle handle) const
    {
        auto resolved = resolveFrameData<TPayload>(handle);
        nrAssert(
            resolved.has_value(),
            std::format("PassRecordContext::frameData failed to resolve frame data handle {}.", handle.value));
        return resolved->get();
    }
};

using PassRecordCallback = std::function<void(const PassRecordContext&)>;

struct GraphNodeDesc
{
    GraphNodeHandle handle{};
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
};

struct PassExecutionDesc
{
    GraphPassHandle handle{};
    GraphNodeHandle node{};
    std::string debugName{};
    bool isCopyPass = false;
    QueueDomain queue = QueueDomain::Graphics;
    std::vector<PassResourceUseDesc> resourceUses{};
    PassPrepareCallback prepare{};
    PassRecordCallback record{};
};

struct SubmitBoundaryDesc
{
    GraphSubmitHandle handle{};
    std::string debugName{};
};

using GraphExecutionStep = std::variant<GraphPassHandle, GraphSubmitHandle>;

struct RenderGraphFrameDescription
{
    std::vector<GraphResourceDesc> resources{};
    std::vector<GraphFrameDataDesc> frameData{};
    std::vector<GraphNodeDesc> nodes{};
    std::vector<PassExecutionDesc> passes{};
    std::vector<SubmitBoundaryDesc> submitBoundaries{};
    std::vector<GraphExecutionStep> executionOrder{};
};

/**
 * @brief Precise sync2 stage+access scope for one side of a barrier.
 *
 * An empty `stages` mask marks the scope as unresolved; barrier emission then
 * falls back to a conservative all-commands scope for that side only.
 */
struct AccessScope
{
    vk::PipelineStageFlags2 stages = vk::PipelineStageFlags2{};
    vk::AccessFlags2 access = vk::AccessFlags2{};

    [[nodiscard]] bool resolved() const noexcept;
};

struct ResourceStateTransition
{
    GraphResourceHandle resource{};
    QueueDomain srcQueue = QueueDomain::Graphics;
    QueueDomain dstQueue = QueueDomain::Graphics;
    ImageLayoutIntent oldLayout = ImageLayoutIntent::Undefined;
    ImageLayoutIntent newLayout = ImageLayoutIntent::Undefined;
    DependencyStrength strength = DependencyStrength::InOrder;

    /// Producer-side scope (last access before the transition). Unresolved on first use.
    AccessScope srcScope{};
    /// Consumer-side scope (first access after the transition).
    AccessScope dstScope{};
};

struct CompiledResourceDesc
{
    GraphResourceHandle handle{};
    std::string debugName{};

    bool isBuffer = false;
    bool isImage = false;
    bool isAccelerationStructure = false;
    bool isSwapchain = false;

    ResourceLifetime lifetime = ResourceLifetime::GraphTransient;
    ResourceResidency residency = ResourceResidency::Managed;

    vk::DeviceSize resolvedBufferSize = 0;
    vk::DeviceSize resolvedAccelerationStructureSize = 0;
    vk::Extent3D resolvedExtent{1, 1, 1};
    vk::Format resolvedFormat = vk::Format::eUndefined;
    vk::AccelerationStructureTypeKHR resolvedAccelerationStructureType = vk::AccelerationStructureTypeKHR::eTopLevel;
    ImageAspectIntent resolvedAspect = ImageAspectIntent::Color;

    vk::BufferUsageFlags resolvedBufferUsage{};
    vk::ImageUsageFlags resolvedImageUsage{};

    ImageLayoutIntent initialLayout = ImageLayoutIntent::Undefined;
    ImageLayoutIntent finalLayout = ImageLayoutIntent::Undefined;

    ResourceOwnershipDomain initialOwnership = ResourceOwnershipDomain::Undefined;
    ResourceOwnershipDomain finalOwnership = ResourceOwnershipDomain::Undefined;
    nr::rhi::MemoryUsage resolvedBufferMemoryUsage = nr::rhi::MemoryUsage::GpuOnly;

    /// Optional reference to a pre-allocated imported buffer held by the node.
    /// Populated from GraphImportedBufferDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> importedBufferResource{};

    /// Optional reference to a pre-allocated imported image held by the node.
    /// Populated from GraphImportedImageDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<const nr::rhi::Image>> importedImageResource{};

    /// Optional reference to a pre-built acceleration structure held by the node or renderer cache.
    /// Populated from GraphImportedAccelerationStructureDesc::importedResource during compilation.
    std::optional<std::reference_wrapper<const nr::rhi::AccelerationStructureResource>> importedAccelerationStructureResource{};
};

struct CompiledPass
{
    GraphPassHandle handle{};
    GraphNodeHandle node{};
    std::string debugName{};

    bool isCopyPass = false;
    QueueDomain queue = QueueDomain::Graphics;
    std::uint32_t submitBatchIndex = 0;

    std::vector<PassResourceUseDesc> resourceUses{};
    std::vector<std::size_t> resolvedResourceIndices{};
    std::vector<ResourceStateTransition> preBarriers{};
    PassPrepareCallback prepare{};
    PassRecordCallback record{};
};

struct CompiledSubmitBatch
{
    std::uint32_t batchIndex = 0;
    QueueDomain queue = QueueDomain::Graphics;
    std::optional<GraphSubmitHandle> openedBySubmitNode{};
    std::string openedBySubmitNodeDebugName{};
    std::vector<CompiledPass> passes{};
};

struct CompiledGraphFrame
{
    std::vector<CompiledResourceDesc> resources{};
    std::vector<GraphFrameDataDesc> frameData{};
    std::vector<CompiledSubmitBatch> submitBatches{};
    std::vector<ResourceStateTransition> ownershipTransitions{};
    std::string debugView{};
};

struct GpuPassTimingSample
{
    GraphPassHandle pass{};
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
    bool isCopyPass = false;
    double milliseconds = 0.0;
};

struct GpuPassTimingFrame
{
    std::uint32_t frameIndex = 0;
    std::vector<GpuPassTimingSample> passes{};
};

struct RendererGpuPassAverage
{
    GraphPassHandle pass{};
    std::string debugName{};
    QueueDomain queue = QueueDomain::Graphics;
    bool isCopyPass = false;
    double milliseconds = 0.0;
    std::uint32_t sampleCount = 0;
};

struct RendererGpuPassStatistics
{
    std::vector<RendererGpuPassAverage> averages{};
    std::uint32_t pendingSampleFrameCount = 0;
    std::uint32_t averagedFrameCount = 0;
    bool valid = false;
};
} // namespace nr::renderer
