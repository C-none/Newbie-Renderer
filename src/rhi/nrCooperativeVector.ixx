export module nr.rhi:cooperativeVector;
import dependency.vulkan;
import std;

export namespace nr::rhi
{
// VK_NV_cooperative_vector VUID-vkCmdConvertCooperativeVectorMatrixNV-pInfo-10084/-10085
// fixes both conversion device addresses at 64-byte alignment. This is an extension
// command requirement, not a physical-device property that can be queried.
inline constexpr vk::DeviceSize kCooperativeVectorMatrixDeviceAddressAlignment = 64u;

enum class CooperativeVectorMatrixLayout : std::uint8_t
{
    RowMajor,
    TrainingOptimal,
    InferencingOptimal,
};

enum class CooperativeVectorComponentType : std::uint8_t
{
    Float16,
    FloatE4M3,
};

struct CooperativeVectorMatrixDesc
{
    std::uint32_t rows = 0;
    std::uint32_t columns = 0;
    CooperativeVectorMatrixLayout layout = CooperativeVectorMatrixLayout::RowMajor;
    vk::DeviceSize rowStrideBytes = 0;
    CooperativeVectorComponentType componentType = CooperativeVectorComponentType::Float16;
};

struct CooperativeVectorMatrixMemory
{
    vk::DeviceAddress deviceAddress = 0;
    vk::DeviceSize size = 0;
};

struct CooperativeVectorMatrixLayoutSize
{
    vk::DeviceSize byteSize = 0;
};

struct CooperativeVectorCapabilitySnapshot
{
    bool extensionEnabled = false;
    bool cooperativeVectorFeatureEnabled = false;
    bool cooperativeVectorTrainingFeatureEnabled = false;
    bool shaderFloat16FeatureEnabled = false;
    bool vulkanMemoryModelFeatureEnabled = false;
    bool shaderReplicatedCompositesFeatureEnabled = false;
    bool storageBuffer16BitAccessFeatureEnabled = false;
    bool uniformAndStorageBuffer16BitAccessFeatureEnabled = false;
    bool computeStage = false;
    bool raygenStage = false;
    bool closestHitStage = false;
    bool trainingFloat16Accumulation = false;
    bool fullFloat16Tuple = false;
    bool fullFloat16TupleWithTranspose = false;
    std::uint32_t maxComponents = 0;
    // Every tuple reported by vkGetPhysicalDeviceCooperativeVectorPropertiesNV.
    // The booleans above are cached predicates over this list; consumers that
    // need a non-admitted tuple, such as FP8 inference, query it here.
    std::vector<vk::CooperativeVectorPropertiesNV> supportedTuples{};
};

// Row-major layout sizes are computed; TrainingOptimal/InferencingOptimal sizes
// are queried through vkConvertCooperativeVectorMatrixNV with a host source.
[[nodiscard]] CooperativeVectorMatrixLayoutSize queryCooperativeVectorMatrixLayoutSize(
    const vk::raii::Device &device, CooperativeVectorMatrixDesc desc);

// The source and destination regions must describe exactly one matrix using the
// cached layout sizes from queryCooperativeVectorMatrixLayoutSize.
void recordCooperativeVectorMatrixConversion(const vk::raii::CommandBuffer &commandBuffer,
                                             CooperativeVectorMatrixMemory source,
                                             CooperativeVectorMatrixDesc sourceDesc,
                                             CooperativeVectorMatrixLayoutSize sourceLayoutSize,
                                             CooperativeVectorMatrixMemory destination,
                                             CooperativeVectorMatrixDesc destinationDesc,
                                             CooperativeVectorMatrixLayoutSize destinationLayoutSize);
} // namespace nr::rhi
