import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;

namespace
{
template <typename VkHandle> [[nodiscard]] VkHandle fakeVkHandle(std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<VkHandle>)
    {
        return reinterpret_cast<VkHandle>(value);
    }
    else
    {
        return static_cast<VkHandle>(value);
    }
}

[[nodiscard]] nr::rhi::DescriptorBindingInfo makeDescriptorBinding(std::uint32_t set, std::uint32_t binding,
                                                                   vk::DescriptorType descriptorType)
{
    return nr::rhi::DescriptorBindingInfo{
        .set = set,
        .binding = binding,
        .descriptorType = descriptorType,
        .debugPath = {},
    };
}

[[nodiscard]] nr::rhi::DescriptorWriteRequest makeBufferWrite(std::uint32_t set, std::uint32_t binding,
                                                              std::uint32_t arrayElement, vk::DeviceSize offset,
                                                              vk::DeviceSize range)
{
    return nr::rhi::DescriptorWriteRequest{
        .binding = makeDescriptorBinding(set, binding, vk::DescriptorType::eUniformBuffer),
        .arrayElement = arrayElement,
        .payload =
            nr::rhi::BufferDescriptorWrite{
                .buffer = vk::Buffer{fakeVkHandle<VkBuffer>(0x1001u)},
                .offset = offset,
                .range = range,
            },
    };
}

const nr::test::CaseRegistrar descriptorSemanticCase{
    "rhi descriptor semantic maps Vulkan descriptor families", [] {
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eSampler) ==
                          nr::rhi::ShaderDescriptorSemantic::Sampler);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eCombinedImageSampler) ==
                          nr::rhi::ShaderDescriptorSemantic::CombinedImageSampler);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eSampledImage) ==
                          nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eStorageImage) ==
                          nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eUniformBuffer) ==
                          nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eStorageBuffer) ==
                          nr::rhi::ShaderDescriptorSemantic::StorageBuffer);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eInputAttachment) ==
                          nr::rhi::ShaderDescriptorSemantic::InputAttachment);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eInlineUniformBlock) ==
                          nr::rhi::ShaderDescriptorSemantic::InlineUniformBlock);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eAccelerationStructureKHR) ==
                          nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(nr::rhi::descriptorSemantic(vk::DescriptorType::eMutableEXT) ==
                          nr::rhi::ShaderDescriptorSemantic::Unsupported);

        nr::test::requireEqual(std::string{nr::rhi::shaderDescriptorSemanticName(
                                   nr::rhi::ShaderDescriptorSemantic::AccelerationStructure)},
                               std::string{"AccelerationStructure"});
    }};

const nr::test::CaseRegistrar runtimeDescriptorArraySetCase{
    "rhi runtime descriptor arrays follow semantic multi-set convention", [] {
        auto convention = nr::rhi::RuntimeDescriptorArraySetConvention{
            .samplerSet = 10,
            .sampledImageSet = 11,
            .storageImageSet = 12,
            .bufferSet = 13,
            .accelerationStructureSet = 14,
        };

        nr::test::requireEqual(
            *nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::Sampler, convention), 10u);
        nr::test::requireEqual(
            *nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::SampledImage, convention), 11u);
        nr::test::requireEqual(
            *nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::StorageImage, convention), 12u);
        nr::test::requireEqual(
            *nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::UniformBuffer, convention), 13u);
        nr::test::requireEqual(
            *nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::StorageBuffer, convention), 13u);
        nr::test::requireEqual(*nr::rhi::runtimeDescriptorArraySetFor(
                                   nr::rhi::ShaderDescriptorSemantic::AccelerationStructure, convention),
                               14u);
        nr::test::require(
            !nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::InputAttachment, convention)
                 .has_value());
        nr::test::require(
            !nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::InlineUniformBlock, convention)
                 .has_value());
        nr::test::require(
            !nr::rhi::runtimeDescriptorArraySetFor(nr::rhi::ShaderDescriptorSemantic::Unsupported, convention)
                 .has_value());
    }};

const nr::test::CaseRegistrar descriptorBindingInfoCase{
    "rhi descriptor binding info reports binding feature flags", [] {
        auto binding = nr::rhi::DescriptorBindingInfo{};
        binding.descriptorType = vk::DescriptorType::eSampledImage;
        binding.bindingFlags = vk::DescriptorBindingFlagBits::eVariableDescriptorCount |
                               vk::DescriptorBindingFlagBits::ePartiallyBound |
                               vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        nr::test::require(binding.supportsVariableDescriptorCount(),
                          "binding should support variable descriptor count");
        nr::test::require(binding.isPartiallyBound(), "binding should support partially bound descriptors");
        nr::test::require(binding.isUpdateAfterBind(), "binding should support update-after-bind");
        nr::test::require(binding.supportsImmutableSampler() == false,
                          "sampled image binding should not support immutable sampler");
        nr::test::require(binding.semantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);

        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        nr::test::require(binding.supportsImmutableSampler(),
                          "combined image sampler should support immutable sampler");

        binding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
        nr::test::require(binding.usesDynamicDescriptorOffset(),
                          "dynamic uniform buffer should require dynamic offset");
    }};

const nr::test::CaseRegistrar descriptorWriteCacheBufferCase{
    "rhi descriptor write cache filters repeated buffer writes", [] {
        auto cache = nr::rhi::DescriptorWriteCache{};
        auto initialVersion = cache.version();
        auto write = makeBufferWrite(0u, 2u, 0u, 64u, 128u);

        auto changed =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::test::requireEqual(changed.size(), std::size_t{1});
        nr::test::require(cache.version() == initialVersion,
                          "filtering should not advance cache version before update commit");
        nr::rhi::commitDescriptorWrites(cache, changed);
        nr::test::require(cache.version() == initialVersion + 1u,
                          "committed first changed write should advance cache version");

        changed =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::test::require(changed.empty(), "identical buffer write should be skipped");
        nr::test::require(cache.version() == initialVersion + 1u, "skipped write should not advance cache version");

        auto offsetChanged = makeBufferWrite(0u, 2u, 0u, 96u, 128u);
        changed = nr::rhi::filterChangedDescriptorWrites(
            cache, std::span<const nr::rhi::DescriptorWriteRequest>{&offsetChanged, 1u});
        nr::test::requireEqual(changed.size(), std::size_t{1});
        nr::test::require(cache.version() == initialVersion + 1u,
                          "changed buffer offset should not advance cache version before commit");
        nr::rhi::commitDescriptorWrites(cache, changed);
        nr::test::require(cache.version() == initialVersion + 2u,
                          "committed buffer offset should advance cache version");

        auto otherSlot = makeBufferWrite(0u, 2u, 1u, 96u, 128u);
        changed = nr::rhi::filterChangedDescriptorWrites(
            cache, std::span<const nr::rhi::DescriptorWriteRequest>{&otherSlot, 1u});
        nr::test::requireEqual(changed.size(), std::size_t{1},
                               "different array element should be a distinct cache slot");

        auto beforeClearVersion = cache.version();
        cache.clear();
        nr::test::require(cache.version() == beforeClearVersion + 1u, "clear should advance cache version");

        changed = nr::rhi::filterChangedDescriptorWrites(
            cache, std::span<const nr::rhi::DescriptorWriteRequest>{&offsetChanged, 1u});
        nr::test::requireEqual(changed.size(), std::size_t{1}, "write after clear should be changed again");
    }};

const nr::test::CaseRegistrar descriptorWriteCacheTwoPhaseCase{
    "rhi descriptor write cache does not commit during filtering", [] {
        auto cache = nr::rhi::DescriptorWriteCache{};
        auto write = makeBufferWrite(0u, 2u, 0u, 64u, 128u);

        auto changed =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::test::requireEqual(changed.size(), std::size_t{1});
        nr::test::requireEqual(cache.version(), std::uint64_t{0});

        auto changedAgain =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::test::requireEqual(changedAgain.size(), std::size_t{1},
                               "same payload should remain changed until update commit");

        nr::rhi::commitDescriptorWrites(cache, changed);
        auto skipped =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::test::require(skipped.empty(), "same payload should be skipped after commit");
    }};

const nr::test::CaseRegistrar descriptorWriteCacheForceWriteCase{
    "rhi descriptor write cache accepts explicit forced writes", [] {
        auto cache = nr::rhi::DescriptorWriteCache{};
        auto write = makeBufferWrite(0u, 2u, 0u, 64u, 128u);

        auto changed =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::rhi::commitDescriptorWrites(cache, changed);

        auto skipped =
            nr::rhi::filterChangedDescriptorWrites(cache, std::span<const nr::rhi::DescriptorWriteRequest>{&write, 1u});
        nr::test::require(skipped.empty(), "same payload should still be skipped without forceWrite");

        auto forcedWrite = write;
        forcedWrite.forceWrite = true;
        auto forced = nr::rhi::filterChangedDescriptorWrites(
            cache, std::span<const nr::rhi::DescriptorWriteRequest>{&forcedWrite, 1u});
        nr::test::requireEqual(forced.size(), std::size_t{1},
                               "forceWrite should bypass descriptor payload cache filtering");
    }};

const nr::test::CaseRegistrar descriptorWriteCachePayloadCase{
    "rhi descriptor write cache compares every descriptor payload family", [] {
        auto cache = nr::rhi::DescriptorWriteCache{};
        auto writes = std::vector<nr::rhi::DescriptorWriteRequest>{
            nr::rhi::DescriptorWriteRequest{
                .binding = makeDescriptorBinding(0u, 0u, vk::DescriptorType::eCombinedImageSampler),
                .payload =
                    nr::rhi::ImageDescriptorWrite{
                        .imageView = vk::ImageView{fakeVkHandle<VkImageView>(0x2001u)},
                        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    },
            },
            nr::rhi::DescriptorWriteRequest{
                .binding = makeDescriptorBinding(0u, 1u, vk::DescriptorType::eUniformTexelBuffer),
                .payload =
                    nr::rhi::TexelBufferDescriptorWrite{
                        .view = vk::BufferView{fakeVkHandle<VkBufferView>(0x3001u)},
                    },
            },
            nr::rhi::DescriptorWriteRequest{
                .binding = makeDescriptorBinding(0u, 2u, vk::DescriptorType::eAccelerationStructureKHR),
                .payload =
                    nr::rhi::AccelerationStructureDescriptorWrite{
                        .accelerationStructure =
                            vk::AccelerationStructureKHR{fakeVkHandle<VkAccelerationStructureKHR>(0x4001u)},
                    },
            },
            nr::rhi::DescriptorWriteRequest{
                .binding = makeDescriptorBinding(0u, 3u, vk::DescriptorType::eInlineUniformBlock),
                .payload =
                    nr::rhi::InlineUniformDescriptorWrite{
                        .data = std::vector<std::uint8_t>{1u, 2u, 3u, 4u},
                    },
            },
        };

        auto changed = nr::rhi::filterChangedDescriptorWrites(cache, writes);
        nr::test::requireEqual(changed.size(), writes.size());
        nr::rhi::commitDescriptorWrites(cache, changed);

        changed = nr::rhi::filterChangedDescriptorWrites(cache, writes);
        nr::test::require(changed.empty(), "unchanged descriptor payload families should be skipped");

        auto modifiedWrites = writes;
        std::get<nr::rhi::ImageDescriptorWrite>(modifiedWrites[0].payload).imageLayout = vk::ImageLayout::eGeneral;
        std::get<nr::rhi::TexelBufferDescriptorWrite>(modifiedWrites[1].payload).view =
            vk::BufferView{fakeVkHandle<VkBufferView>(0x3002u)};
        std::get<nr::rhi::AccelerationStructureDescriptorWrite>(modifiedWrites[2].payload).accelerationStructure =
            vk::AccelerationStructureKHR{fakeVkHandle<VkAccelerationStructureKHR>(0x4002u)};
        std::get<nr::rhi::InlineUniformDescriptorWrite>(modifiedWrites[3].payload).data[3] = 5u;

        changed = nr::rhi::filterChangedDescriptorWrites(cache, modifiedWrites);
        nr::test::requireEqual(changed.size(), modifiedWrites.size(),
                               "field changes in every payload family should be detected");
    }};

} // namespace
