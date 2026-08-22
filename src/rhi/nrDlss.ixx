export module nr.rhi:dlss;

import dependency.dlss;
import dependency.vulkan;
import :queue;
import std;

export namespace nr::rhi
{
class DlssContext final
{
  public:
    DlssContext(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device,
                std::filesystem::path applicationDataPath);

    DlssContext(const DlssContext &) = delete;
    DlssContext &operator=(const DlssContext &) = delete;
    DlssContext(DlssContext &&) = delete;
    DlssContext &operator=(DlssContext &&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const nr::dependency::dlss::Status &status() const noexcept;
    [[nodiscard]] nr::dependency::dlss::OptimalSettings optimalSettings(nr::dependency::dlss::Dimensions targetSize,
                                                                        nr::dependency::dlss::Quality quality);

  private:
    friend class DlssRayReconstructionFeature;
    nr::dependency::dlss::Context context_;
};

class DlssRayReconstructionFeature final
{
  public:
    DlssRayReconstructionFeature(std::shared_ptr<DlssContext> context, const vk::raii::CommandBuffer &commandBuffer,
                                 const nr::dependency::dlss::RayReconstructionCreateDesc &desc);
    ~DlssRayReconstructionFeature();

    DlssRayReconstructionFeature(const DlssRayReconstructionFeature &) = delete;
    DlssRayReconstructionFeature &operator=(const DlssRayReconstructionFeature &) = delete;
    DlssRayReconstructionFeature(DlssRayReconstructionFeature &&) noexcept;
    DlssRayReconstructionFeature &operator=(DlssRayReconstructionFeature &&) noexcept;

    // Feature creation records an NGX preprocessing command buffer, so the
    // queue must accept that work and is drained before this returns.
    [[nodiscard]] static std::unique_ptr<DlssRayReconstructionFeature> create(
        std::shared_ptr<DlssContext> context, const vk::raii::Device &device, GpuQueue &queue,
        const nr::dependency::dlss::RayReconstructionCreateDesc &desc);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] const nr::dependency::dlss::Status &status() const noexcept;
    [[nodiscard]] nr::dependency::dlss::Status evaluate(const vk::raii::CommandBuffer &commandBuffer,
                                                        const nr::dependency::dlss::RayReconstructionEvalDesc &desc);

  private:
    // Declaration order guarantees the feature and its parameter map are
    // released before the shared NGX context.
    std::shared_ptr<DlssContext> context_{};
    std::unique_ptr<nr::dependency::dlss::RayReconstructionFeature> feature_{};
};
} // namespace nr::rhi
