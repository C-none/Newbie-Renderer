module nr.rhi;

import :dlss;
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

const DlssStatus &DlssContext::status() const noexcept
{
    return context_.status();
}

DlssOptimalSettings DlssContext::optimalSettings(DlssDimensions targetSize, DlssQuality quality)
{
    return context_.optimalSettings(targetSize, quality);
}

DlssRayReconstructionFeature::DlssRayReconstructionFeature(std::shared_ptr<DlssContext> context,
                                                           const vk::raii::CommandBuffer &commandBuffer,
                                                           const DlssRayReconstructionCreateDesc &desc)
    : context_(std::move(context))
{
    nrAssert(static_cast<bool>(context_), "DLSS RR feature creation requires a shared device context.");
    feature_ = context_->context_.createRayReconstruction(static_cast<vk::CommandBuffer>(*commandBuffer), desc);
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

const DlssStatus &DlssRayReconstructionFeature::status() const noexcept
{
    nrAssert(static_cast<bool>(feature_), "DLSS RR status requires a feature object.");
    return feature_->status();
}

DlssStatus DlssRayReconstructionFeature::evaluate(const vk::raii::CommandBuffer &commandBuffer,
                                                  const DlssRayReconstructionEvalDesc &desc)
{
    nrAssert(valid(), "DLSS RR evaluation requires a valid feature.");
    return feature_->evaluate(static_cast<vk::CommandBuffer>(*commandBuffer), desc);
}

bool dlssSdkCompiled() noexcept
{
    return nr::dependency::dlss::sdkCompiled();
}

std::string_view dlssQualityName(DlssQuality quality) noexcept
{
    return nr::dependency::dlss::qualityName(quality);
}

std::string_view dlssPresetName(DlssRayReconstructionPreset preset) noexcept
{
    return nr::dependency::dlss::presetName(preset);
}

std::string_view dlssResourceSlotName(DlssRayReconstructionResourceSlot slot) noexcept
{
    return nr::dependency::dlss::resourceSlotName(slot);
}

std::string_view dlssSubrectSlotName(DlssRayReconstructionSubrectSlot slot) noexcept
{
    return nr::dependency::dlss::subrectSlotName(slot);
}
} // namespace nr::rhi
