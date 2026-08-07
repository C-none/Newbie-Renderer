export module nr.rhi:pipelineBinary;
import dependency.vulkan;
import std;

export namespace nr::rhi
{
struct PipelineBinaryCacheKey
{
    vk::PipelineBinaryKeyKHR driverKey{};
    std::uint64_t contentFingerprint = 0u;
};

class PipelineBinaryStore
{
  public:
    struct LoadedBinaries
    {
        std::vector<vk::raii::PipelineBinaryKHR> owned{};
        std::vector<vk::PipelineBinaryKHR> handles{};
    };

    PipelineBinaryStore(const vk::raii::Device &device, std::filesystem::path root);

    [[nodiscard]] PipelineBinaryCacheKey pipelineKey(const vk::PipelineCreateInfoKHR &createInfo,
                                                     std::uint64_t contentFingerprint) const;

    [[nodiscard]] std::optional<LoadedBinaries> load(const PipelineBinaryCacheKey &cacheKey);

    void capture(const PipelineBinaryCacheKey &cacheKey, vk::Pipeline pipeline);

    void markLoadAccepted() noexcept;

    [[nodiscard]] std::uint64_t acceptedLoadCount() const noexcept;

    [[nodiscard]] std::uint64_t persistedCaptureCount() const noexcept;

    void invalidate(const PipelineBinaryCacheKey &cacheKey) noexcept;

  private:
    struct BinaryBlob
    {
        vk::PipelineBinaryKeyKHR key{};
        std::vector<std::uint8_t> data{};
    };

    struct Artifact
    {
        std::vector<BinaryBlob> binaries{};
    };

    [[nodiscard]] static bool validKey(const vk::PipelineBinaryKeyKHR &key) noexcept;
    [[nodiscard]] static bool equalKeys(const vk::PipelineBinaryKeyKHR &lhs,
                                        const vk::PipelineBinaryKeyKHR &rhs) noexcept;
    [[nodiscard]] static std::string keyHex(const vk::PipelineBinaryKeyKHR &key);

    [[nodiscard]] std::filesystem::path artifactPath(const PipelineBinaryCacheKey &cacheKey) const;
    [[nodiscard]] std::optional<Artifact> readArtifact(const PipelineBinaryCacheKey &cacheKey) const;
    [[nodiscard]] bool writeArtifact(const PipelineBinaryCacheKey &cacheKey, const Artifact &artifact) const;
    void releaseCapturedData(vk::Pipeline pipeline) const noexcept;

    std::reference_wrapper<const vk::raii::Device> device_;
    std::filesystem::path root_{};
    vk::PipelineBinaryKeyKHR globalKey_{};
    std::atomic_uint64_t acceptedLoadCount_ = 0u;
    std::atomic_uint64_t persistedCaptureCount_ = 0u;
    mutable std::mutex fileMutex_{};
};
} // namespace nr::rhi
