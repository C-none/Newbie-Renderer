export module nr.rhi:rayTracing;
import dependency.vulkan;
import :type;
import :pipeline;
import :resourcePool;
import nr.utils;
import std;

export namespace nr::rhi
{
struct ShaderBindingTableRecordDesc
{
    std::uint32_t groupIndex = 0;
    std::span<const std::uint8_t> data{};
};

struct ShaderBindingTableSectionDesc
{
    std::uint32_t firstGroup = 0;
    std::uint32_t groupCount = 0;
    std::uint32_t stride = 0;
    std::span<const ShaderBindingTableRecordDesc> records{};
};

struct ShaderBindingTableBuildDesc
{
    const RayTracingPipeline &pipeline;
    ShaderBindingTableSectionDesc raygen{.firstGroup = 0, .groupCount = 1, .stride = 0};
    ShaderBindingTableSectionDesc miss{};
    ShaderBindingTableSectionDesc hit{};
    ShaderBindingTableSectionDesc callable{};
    std::string debugName = "rt_sbt";
};

struct ShaderBindingTableLayoutDesc
{
    RayTracingCapabilitySnapshot capabilities{};
    std::uint32_t pipelineGroupCount = 0;
    ShaderBindingTableSectionDesc raygen{.firstGroup = 0, .groupCount = 1, .stride = 0};
    ShaderBindingTableSectionDesc miss{};
    ShaderBindingTableSectionDesc hit{};
    ShaderBindingTableSectionDesc callable{};
};

struct ShaderBindingTableBuildPlanSection
{
    std::uint32_t firstGroup = 0;
    std::uint32_t recordCount = 0;
    std::uint32_t stride = 0;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = 0;
};

struct ShaderBindingTableBuildPlan
{
    vk::DeviceSize totalSize = 0;
    std::uint32_t handleSize = 0;
    std::uint32_t handleAlignment = 1;
    std::uint32_t baseAlignment = 1;
    ShaderBindingTableBuildPlanSection raygen{};
    ShaderBindingTableBuildPlanSection miss{};
    ShaderBindingTableBuildPlanSection hit{};
    ShaderBindingTableBuildPlanSection callable{};
};

struct TraceRaysDimensions
{
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
};

class ShaderBindingTable;

struct TraceRaysDesc
{
    const RayTracingPipeline &pipeline;
    const ShaderBindingTable &shaderBindingTable;
    TraceRaysDimensions dimensions{};
    QueueRole recordingQueueRole = QueueRole::Compute;
};

} // namespace nr::rhi

namespace nr::rhi::rt_detail
{
using nr::rhi::detail::ValidationResult;

[[nodiscard]] std::uint32_t recordCount(const ShaderBindingTableSectionDesc &section);

[[nodiscard]] std::size_t maxRecordDataSize(const ShaderBindingTableSectionDesc &section);

[[nodiscard]] std::uint32_t effectiveStride(const ShaderBindingTableSectionDesc &section,
                                            const RayTracingCapabilitySnapshot &capabilities);

[[nodiscard]] ValidationResult validateSection(std::string_view label, const ShaderBindingTableSectionDesc &section,
                                               std::uint32_t effectiveSectionStride,
                                               const RayTracingCapabilitySnapshot &capabilities,
                                               std::uint32_t pipelineGroupCount);

[[nodiscard]] vk::DeviceSize sectionSize(const ShaderBindingTableSectionDesc &section);

[[nodiscard]] std::array<ShaderBindingTableBuildPlanSection, 4> buildSectionPlan(
    const ShaderBindingTableLayoutDesc &desc);

[[nodiscard]] vk::StridedDeviceAddressRegionKHR buildRegion(vk::DeviceAddress baseAddress,
                                                            const ShaderBindingTableBuildPlanSection &section);
[[nodiscard]] ValidationResult validateShaderBindingTableLayoutDesc(const ShaderBindingTableLayoutDesc &desc);

[[nodiscard]] ValidationResult validateShaderBindingTableBuildDesc(const ShaderBindingTableBuildDesc &desc);

[[nodiscard]] ValidationResult validateTraceRaysDispatch(const TraceRaysDimensions &dimensions,
                                                         const RayTracingCapabilitySnapshot &capabilities);

} // namespace nr::rhi::rt_detail

export namespace nr::rhi
{

[[nodiscard]] ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableLayoutDesc &desc);

[[nodiscard]] ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableBuildDesc &desc);

class ShaderBindingTable
{
  public:
    struct Regions
    {
        vk::StridedDeviceAddressRegionKHR raygen{};
        vk::StridedDeviceAddressRegionKHR miss{};
        vk::StridedDeviceAddressRegionKHR hit{};
        vk::StridedDeviceAddressRegionKHR callable{};
    };

    ShaderBindingTable() = default;
    ShaderBindingTable(const ShaderBindingTable &) = delete;
    ShaderBindingTable &operator=(const ShaderBindingTable &) = delete;
    ShaderBindingTable(ShaderBindingTable &&) noexcept = default;
    ShaderBindingTable &operator=(ShaderBindingTable &&) noexcept = default;

    [[nodiscard]] static ShaderBindingTable create(const ResourceFactory &resourceFactory,
                                                   const ShaderBindingTableBuildDesc &desc);

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] RayTracingPipelineIdentity pipelineIdentity() const noexcept;

    [[nodiscard]] const Buffer &buffer() const noexcept;

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &raygenRegion() const noexcept;

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &missRegion() const noexcept;

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &hitRegion() const noexcept;

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &callableRegion() const noexcept;

    [[nodiscard]] Regions regions() const noexcept;

  private:
    RayTracingPipelineIdentity pipelineIdentity_{};
    Buffer buffer_{};
    vk::StridedDeviceAddressRegionKHR raygenRegion_{};
    vk::StridedDeviceAddressRegionKHR missRegion_{};
    vk::StridedDeviceAddressRegionKHR hitRegion_{};
    vk::StridedDeviceAddressRegionKHR callableRegion_{};
};

void traceRays(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysDesc &desc);

} // namespace nr::rhi
