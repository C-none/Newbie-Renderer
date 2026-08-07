import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] std::vector<nr::rhi::SlangProgram> compilePipelineBinaryPrograms()
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto const requests = std::array{
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/pipelineBinary/vertex"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/pipelineBinary/fragment"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/pipelineBinary/compute"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"test/pipelineBinary/raygen"},
        },
    };
    auto programs = shaderService.compileProgramsByFile(requests);
    nr::test::requireEqual(programs.size(), requests.size());
    nr::test::require(std::ranges::all_of(programs, &nr::rhi::SlangProgram::valid),
                      "pipeline-binary contract shaders should compile");
    return programs;
}

enum class ExpectedPipelineBinaryPath : std::uint8_t
{
    capture,
    load,
};

[[nodiscard]] std::filesystem::path uniquePipelineBinaryRoot()
{
    static std::atomic_uint64_t sequence = 0u;
    auto const timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto const threadIdentity = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::path{std::string{nr::psoCacheRoot}} / "contract" /
           std::format("pipeline_binary.{}.{}.{}", timestamp, threadIdentity,
                       sequence.fetch_add(1u, std::memory_order_relaxed));
}

void createAndValidatePipelines(nr::rhi::Device &device, const std::vector<nr::rhi::SlangProgram> &programs,
                                ExpectedPipelineBinaryPath expectedPath)
{
    auto graphicsDesc = nr::rhi::GraphicsPipelineDesc{};
    graphicsDesc.colorAttachmentFormats = {vk::Format::eR8G8B8A8Unorm};
    auto graphicsPrograms = std::span<const nr::rhi::SlangProgram>{programs}.first(2u);

    auto assembly = nr::rhi::RayTracingProgramAssemblyDesc{};
    assembly.stages = {
        nr::rhi::RayTracingPipelineStageSelection{
            .program = std::cref(programs[3]),
            .logicalEntryPointName = "pipelineBinaryRaygen",
        },
    };
    assembly.groups = {
        nr::rhi::RayTracingShaderGroupDesc{
            .name = "pipelineBinaryRaygen",
            .generalEntryPoint = "pipelineBinaryRaygen",
        },
    };

    auto expectedLoadCount = device.pipeline().pipelineBinaryLoadCount();
    auto expectedCaptureCount = device.pipeline().pipelineBinaryCaptureCount();
    auto requireExpectedPath = [&](std::string_view pipelineType) {
        if (expectedPath == ExpectedPipelineBinaryPath::load)
        {
            nr::test::requireEqual(
                device.pipeline().pipelineBinaryLoadCount(), ++expectedLoadCount,
                std::format("{} PSO should be recreated from persisted pipeline binaries", pipelineType));
        }
        else
        {
            nr::test::requireEqual(
                device.pipeline().pipelineBinaryCaptureCount(), ++expectedCaptureCount,
                std::format("{} PSO should persist a captured pipeline-binary artifact", pipelineType));
        }
    };

    auto graphics =
        device.pipeline()
            .createGraphicsPipeline(graphicsPrograms, graphicsDesc, 8u, {}, "PipelineBinary.Contract.Graphics")
            .get();
    nr::test::require(graphics.pipeline.valid(), "pipeline-binary graphics PSO should be valid");
    requireExpectedPath("graphics");

    auto compute =
        device.pipeline().createComputePipeline(programs[2], {}, 8u, {}, "PipelineBinary.Contract.Compute").get();
    nr::test::require(compute.pipeline.valid(), "pipeline-binary compute PSO should be valid");
    requireExpectedPath("compute");

    auto rayTracing =
        device.pipeline()
            .createRayTracingPipeline(programs[3], assembly, {}, 8u, {}, "PipelineBinary.Contract.RayTracing")
            .get();
    nr::test::require(rayTracing.pipeline.valid(), "pipeline-binary ray-tracing PSO should be valid");
    requireExpectedPath("ray-tracing");

    auto deeperRayTracingDesc = nr::rhi::RayTracingPipelineDesc{};
    deeperRayTracingDesc.maxRayRecursionDepth = 2u;
    auto deeperRayTracing = device.pipeline()
                                .createRayTracingPipeline(programs[3], assembly, deeperRayTracingDesc, 8u, {},
                                                          "PipelineBinary.Contract.RayTracing.Depth2")
                                .get();
    nr::test::require(deeperRayTracing.pipeline.valid(),
                      "pipeline-binary ray-tracing depth variant PSO should be valid");
    requireExpectedPath("ray-tracing depth variant");
}

void requireProjectFingerprintedArtifacts(const std::filesystem::path &cacheRoot)
{
    auto artifacts = std::filesystem::recursive_directory_iterator{cacheRoot} |
                     std::views::filter([](const std::filesystem::directory_entry &entry) {
                         return entry.is_regular_file() && entry.path().extension() == ".nrpso";
                     }) |
                     std::views::transform([](const std::filesystem::directory_entry &entry) { return entry.path(); }) |
                     std::ranges::to<std::vector>();
    nr::test::requireEqual(artifacts.size(), std::size_t{4u},
                           "cold PSO creation should persist one artifact per semantic PSO");

    auto projectFingerprints = std::set<std::string>{};
    std::ranges::for_each(artifacts, [&](const std::filesystem::path &artifact) {
        auto const stem = artifact.stem().string();
        auto const separator = stem.find('.');
        nr::test::require(separator == 16u,
                          "pipeline-binary artifact name should start with a 64-bit project fingerprint");
        auto const fingerprint = stem.substr(0u, separator);
        nr::test::require(fingerprint != "0000000000000000",
                          "pipeline-binary project fingerprint must never be all zero");
        nr::test::require(projectFingerprints.insert(fingerprint).second,
                          "semantic PSO variants should have distinct project fingerprints");
    });
}

const nr::test::CaseRegistrar pipelineBinaryCase{
    "rhi pipeline binary persists and recreates graphics compute and ray tracing PSOs", [] {
        auto programs = compilePipelineBinaryPrograms();
        auto const cacheRoot = uniquePipelineBinaryRoot();

        {
            auto coldDevice = nr::rhi::Device{};
            coldDevice.initialize("nr_rhi_pipeline_binary_contract_test", "NewbieRenderer", cacheRoot);
            createAndValidatePipelines(coldDevice, programs, ExpectedPipelineBinaryPath::capture);
            nr::test::requireEqual(coldDevice.pipeline().pipelineBinaryLoadCount(), std::uint64_t{0u},
                                   "isolated cold PSOs should not load persisted pipeline binaries");
            coldDevice.waitIdle();
        }

        requireProjectFingerprintedArtifacts(cacheRoot);

        {
            auto warmDevice = nr::rhi::Device{};
            warmDevice.initialize("nr_rhi_pipeline_binary_contract_test", "NewbieRenderer", cacheRoot);
            createAndValidatePipelines(warmDevice, programs, ExpectedPipelineBinaryPath::load);
            nr::test::requireEqual(warmDevice.pipeline().pipelineBinaryCaptureCount(), std::uint64_t{0u},
                                   "warm PSOs should not fall back to capture");
            warmDevice.waitIdle();
        }

        auto cleanupError = std::error_code{};
        std::filesystem::remove_all(cacheRoot, cleanupError);
        nr::test::require(!cleanupError, "pipeline-binary contract cache directory should be removable");
    }};
} // namespace
