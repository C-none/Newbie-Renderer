module nr.rhi;

import :dlss;
import :command;
import nr.utils;
import std;

namespace nr::rhi
{
DlssContext::DlssContext(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device,
                         std::filesystem::path applicationDataPath)
    : context_(nr::dependency::dlss::VulkanContextDesc{
          .instance = instance,
          .physicalDevice = physicalDevice,
          .device = device,
          .applicationDataPath = std::move(applicationDataPath),
      })
{
}

bool DlssContext::valid() const noexcept
{
    return context_.valid();
}

const nr::dependency::dlss::Status &DlssContext::status() const noexcept
{
    return context_.status();
}

nr::dependency::dlss::OptimalSettings DlssContext::optimalSettings(nr::dependency::dlss::Dimensions targetSize,
                                                                   nr::dependency::dlss::Quality quality)
{
    return context_.optimalSettings(targetSize, quality);
}

DlssRayReconstructionFeature::DlssRayReconstructionFeature(
    std::shared_ptr<DlssContext> context, const vk::raii::CommandBuffer &commandBuffer,
    const nr::dependency::dlss::RayReconstructionCreateDesc &desc)
    : context_(std::move(context))
{
    nrAssert(static_cast<bool>(context_), "DLSS RR feature creation requires a shared device context.");
    feature_ = context_->context_.createRayReconstruction(static_cast<vk::CommandBuffer>(*commandBuffer), desc);
}

std::unique_ptr<DlssRayReconstructionFeature> DlssRayReconstructionFeature::create(
    std::shared_ptr<DlssContext> context, const vk::raii::Device &device, GpuQueue &queue,
    const nr::dependency::dlss::RayReconstructionCreateDesc &desc)
{
    std::unique_ptr<DlssRayReconstructionFeature> feature{};
    submitOneShot(device, queue, {}, [&](const vk::raii::CommandBuffer &commandBuffer) {
        feature = std::make_unique<DlssRayReconstructionFeature>(std::move(context), commandBuffer, desc);
    });
    nrAssert(feature->valid(), "DLSS RR feature creation failed: {}", feature->status().message);
    return feature;
}

DlssRayReconstructionFeature::~DlssRayReconstructionFeature() = default;
DlssRayReconstructionFeature::DlssRayReconstructionFeature(DlssRayReconstructionFeature &&) noexcept = default;
DlssRayReconstructionFeature &DlssRayReconstructionFeature::operator=(DlssRayReconstructionFeature &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    feature_.reset();
    context_.reset();
    context_ = std::move(other.context_);
    feature_ = std::move(other.feature_);
    return *this;
}

bool DlssRayReconstructionFeature::valid() const noexcept
{
    return feature_ && feature_->valid();
}

const nr::dependency::dlss::Status &DlssRayReconstructionFeature::status() const noexcept
{
    nrAssert(static_cast<bool>(feature_), "DLSS RR status requires a feature object.");
    return feature_->status();
}

nr::dependency::dlss::Status DlssRayReconstructionFeature::evaluate(
    const vk::raii::CommandBuffer &commandBuffer, const nr::dependency::dlss::RayReconstructionEvalDesc &desc)
{
    nrAssert(valid(), "DLSS RR evaluation requires a valid feature.");
    return feature_->evaluate(static_cast<vk::CommandBuffer>(*commandBuffer), desc);
}
} // namespace nr::rhi
