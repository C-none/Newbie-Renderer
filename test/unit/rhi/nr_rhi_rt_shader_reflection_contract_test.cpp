import std;
import dependency;
import nr.rhi;
import nr.test;

namespace
{
struct CameraData
{
    glm::vec4 origin{};
    glm::vec4 right{};
    glm::vec4 up{};
    glm::vec4 forward{};
};

const nr::test::CaseRegistrar rtShaderReflectionCase{
    "rhi rt shader reflection exposes AS image and camera bindings",
    [] {
        auto &shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/rt/minimalRtTriangle"},
        });
        nr::test::require(program.valid(), "minimal RT shader should compile");

        auto layout = nr::rhi::ShaderDescriptorLayout::create(program);
        nr::test::require(layout.valid(), "minimal RT descriptor layout should be valid");

        auto root = layout.rootCursor();
        auto scene = root["scene"];
        auto outputImage = root["outputImage"];
        auto camera = root["camera"];

        nr::test::require(scene.valid(), "RT scene cursor should resolve");
        nr::test::require(outputImage.valid(), "RT output image cursor should resolve");
        nr::test::require(camera.valid(), "RT camera cursor should resolve");

        nr::test::require(scene.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(outputImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(camera.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);

        nr::test::require(scene.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 100,
            .debugName = "logical-tlas",
        }));
        nr::test::require(outputImage.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 101,
            .debugName = "logical-output",
        }));
        nr::test::require(camera.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = 102,
            .debugName = "logical-camera",
        }));

        auto snapshot = root.snapshot();
        nr::test::requireEqual(snapshot.descriptorWriteCount(), std::size_t{3});
        auto semantics = snapshot.descriptorWrites() |
                         std::views::transform([](const nr::rhi::ShaderBindingRecord &record) {
                             return record.binding.semantic();
                         }) |
                         std::ranges::to<std::vector>();
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::AccelerationStructure));
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::StorageImage));
        nr::test::require(std::ranges::contains(semantics, nr::rhi::ShaderDescriptorSemantic::UniformBuffer));
    }};

const nr::test::CaseRegistrar rtObjectShaderReflectionCase{
    "rtobject display and RT instance hash shaders compile and expose expected bindings",
    [] {
        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        auto displayProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/displayConvert"},
        });
        nr::test::require(displayProgram.valid(), "rtobject display conversion shader should compile");

        auto displayLayout = nr::rhi::ShaderDescriptorLayout::create(displayProgram);
        nr::test::require(displayLayout.valid(), "rtobject display conversion layout should be valid");

        auto displayRoot = displayLayout.rootCursor();
        auto sourceColor = displayRoot["gSourceColor"];
        auto convertedColor = displayRoot["gConvertedColor"];
        auto displayPushConstants = displayRoot["gDisplayConvert"];

        nr::test::require(sourceColor.valid(), "display source color cursor should resolve");
        nr::test::require(convertedColor.valid(), "display converted color cursor should resolve");
        nr::test::require(displayPushConstants.valid(), "display push constant cursor should resolve");
        nr::test::require(sourceColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::SampledImage);
        nr::test::require(convertedColor.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(displayPushConstants.pushConstantRange().has_value());

        auto rtProgram = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/rtInstanceHash"},
        });
        nr::test::require(rtProgram.valid(), "rtobject RT instance hash shader should compile");

        auto rtLayout = nr::rhi::ShaderDescriptorLayout::create(rtProgram);
        nr::test::require(rtLayout.valid(), "rtobject RT instance hash layout should be valid");

        auto rtRoot = rtLayout.rootCursor();
        auto scene = rtRoot["scene"];
        auto outputImage = rtRoot["outputImage"];
        auto frameUniform = rtRoot["gFrame"];

        nr::test::require(scene.valid(), "RT instance hash TLAS cursor should resolve");
        nr::test::require(outputImage.valid(), "RT instance hash output cursor should resolve");
        nr::test::require(frameUniform.valid(), "RT instance hash global frame uniform cursor should resolve");
        nr::test::require(scene.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::AccelerationStructure);
        nr::test::require(outputImage.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::StorageImage);
        nr::test::require(frameUniform.descriptorSemantic() == nr::rhi::ShaderDescriptorSemantic::UniformBuffer);
    }};
} // namespace
