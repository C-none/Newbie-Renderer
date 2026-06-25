import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
struct UiPushConstants
{
    glm::vec2 scale{};
    glm::vec2 translate{};
    std::uint32_t textureIndex = 0;
    glm::uvec3 padding{};
};

const nr::test::CaseRegistrar appUiCursorCase{
    "rhi shader cursor captures runtime descriptor array and push constants",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/appUi"},
        });
        nr::test::require(program.valid(), "appUi shader program should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program, nr::rhi::DescriptorBindingPolicy{
            .defaultRuntimeDescriptorCount = 16,
        });
        nr::test::require(layout.valid(), "appUi descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto samplerCursor = root["gUiSampler"];
        nr::test::require(samplerCursor.valid(), "sampler cursor should resolve");
        nr::test::require(samplerCursor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::Sampler);
        nr::test::require(!samplerCursor.referencesRuntimeDescriptorArray(), "sampler should not be runtime-sized");

        auto texturesCursor = root["gUiTextures"];
        nr::test::require(texturesCursor.valid(), "runtime texture cursor should resolve");
        nr::test::require(texturesCursor.referencesRuntimeDescriptorArray(), "gUiTextures should be runtime-sized");
        nr::test::require(texturesCursor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::requireEqual(*texturesCursor.bindingDescriptorCount(), 16u);

        auto textureElement = texturesCursor[5u];
        nr::test::require(textureElement.valid(), "runtime descriptor array element should resolve");
        nr::test::require(textureElement.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 42,
            .debugName = "ui-test-texture",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));

        auto pushCursor = root["gUiPush"];
        nr::test::require(pushCursor.valid(), "push constant cursor should resolve");
        nr::test::require(pushCursor.bindingKind() == nr::rhi::ShaderBindingKind::PushConstant);
        nr::test::require(pushCursor.setData(UiPushConstants{
            .scale = glm::vec2{2.0f, 2.0f},
            .translate = glm::vec2{-1.0f, -1.0f},
            .textureIndex = 5,
        }));

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{1});
        nr::test::requireEqual(snapshot.pushConstantWriteCount(), std::size_t{1});
        auto const &write = snapshot.descriptorWrites().front();
        nr::test::requireEqual(write.arrayElement, 5u);
        nr::test::require(write.binding.descriptorType == vk::DescriptorType::eSampledImage);
        nr::test::require(std::holds_alternative<nr::rhi::LogicalResourceDescriptorWrite>(write.payload));
        nr::test::requireEqual(std::get<nr::rhi::LogicalResourceDescriptorWrite>(write.payload).logicalResourceId, std::uint64_t{42});

        root.clearSnapshot();
        nr::test::require(root.snapshot().empty(), "clearSnapshot should remove descriptor and push writes");
    }};
} // namespace
