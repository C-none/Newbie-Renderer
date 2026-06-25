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
} // namespace
