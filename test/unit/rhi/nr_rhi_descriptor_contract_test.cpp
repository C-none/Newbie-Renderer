import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
const nr::test::CaseRegistrar descriptorSemanticCase{
    "rhi descriptor semantic maps Vulkan descriptor families",
    [] {
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eSampler) == nr::rhi::ShaderDescriptorSemantic::Sampler);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eCombinedImageSampler) == nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eSampledImage) == nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eStorageImage) == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eUniformBuffer) == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eStorageBuffer) == nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eInputAttachment) == nr::rhi::ShaderDescriptorSemantic::InputAttachment);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eInlineUniformBlock) == nr::rhi::ShaderDescriptorSemantic::InlineUniformBlock);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eAccelerationStructureKHR) == nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eMutableEXT) == nr::rhi::ShaderDescriptorSemantic::Unsupported);

        nr::test::requireEqual(
            std::string{nr::rhi::shaderDescriptorSemanticName(nr::rhi::ShaderDescriptorSemantic::AccelerationStructure)},
            std::string{"AccelerationStructure"});
    }};

const nr::test::CaseRegistrar runtimeDescriptorArraySetCase{
    "rhi runtime descriptor arrays follow semantic multi-set convention",
    [] {
        auto convention = nr::rhi::RuntimeDescriptorArraySetConvention{
            .samplerSet = 10,
            .sampledImageSet = 11,
            .storageImageSet = 12,
            .bufferSet = 13,
            .accelerationStructureSet = 14,
            .inputAttachmentSet = 15,
            .inlineUniformBlockSet = 16,
        };

        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::Sampler, convention), 10u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::SampledImage, convention), 11u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::StorageImage, convention), 12u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::UniformBuffer, convention), 13u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::StorageBuffer, convention), 13u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::AccelerationStructure, convention), 14u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::InputAttachment, convention), 15u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::InlineUniformBlock, convention), 16u);
        nr::test::require(!nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::Unsupported, convention).has_value());
    }};

const nr::test::CaseRegistrar descriptorBindingInfoCase{
    "rhi descriptor binding info reports binding feature flags",
    [] {
        auto binding = nr::rhi::DescriptorBindingInfo{};
        binding.descriptorType = vk::DescriptorType::eSampledImage;
        binding.bindingFlags = vk::DescriptorBindingFlagBits::eVariableDescriptorCount |
                               vk::DescriptorBindingFlagBits::ePartiallyBound |
                               vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        nr::test::require(binding.supportsVariableDescriptorCount(), "binding should support variable descriptor count");
        nr::test::require(binding.isPartiallyBound(), "binding should support partially bound descriptors");
        nr::test::require(binding.isUpdateAfterBind(), "binding should support update-after-bind");
        nr::test::require(binding.supportsImmutableSampler() == false, "sampled image binding should not support immutable sampler");
        nr::test::require(binding.semantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);

        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        nr::test::require(binding.supportsImmutableSampler(), "combined image sampler should support immutable sampler");

        binding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        nr::test::require(binding.usesDynamicDescriptorOffset(), "dynamic uniform buffer should require dynamic offset");
    }};
} // namespace
