import dependency.json;
import std;
import nr.options;
import nr.rhi;
import nr.renderer;
import nr.renderPasses;
import nr.resource;
import nr.scene;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] nr::options::OptionFrameSnapshot makeOptionSnapshot(
    std::vector<nr::options::OptionDefinition> definitions)
{
    auto builder = nr::options::OptionCatalogBuilder{};
    std::ranges::for_each(
        definitions,
        [&](nr::options::OptionDefinition definition) {
            nr::test::require(builder.add(std::move(definition)));
        });
    auto result = builder.build();
    nr::test::require(result.valid());

    auto values = nr::options::OptionValueMap{};
    auto availability = nr::options::OptionAvailabilityMap{};
    std::ranges::for_each(result.catalog->definitions(), [&](const auto& entry) {
        values.emplace(entry.first, entry.second.defaultValue);
        availability.emplace(
            entry.first,
            nr::options::OptionAvailability{.available = true, .reason = {}});
    });
    return nr::options::OptionFrameSnapshot{
        .catalog = std::move(result.catalog),
        .values = std::move(values),
        .availability = std::move(availability),
        .frameIndex = 1u,
        .revision = 1u,
        .graphGeneration = 1u,
        .bindingEpoch = 1u,
        .snapshotToken = "renderpasses-cache-contract",
    };
}

const nr::test::CaseRegistrar migratedRenderPassBranchSnapshotsCase{
    "all rtobject nodes opt into exact Skeleton branch snapshots",
    [] {
        auto pathTracing = nr::renderPasses::PathTracingNode{};
        nr::test::require(pathTracing.supportsRenderGraphSkeleton());
        auto pathDefaultSnapshot = makeOptionSnapshot(nr::options::makePathTracingDefinitions());
        auto const pathDefault = pathTracing.structuralSnapshot(
            nr::renderer::NodeFrameParameters{
                .optionSnapshot = std::cref(pathDefaultSnapshot),
            });
        nr::test::require(pathDefault.has_value());
        auto pathVariantSnapshot = pathDefaultSnapshot;
        pathVariantSnapshot.values.insert_or_assign(
            nr::options::optionId(nr::options::keys::pathTracingMaxSurfaceBounces),
            nr::options::OptionWireValue{std::uint64_t{7u}});
        auto const pathVariant = pathTracing.structuralSnapshot(
            nr::renderer::NodeFrameParameters{
                .optionSnapshot = std::cref(pathVariantSnapshot),
            });
        nr::test::require(pathVariant.has_value());
        nr::test::require(pathDefault->branchKey != pathVariant->branchKey);

        auto pathFilterAfterShadingSnapshot = pathDefaultSnapshot;
        pathFilterAfterShadingSnapshot.values.insert_or_assign(
            nr::options::optionId(
                nr::options::keys::pathTracingFilterAfterShadingEnabled),
            nr::options::OptionWireValue{true});
        auto const pathFilterAfterShading = pathTracing.structuralSnapshot(
            nr::renderer::NodeFrameParameters{
                .optionSnapshot =
                    std::cref(pathFilterAfterShadingSnapshot),
            });
        nr::test::require(pathFilterAfterShading.has_value());
        nr::test::require(
            pathDefault->branchKey != pathFilterAfterShading->branchKey,
            "Filter After Shading must select a distinct PathTracing structural branch");

        auto dlss = nr::renderPasses::DlssRayReconstructionNode{};
        nr::test::require(dlss.supportsRenderGraphSkeleton());
        auto dlssDefaultSnapshot = makeOptionSnapshot(nr::options::makeDlssDefinitions());
        auto const dlssDefault = dlss.structuralSnapshot(
            nr::renderer::NodeFrameParameters{
                .optionSnapshot = std::cref(dlssDefaultSnapshot),
            });
        nr::test::require(dlssDefault.has_value());
        dlss.input.create.flags.alphaUpscaling = true;
        auto dlssDebugSnapshot = dlssDefaultSnapshot;
        dlssDebugSnapshot.values.insert_or_assign(
            nr::options::optionId(nr::options::keys::dlssVisualizeMotionVectors),
            nr::options::OptionWireValue{true});
        auto const dlssAlphaDebug = dlss.structuralSnapshot(
            nr::renderer::NodeFrameParameters{
                .optionSnapshot = std::cref(dlssDebugSnapshot),
            });
        nr::test::require(dlssAlphaDebug.has_value());
        nr::test::require(dlssDefault->branchKey != dlssAlphaDebug->branchKey);

        auto ui = nr::renderPasses::UiNode{};
        nr::test::require(ui.supportsRenderGraphSkeleton());

        auto accelerationStructure = nr::renderPasses::AccelerationStructureBuildNode{};
        nr::test::require(accelerationStructure.supportsRenderGraphSkeleton());
    }};

[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto path = std::filesystem::path{std::string{nr::projectRoot}} / relativePath;
    auto file = std::ifstream{path};
    nr::test::require(file.good(), std::format("failed to open {}", path.generic_string()));

    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

void requireAbsent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.find(token) == std::string::npos, std::string{message});
}

void requirePresent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.find(token) != std::string::npos, std::string{message});
}

const nr::test::CaseRegistrar singleEntryPointShaderFileCase{
    "each Slang source file declares at most one shader entry point",
    [] {
        auto shaderSources =
            std::filesystem::recursive_directory_iterator{
                std::filesystem::path{std::string{nr::projectRoot}} / "shader"} |
            std::views::filter([](const std::filesystem::directory_entry& entry) {
                return
                    entry.is_regular_file() &&
                    entry.path().extension() == ".slang";
            });

        std::ranges::for_each(shaderSources, [](const auto& entry) {
            auto const relativePath = std::filesystem::relative(
                entry.path(),
                std::filesystem::path{std::string{nr::projectRoot}});
            auto const source = readProjectFile(relativePath);
            auto const entryPointAttribute = std::regex{
                R"(\[\s*shader\s*\()",
                std::regex_constants::ECMAScript | std::regex_constants::optimize};
            auto const matches = std::ranges::subrange{
                std::sregex_iterator{source.begin(), source.end(), entryPointAttribute},
                std::sregex_iterator{}};
            auto const entryPointCount = std::ranges::distance(matches);
            nr::test::require(
                entryPointCount <= 1,
                std::format(
                    "shader source '{}' declares {} entry points; each file may declare at most one",
                    relativePath.generic_string(),
                    entryPointCount));
        });
    }};

const nr::test::CaseRegistrar renderPassShaderRequestCollectionCase{
    "render passes declare ordered static single-entry shader requests",
    [] {
        auto requireRequests = []<std::size_t Count>(
                                   const nr::renderer::NodeRuntime& node,
                                   const std::array<std::string_view, Count>& expectedPaths) {
            auto const requests = node.shaderRequests();
            nr::test::requireEqual(requests.size(), expectedPaths.size());
            std::ranges::for_each(
                std::views::iota(std::size_t{0}, requests.size()),
                [&](std::size_t index) {
                    nr::test::requireEqual(
                        requests[index].sourcePath.generic_string(),
                        std::string{expectedPaths[index]});
                    nr::test::require(
                        requests[index].variant.assignments.empty(),
                        "static render-pass shaders should not carry unrelated variants");
                });
        };

        requireRequests(
            nr::renderPasses::AccumulateNode{},
            std::array{std::string_view{"renderer/accumulate"}});
        requireRequests(
            nr::renderPasses::DlssRayReconstructionNode{},
            std::array{std::string_view{"renderer/dlssRayReconstructionDebug"}});
        requireRequests(
            nr::renderPasses::EmbeddedTriangleNode{},
            std::array{
                std::string_view{"renderer/embeddedTriangle/vertex"},
                std::string_view{"renderer/embeddedTriangle/fragment"},
            });
        requireRequests(
            nr::renderPasses::NormalBufferNode{},
            std::array{
                std::string_view{"renderer/normalBuffer/vertex"},
                std::string_view{"renderer/normalBuffer/fragment"},
            });
        requireRequests(
            nr::renderPasses::PresentNode{},
            std::array{std::string_view{"renderer/presentConvert"}});
        requireRequests(
            nr::renderPasses::UiNode{},
            std::array{
                std::string_view{"renderer/appUi/vertex"},
                std::string_view{"renderer/appUi/fragment"},
            });
        requireRequests(
            nr::renderPasses::PathTracingNode{},
            std::array<std::string_view, 0>{});
    }};

void requireOrdered(
    std::string_view contents,
    std::string_view beforeToken,
    std::string_view afterToken,
    std::string_view message)
{
    auto const before = contents.find(beforeToken);
    auto const after = contents.find(afterToken);
    nr::test::require(
        before != std::string_view::npos &&
            after != std::string_view::npos &&
            before < after,
        std::string{message});
}

[[nodiscard]] std::string_view sourceSection(
    std::string_view contents,
    std::string_view beginToken,
    std::string_view endToken)
{
    auto const begin = contents.find(beginToken);
    nr::test::require(begin != std::string_view::npos, "source section begin token is missing");
    auto const end = contents.find(endToken, begin + beginToken.size());
    nr::test::require(end != std::string_view::npos, "source section end token is missing");
    return contents.substr(begin, end - begin);
}

const nr::test::CaseRegistrar materialFilterPacketAdvanceCase{
    "path tracing material-filter reservation advances exactly three rand4 packets",
    [] {
        auto const seeds = std::array{
            0u,
            1u,
            0x12345678u,
            0x80000000u,
            0xffffffffu,
        };
        std::ranges::for_each(seeds, [](std::uint32_t seed) {
            auto expanded = seed;
            std::ranges::for_each(
                std::views::iota(0u, 15u),
                [&](auto) {
                    expanded =
                        expanded * 0x915f77f5u + 0x93d765ddu;
                });

            auto const collapsed =
                seed * 0x98a5741du + 0xacfbeaa7u;
            nr::test::requireEqual(
                expanded,
                collapsed,
                "fifteen LCG steps must equal the affine skip for three rand4 packets");
        });
    }};

static_assert(std::same_as<nr::renderer::RendererTlasTextureRevisionProjection, nr::scene::SceneRtStructuralRevisionProjection>);

const nr::test::CaseRegistrar renderPassesRendererCacheOwnershipCase{"renderpasses no longer own renderer/RDG descriptor table cache state", [] {
                                                                         auto normalBuffer = readProjectFile("src/renderPasses/NormalBuffer/nrNormalBufferNode.cpp");
                                                                         auto embeddedTriangle = readProjectFile("src/renderPasses/EmbeddedTriangle/nrEmbeddedTriangleNode.cpp");
                                                                         auto pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
                                                                         auto normalBufferShader = readProjectFile("shader/renderer/normalBuffer/fragment.slang");
                                                                         auto pathTracingInterface = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.ixx");
                                                                         auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
                                                                         auto accumulateInterface = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.ixx");
                                                                         auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
                                                                         auto sceneTextureBinding = readProjectFile("src/renderPasses/nrSceneTextureTableBinding.ixx");
                                                                         auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
                                                                         auto rendererCacheInterface = readProjectFile("src/renderer/nrRendererCache.ixx");
                                                                          auto rendererImplementation =
                                                                              readProjectFile("src/renderer/nrRenderer.cpp") +
                                                                              readProjectFile("src/renderer/nrRendererPassBuilders.cpp");
                                                                          auto pipelineImplementation =
                                                                              readProjectFile("src/pipeline/nrPipeline.cpp");
                                                                          auto accelerationStructureBuild = readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");

                                                                         requireAbsent(normalBuffer, "SceneTextureTableBindingCache", "NormalBuffer must use renderer-owned bindless table cache instead of a node-local scene cache");
                                                                         requireAbsent(pathTracing, "SceneTextureTableBindingCache", "PathTracing must use renderer-owned bindless table cache instead of a node-local scene cache");
                                                                         requireAbsent(ui, "appliedTextureTableRevisionByFrame", "Ui must not keep per-frame applied texture table revisions");
                                                                         requireAbsent(ui, "ensureBindlessTextureBindingSetsForFrame", "Ui texture table binding-set allocation should be owned by renderer bindless cache");
                                                                         requireAbsent(ui, "if (tablePrepare.requiresDescriptorCacheInvalidation)", "Ui must not clear descriptor write cache based on bindless table prepare state");
                                                                         requirePresent(ui, ".refreshActiveDescriptorsOnCacheHit = true", "Ui GPU-AV descriptor refresh should request active descriptor writes on bindless cache hits");
                                                                         requireAbsent(ui, ".forceDescriptorWritesOnCacheHit = true", "Ui GPU-AV descriptor refresh should use one cache-hit refresh option");
                                                                         requireAbsent(sceneTextureBinding, "resetSceneTextureTableFrameCache", "scene texture table helper should not own frame-slot cache reset state");
                                                                         requirePresent(normalBuffer, "sceneTextureTableImmutableSamplerBinding()", "NormalBuffer should install the scene texture table immutable sampler before graphics PSO creation");
                                                                         requirePresent(pathTracing, "sceneTextureTableImmutableSamplerBinding()", "PathTracing should install the scene texture table immutable sampler before RT PSO creation");
                                                                          requireAbsent(pathTracingInterface, "PathTracingVariantKey variant{}", "PathTracing input must not retain a second writable option value");
                                                                          requirePresent(pathTracingInterface, "enableRussianRoulette", "PathTracing variant key should expose the Russian roulette toggle");
                                                                          requirePresent(pathTracingInterface, "enableFilterAfterShading", "PathTracing variant key should expose the compile-time FAS toggle");
                                                                         requirePresent(pathTracing, "std::map<PathTracingPipelineKey, std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::RayTracingPipeline>>>", "PathTracing should cache RT pipelines by root variant and CHS permutation set");
                                                                         requirePresent(pathTracing, "std::map<PathTracingSbtKey, nr::rhi::ShaderBindingTable>", "PathTracing should cache SBTs separately from pipeline runtimes");
                                                                         requirePresent(pathTracing, "std::uint64_t chsPermutationSetHash", "PathTracing pipeline keys should include the CHS permutation set");
                                                                         requirePresent(pathTracing, "std::uint64_t hitRecordPlanHash", "PathTracing SBT keys should include the hit record plan");
                                                                         requirePresent(pathTracing, "std::uint64_t shaderSessionGeneration", "PathTracing pipeline keys should invalidate on shader service session reload");
                                                                         requirePresent(pathTracing, "ShaderService::instance().sessionGeneration()", "PathTracing should capture the current shader session generation in the pipeline key");
                                                                         requirePresent(pathTracing, "ensurePathTracingFrameRuntime", "PathTracing should compose the current frame pipeline and SBT from separate node-owned caches");
                                                                         requireAbsent(pathTracing, "PathTracingRuntimeKey", "PathTracing should not merge root variants, CHS permutations, and SBT records into one runtime key");
                                                                         requireAbsent(pathTracing, "PathTracingVariantRuntime", "PathTracing should keep pipeline and SBT runtime state in separate caches");
                                                                         requireAbsent(pathTracingInterface, "variantUiDraft_", "PathTracing must not retain a node-local UI draft");
                                                                         requireAbsent(pathTracingInterface, "pendingVariant_", "PathTracing must not retain a node-local pending variant");
                                                                         requirePresent(pathTracingInterface, "void declareOptions", "PathTracing must declare its controls through the option catalog");
                                                                         requirePresent(pathTracing, "pathTracingVariant(frameParameters.optionSnapshot.get())", "PathTracing must derive its active variant from the immutable frame option snapshot");
                                                                         requireAbsent(pathTracing, "context.variants", "PathTracing should not access renderer-owned variant state");
                                                                         requirePresent(pathTracing, "createPathTracingPipelineRuntime(device, pipelineKey, hitSbtPlan)", "PathTracing pipeline cache misses should rebuild the PSO synchronously on the build thread");
                                                                         requirePresent(pathTracing, "createPathTracingShaderBindingTable(device, pipelineRuntime, sbtKey, hitSbtPlan)", "PathTracing SBT cache misses should rebuild only the SBT for the active record plan");
                                                                         requireAbsent(pathTracingInterface, "collectUi", "PathTracing must not expose a node-local mutation UI");
                                                                         requireAbsent(rendererInterface, "VariantStateRegistry", "Renderer interfaces should not expose a shared variant registry");
                                                                         requireAbsent(rendererCacheInterface, "variantRegistry", "Renderer cache suite should not own node variant state");
                                                                         requireAbsent(rendererImplementation, "commitFramePatches", "Renderer should not commit shader variant patches");
                                                                         requireAbsent(rendererImplementation, "collectVariantUiSections", "Renderer should not append registry-generated variant UI sections");
                                                                         requirePresent(rendererInterface, "std::span<const nr::rhi::SlangProgram> shaderPrograms{}", "Node initialization should receive only its ordered slice of precompiled shaders");
                                                                         requirePresent(rendererInterface, "shaderRequests() const", "Node runtimes should declare static shader requirements before initialization");
                                                                         requirePresent(rendererImplementation, "createInfo.runtime->shaderRequests()", "Renderer graph installation should collect every node's static shader requirements");
                                                                         requirePresent(rendererImplementation, "compileProgramsByFile(shaderRequests)", "Renderer graph installation should submit one flattened static shader batch");
                                                                         requirePresent(rendererImplementation, ".shaderPrograms = std::span<const nr::rhi::SlangProgram>{shaderPrograms}.subspan(", "Renderer should return each node's ordered shader-program slice only after batch completion");
                                                                         requireAbsent(normalBuffer, "ShaderService::instance()", "NormalBuffer initialization must consume the renderer-provided batch result");
                                                                         requireAbsent(embeddedTriangle, "ShaderService::instance()", "EmbeddedTriangle initialization must consume the renderer-provided batch result");
                                                                         requireAbsent(accumulate, "ShaderService::instance()", "Accumulate initialization must consume the renderer-provided batch result");
                                                                         requireAbsent(ui, "ShaderService::instance()", "Ui initialization must consume the renderer-provided batch result");
                                                                         requirePresent(rendererImplementation, "createInfo.config.instanceName.empty()", "Renderer graph preflight should require NodeConfig as the node-name source");
                                                                         requireAbsent(pathTracing, "RendererCacheSuite", "PathTracing variant PSOs must not be stored in RendererCacheSuite");
                                                                         requirePresent(rendererImplementation, "makeTlasTextureCollectionKey", "Renderer should cache TLAS-only scene texture collection by an exact structural key");
                                                                         requirePresent(rendererInterface, "std::optional<RendererTlasTextureCollectionKey> tlasTextureCollectionKey_", "Renderer should own the TLAS texture collection cache");
                                                                         requirePresent(rendererInterface, "using RendererTlasTextureRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;", "Renderer should reuse the canonical scene RT structural revision projection");
                                                                         requirePresent(accelerationStructureBuild, "RtStructuralPlanCache structuralPlan", "AS runtime should own the immutable structural metadata/SBT plan cache");
                                                                         requirePresent(accelerationStructureBuild, "using AsStructuralRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;", "AS should reuse the canonical scene RT structural revision projection");
                                                                         requirePresent(accelerationStructureBuild, "appliedStructuralPlanGeneration", "AS frame slots should track the static plan generation already uploaded");
                                                                         requirePresent(accelerationStructureBuild, "runtime.activeSceneIdentity != revisions.sceneIdentity", "AS cache reuse should have an explicit scene identity boundary");
                                                                         requirePresent(accelerationStructureBuild, "std::vector<AsStructuralMeshSemanticEntry> meshSemantics", "AS structural keys should store unique mesh semantics separately from ordered packet identity");
                                                                         requirePresent(accelerationStructureBuild, ".semanticKey = entry.second.cachedBuild.get().semanticKey", "AS structural keys should reuse semantic keys from the current BLAS scan");
                                                                         requirePresent(accelerationStructureBuild, "std::shared_ptr<const SceneRtHitSbtPlan> hitSbtPlan", "AS structural plans should share immutable SBT plans across graph frames");
                                                                         requirePresent(pathTracing, "resolveBuildFrameData<std::shared_ptr<const SceneRtHitSbtPlan>>", "PathTracing should resolve shared immutable SBT plan ownership");
                                                                         requirePresent(accelerationStructureBuild, "RtMaterialRevisionProjection revisions", "RT material cache keys should include authoritative material and texture revisions");
                                                                         requirePresent(accelerationStructureBuild, "std::optional<BlasRevisionProjection> blasRevisions", "AS runtime should retain the mesh revision projection used by cached BLAS descriptors");
                                                                         requirePresent(accelerationStructureBuild, "runtime.blasRevisions.has_value() && *runtime.blasRevisions != blasRevisions", "mesh revision changes alone should invalidate the BLAS subcache");
                                                                         requirePresent(accelerationStructureBuild, "entry.second.cachedBuild = {};", "mesh content or layout revision changes should invalidate cached BLAS descriptors");
                                                                         requirePresent(accelerationStructureBuild, "replacementBlasAtlasCapacity", "revision-only BLAS atlas replacement should preserve capacity without applying the growth policy");
                                                                         requirePresent(accelerationStructureBuild, "createBlasAtlas(runtime, device, requiredBytes, capacityOverflow)", "only actual BLAS atlas overflow should select capacity growth");
                                                                         requirePresent(accelerationStructureBuild, "recordBuildTlas", "AS optimization should preserve unconditional per-frame TLAS rebuild recording");
                                                                         requirePresent(sceneTextureBinding, ".usesImmutableSampler = true", "scene texture table descriptor writes should rely on the immutable sampler in the PSO layout");
                                                                         requirePresent(sceneTextureBinding, "sceneTextureTableNearestSamplerDesc", "scene texture table should expose its nearest immutable sampler");
                                                                         requirePresent(sceneTextureBinding, ".magFilter = vk::Filter::eNearest", "scene texture table immutable sampler should use nearest magnification");
                                                                         requirePresent(sceneTextureBinding, ".minFilter = vk::Filter::eNearest", "scene texture table immutable sampler should use nearest minification");
                                                                         requirePresent(sceneTextureBinding, ".mipmapMode = vk::SamplerMipmapMode::eNearest", "scene texture table immutable sampler should disable mip interpolation");
                                                                         requirePresent(sceneTextureBinding, ".minLod = 0.0f", "scene texture table immutable sampler should clamp its minimum LOD to zero");
                                                                         requirePresent(sceneTextureBinding, ".maxLod = 0.0f", "scene texture table immutable sampler should clamp its maximum LOD to zero");
                                                                         requirePresent(normalBufferShader, ".Sample(normalUv)", "NormalBuffer should retain its raster implicit-LOD texture-coordinate policy over the nearest scene sampler");
                                                                         requirePresent(pathTracing, ".magFilter = vk::Filter::eLinear", "PathTracing environment sampling should retain linear magnification");
                                                                         requirePresent(pathTracing, ".minFilter = vk::Filter::eLinear", "PathTracing environment sampling should retain linear minification");
                                                                         requirePresent(pathTracing, ".mipmapMode = vk::SamplerMipmapMode::eLinear", "PathTracing environment sampling should remain independent from the nearest scene texture table");
                                                                         requirePresent(rendererImplementation, "MissingMaterialTexturePolicy::allowUnavailableAnisotropy", "TLAS texture collection should explicitly opt into the anisotropy-only unavailable-texture policy");
                                                                         requirePresent(rendererImplementation, "slotIndex == anisotropySlotIndex", "the unavailable-texture exception must be limited to the anisotropy slot");
                                                                         requireAbsent(rendererInterface, "sceneTextureSampler", "Renderer global resources should not expose a per-frame scene texture sampler");
                                                                         requireAbsent(rendererImplementation, "SceneTextureSampler", "Renderer should not create a separate scene texture sampler for gSceneTextures");
                                                                         requirePresent(rendererImplementation, "vk::PipelineStageFlagBits2::eAllGraphics);", "RasterPassBuilder should stamp raster passes with a graphics shader scope");
                                                                         requirePresent(rendererImplementation, "vk::PipelineStageFlagBits2::eComputeShader);", "ComputePassBuilder should stamp compute passes with compute shader scope");
                                                                         requirePresent(rendererImplementation, "vk::PipelineStageFlagBits2::eRayTracingShaderKHR);", "RayTracingPassBuilder should stamp RT passes with ray tracing shader scope");
                                                                         requirePresent(rendererInterface, "withOptionalShaderStages", "Shader-visible pass builders should support per-resource shader stage overrides");
                                                                         requireAbsent(rendererInterface, "descriptorCacheOwnerId()", "PipelineRuntime should not expose a cache owner id for bindless tables");
                                                                         requireAbsent(rendererInterface, "bindingSetGenerationForFrame", "PipelineRuntime should not expose per-frame binding-set generations for bindless table cache");
                                                                         requireAbsent(rendererCacheInterface, "bindingSetGenerations", "BindlessImageTableCache should not track binding-set generations for this UI GPU-AV workaround");
                                                                         requirePresent(rendererCacheInterface, "reinterpret_cast<std::uintptr_t>", "BindlessImageTableCache should key table ownership by pipeline runtime object address within the cache lifetime");
                                                                         requirePresent(embeddedTriangle, "ShaderStageIntent::Vertex", "EmbeddedTriangle frame uniform should be scoped to vertex shader access");
                                                                         requirePresent(normalBuffer, "ShaderStageIntent::Vertex", "NormalBuffer frame uniform should be scoped to vertex shader access");
                                                                         requirePresent(ui, "ShaderStageIntent::Fragment", "Ui texture samples should be scoped to fragment shader access");
                                                                         requirePresent(accumulate, "ComputePassBuilder", "Accumulate must use renderer-side compute pass builder descriptor handling");
                                                                         requirePresent(accumulate, "std::optional<AccumulateCameraTransform> previousCameraTransform{}", "Accumulate must own its previous unjittered camera transform");
                                                                         requirePresent(accumulate, "frameParameters.renderCameraConstants.view", "Accumulate must compare the unjittered render-camera view matrix");
                                                                         requirePresent(accumulate, "frameParameters.renderCameraConstants.projection", "Accumulate must compare the unjittered render-camera projection matrix");
                                                                         requirePresent(accumulate, "accumulateCameraTransformsEquivalent", "Accumulate must derive its reset from its node-local camera transform snapshot");
                                                                         requirePresent(accumulate, "frameParameters.resolutionPlan.resetHistory", "Accumulate must consume renderer-wide temporal resets such as environment replacement");
                                                                         requirePresent(accumulate, "std::uint32_t historySampleCount = 0u", "Accumulate must own its history sample count");
                                                                         requirePresent(accumulateInterface, "kAccumulateMaxHistorySampleCount = 4096u", "Accumulate should own its 4096-sample implementation cap");
                                                                         requireAbsent(accumulate, "cameraFrameState", "Accumulate history reset and weighting must not depend on another node's camera state");
                                                                         requirePresent(rendererImplementation, "temporalHistoryResetPending_ = true;", "Environment replacement must queue a renderer-wide temporal history reset");
                                                                         requirePresent(rendererImplementation, "resolutionPlan.resetHistory || temporalHistoryResetPending_", "Renderer must merge an environment replacement reset into the next frame plan");
                                                                         requirePresent(rendererInterface, "void requestTemporalHistoryReset() noexcept;", "Renderer should expose the narrow temporal-history reset request used by committed options");
                                                                         auto const resetRequestBody = sourceSection(
                                                                             rendererImplementation,
                                                                             "void Renderer::requestTemporalHistoryReset() noexcept",
                                                                             "[[nodiscard]] RendererGraphPreflightResult Renderer::preflightGraph");
                                                                         requirePresent(resetRequestBody, "temporalHistoryResetPending_ = true;", "An explicit temporal-history reset request should arm only the pending renderer reset");
                                                                         requireAbsent(resetRequestBody, "sampleFrameOrdinal_", "A temporal-history reset request must not restart the monotonic sampling sequence");
                                                                         requirePresent(pipelineImplementation, "auto const resetsTemporalHistory = definition->resetsTemporalHistory;", "Option execution should snapshot the selected definition's temporal-reset policy before commit");
                                                                         requirePresent(pipelineImplementation, "app.renderer().requestTemporalHistoryReset();", "A successfully committed temporal-resetting option should request one renderer-wide reset");
                                                                         requireAbsent(accumulate, "VariantItemEffect::RuntimeOnly", "Accumulate max history samples should not be registered as a runtime-only variant item");
                                                                         requirePresent(accumulate, "maxHistorySampleCount(frameParameters.optionSnapshot.get())", "Accumulate must read its maximum history sample count from the immutable frame option snapshot");
                                                                         requireAbsent(accumulate, "AccumulateNode::collectUi", "Accumulate must not expose a node-local mutation UI");
                                                                         requireAbsent(rendererImplementation, "snapshot.desc.effect != VariantItemEffect::RuntimeOnly", "Renderer should not contain generated runtime-only variant UI branching");
                                                                     }};

const nr::test::CaseRegistrar rendererTlasTextureKeyCase{"renderer TLAS texture key ignores transform and mask while retaining exact topology", [] {
                                                             using Domain = nr::scene::SceneRtRevisionDomain;
                                                             auto revisions = nr::revision::RevisionSet<Domain>{};
                                                             auto snapshot = nr::scene::SceneRevisionSnapshot{
                                                                 .sceneIdentity = 7u,
                                                                 .rt = revisions.snapshot(),
                                                             };
                                                             auto packet = nr::renderer::RendererTlasTexturePacketIdentity{
                                                                 .renderableId = 11u,
                                                                 .mesh = nr::resource::MeshHandle{3u, 2u},
                                                                 .tlasBucket = 4u,
                                                             };
                                                             auto baseline = nr::renderer::RendererTlasTextureCollectionKey{
                                                                 .sceneIdentity = snapshot.sceneIdentity,
                                                                 .revisions = nr::renderer::RendererTlasTextureRevisionProjection::capture(snapshot.rt),
                                                                 .packets = {packet},
                                                             };

                                                             revisions.advance<Domain::transform, Domain::traceMask>();
                                                             auto dynamicOnly = baseline;
                                                             dynamicOnly.revisions = nr::renderer::RendererTlasTextureRevisionProjection::capture(revisions.snapshot());
                                                             nr::test::requireEqual(dynamicOnly, baseline);

                                                             revisions.advance<Domain::topology>();
                                                             auto topologyChanged = dynamicOnly;
                                                             topologyChanged.revisions = nr::renderer::RendererTlasTextureRevisionProjection::capture(revisions.snapshot());
                                                             nr::test::require(topologyChanged != baseline, "topology revision must invalidate TLAS texture collection");

                                                             auto orderedPacketChanged = baseline;
                                                             orderedPacketChanged.packets.front().renderableId += 1u;
                                                             nr::test::require(orderedPacketChanged != baseline, "ordered packet structural identity must be an exact discriminator");

                                                             auto sceneChanged = baseline;
                                                             sceneChanged.sceneIdentity += 1u;
                                                             nr::test::require(sceneChanged != baseline, "scene identity must be an exact cache boundary");
                                                         }};

const nr::test::CaseRegistrar sceneRtMutationPolicyCase{"scene RT mutation policy separates dynamic and static cache invalidation", [] {
                                                            using Domain = nr::scene::SceneRtRevisionDomain;
                                                            using Mutation = nr::scene::SceneRevisionMutation;
                                                            using Mask = nr::revision::RevisionMask<Domain>;

                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalTransform), Mask::of<Domain::transform>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalTraceMask), Mask::of<Domain::traceMask>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalVisibility), Mask::of<Domain::topology, Domain::visibility>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::instanceAdded), Mask::of<Domain::topology, Domain::transform, Domain::visibility, Domain::traceMask, Domain::meshBinding>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::instanceRemoved), Mask::of<Domain::topology, Domain::transform, Domain::visibility, Domain::traceMask, Domain::meshBinding>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalMeshBinding), Mask::of<Domain::topology, Domain::meshBinding>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalMaterialPayload), Mask::of<Domain::materialPayload>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalMaterialBinding), Mask::of<Domain::topology, Domain::materialBinding>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalMeshContent), Mask::of<Domain::meshContent>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalMeshLayout), Mask::of<Domain::topology, Domain::meshLayout>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalTextureBinding), Mask::of<Domain::topology, Domain::textureBinding>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalTextureContent), Mask::of<Domain::textureContent>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalTextureResidency), Mask::of<Domain::topology, Domain::textureResidency>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::meshResident), Mask::of<Domain::topology, Domain::meshContent>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::materialResident), Mask::of<Domain::topology, Domain::materialPayload>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::textureResident), Mask::of<Domain::topology, Domain::textureResidency>());
                                                            nr::test::requireEqual(nr::scene::SceneRevisionMutationPolicy::mask(Mutation::externalEcsMutation),
                                                                                   Mask::of<Domain::topology, Domain::transform, Domain::visibility, Domain::traceMask, Domain::meshBinding, Domain::meshContent, Domain::meshLayout, Domain::materialBinding, Domain::materialPayload, Domain::textureBinding, Domain::textureContent,
                                                                                            Domain::textureResidency>());

                                                            auto const assertProjectionBehavior = [](Mutation mutation, bool staticInvalidation) {
                                                                auto revisions = nr::revision::RevisionSet<Domain>{};
                                                                auto const baseline = revisions.snapshot();
                                                                auto batch = nr::revision::RevisionBatch<Domain, Mutation, nr::scene::SceneRevisionMutationPolicy>{revisions};
                                                                batch.apply(mutation);
                                                                batch.commit();
                                                                auto const changed = revisions.snapshot();

                                                                auto const rendererBefore = nr::renderer::RendererTlasTextureRevisionProjection::capture(baseline);
                                                                auto const rendererAfter = nr::renderer::RendererTlasTextureRevisionProjection::capture(changed);
                                                                auto const asBefore = nr::scene::SceneRtStructuralRevisionProjection::capture(baseline);
                                                                auto const asAfter = nr::scene::SceneRtStructuralRevisionProjection::capture(changed);
                                                                nr::test::require((rendererAfter != rendererBefore) == staticInvalidation, "renderer static projection mutation classification mismatch");
                                                                nr::test::require((asAfter != asBefore) == staticInvalidation, "AS static projection mutation classification mismatch");
                                                            };

                                                            assertProjectionBehavior(Mutation::externalTransform, false);
                                                            assertProjectionBehavior(Mutation::externalTraceMask, false);
                                                            assertProjectionBehavior(Mutation::externalVisibility, true);
                                                            assertProjectionBehavior(Mutation::instanceAdded, true);
                                                            assertProjectionBehavior(Mutation::instanceRemoved, true);
                                                            assertProjectionBehavior(Mutation::externalMeshBinding, true);
                                                            assertProjectionBehavior(Mutation::externalMaterialPayload, true);
                                                            assertProjectionBehavior(Mutation::externalMaterialBinding, true);
                                                            assertProjectionBehavior(Mutation::externalMeshContent, true);
                                                            assertProjectionBehavior(Mutation::externalMeshLayout, true);
                                                            assertProjectionBehavior(Mutation::externalTextureBinding, true);
                                                            assertProjectionBehavior(Mutation::externalTextureContent, true);
                                                            assertProjectionBehavior(Mutation::externalTextureResidency, true);
                                                            assertProjectionBehavior(Mutation::meshResident, true);
                                                            assertProjectionBehavior(Mutation::materialResident, true);
                                                            assertProjectionBehavior(Mutation::textureResident, true);
                                                            assertProjectionBehavior(Mutation::externalEcsMutation, true);
                                                        }};

const nr::test::CaseRegistrar presentLinearExrScreenshotCase{"present screenshots read back linear source images and write EXR", [] {
                                                                 auto manifest = readProjectFile("vcpkg.json");
                                                                 auto externCMake = readProjectFile("src/extern/CMakeLists.txt");
                                                                 auto dependencyAssets = readProjectFile("src/extern/dependencyAssets.ixx");
                                                                 auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");
                                                                 auto rendererImplementation = readProjectFile("src/renderer/nrRendererPassBuilders.cpp");
                                                                 auto presentInterface = readProjectFile("src/renderPasses/Present/nrPresentNode.ixx");
                                                                 auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");

                                                                 auto parsedManifest = dependency::json::parseJson(manifest);
                                                                 nr::test::require(parsedManifest.valid(), "vcpkg manifest should be valid JSON");
                                                                 auto const *manifestObject = std::get_if<dependency::json::JsonValue::Object>(&parsedManifest.value->storage);
                                                                 nr::test::require(manifestObject != nullptr, "vcpkg manifest should be a JSON object");
                                                                 auto const dependencies = manifestObject->find("dependencies");
                                                                 nr::test::require(dependencies != manifestObject->end(), "vcpkg manifest should declare dependencies");
                                                                 auto const *dependencyArray = std::get_if<dependency::json::JsonValue::Array>(&dependencies->second.storage);
                                                                 nr::test::require(dependencyArray != nullptr, "vcpkg dependencies should be a JSON array");
                                                                 auto const hasOpenExr = std::ranges::any_of(*dependencyArray, [](const dependency::json::JsonValue &entry) {
                                                                     auto const *name = std::get_if<std::string>(&entry.storage);
                                                                     return name != nullptr && *name == "openexr";
                                                                 });
                                                                 nr::test::require(hasOpenExr, "vcpkg manifest should install OpenEXR");
                                                                 requirePresent(externCMake, "find_package(OpenEXR CONFIG REQUIRED)", "dependency boundary should find OpenEXR");
                                                                 requirePresent(externCMake, "OpenEXR::OpenEXR", "dependency target should link OpenEXR");
                                                                 requirePresent(dependencyAssets, "namespace nr::dependency::openexr", "OpenEXR declarations should be exposed only through dependency.assets");
                                                                 requirePresent(rendererInterface, "describeImageResource(GraphResourceHandle resource)", "Present should be able to query source image metadata without owning graph internals");
                                                                 requirePresent(rendererImplementation, "describeGraphImageResource", "renderer should implement image resource metadata lookup");

                                                                 requireAbsent(present, "stbi_write_png", "Present screenshots should no longer write PNG files");
                                                                 requireAbsent(present, "Present.ConvertScreenshot", "Present screenshots should not run a shader conversion pass");
                                                                 requireAbsent(present, "kScreenshotFormat", "Present screenshots should not force a fixed RGBA8 format");
                                                                 requirePresent(present, "context.describeImageResource(sourceColor)", "Present screenshots should inspect the published source image format");
                                                                 requirePresent(present, "detail::addPresentReadbackCopyPass(\n                context,\n                sourceColor", "Present screenshots should copy frameResource::presentSourceColor directly");
                                                                 requirePresent(present, ".format = sourceDesc->format", "Pending screenshot save should remember the source format");
                                                                 requirePresent(presentInterface, "vk::Format format = vk::Format::eUndefined", "Pending screenshot save should carry the source format across the frame fence");
                                                                 requirePresent(present, "writeLinearScreenshotExr", "Present should save the readback payload through the EXR writer");
                                                                 requirePresent(present, "!screenshotPrepared_.has_value() &&\n        !screenshotPendingSave_.has_value()", "capture availability must remain conservatively busy while dispatch or continuation state exists");
                                                                 requirePresent(present, "frameParameters.frameEffectSink->get().claim(*this, readbackPass)", "capture must claim its exact image-to-readback copy pass");
                                                                 requirePresent(present, "if (!targetBatchSubmitted || screenshotPendingSave_.has_value())", "capture must not arm its continuation unless the target batch submitted");
                                                                 requireOrdered(present, "if (!targetBatchSubmitted || screenshotPendingSave_.has_value())", "screenshotPendingSave_ = detail::PresentScreenshotPendingSave", "capture continuation state must be created only after target submission validation");
                                                                 requirePresent(present, "screenshotPendingSave_->frameSlot != frameSlot", "capture harvest must wait for the owning RHI frame slot");
                                                                 requirePresent(present, "void PresentNode::flushContinuations()", "graph replacement and shutdown must expose a synchronous capture flush hook");
                                                                 requirePresent(present, ".phase = nr::options::OptionLogPhase::terminal", "capture harvest must emit its terminal machine record");
                                                                 requireAbsent(present, "screenshotRequestCount_", "Present must not retain a multi-request screenshot counter");
                                                             }};

const nr::test::CaseRegistrar pathTracingNodeAssemblyCase{"path tracing node resolves typed inputs and uses named RT assembly groups", [] {
                                                              auto pathTracingNode = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
                                                              auto rhiPipelineHeader = readProjectFile("src/rhi/nrPipeline.ixx");
                                                              auto rhiPipelineSource = readProjectFile("src/rhi/nrPipeline.cpp");

                                                              requirePresent(pathTracingNode, "PathTracingFrameInputs", "PathTracing should resolve its scene inputs through one typed bundle");
                                                              requirePresent(pathTracingNode, "clearUnavailableGuides", "PathTracing unavailable inputs should clear the complete guide set");
                                                              requirePresent(pathTracingNode, "kPathTracingGuideResourceCount", "PathTracing should define one fixed guide resource count");
                                                              requirePresent(pathTracingNode, "guideFrameSlots", "PathTracing guides should be isolated by resource frame slot");
                                                              requirePresent(pathTracingNode, "nr::maxFrameInFlight", "PathTracing should retain one guide set per frame in flight");
                                                              requirePresent(pathTracingNode, "publishPathTracingGuides", "PathTracing should publish its guide set through frame resources");
                                                              requirePresent(pathTracingNode, "PathTracing.ClearUnavailable.", "PathTracing fallback clears should preserve their reason in the debug name");
                                                              requirePresent(pathTracingNode, "makePathTracingProgramAssembly", "PathTracing should assemble RT stages and shader groups from one description");
                                                              requirePresent(pathTracingNode, "shaderGroupIndex", "PathTracing SBT records should resolve named RT shader groups");
                                                              requireAbsent(pathTracingNode, "2u + record.permutationIndex", "PathTracing SBT records must not depend on hard-coded RT shader group indices");
                                                              requirePresent(rhiPipelineHeader, "struct RayTracingPipelineStageSelection", "RHI should expose explicit RT stage selection records");
                                                              requirePresent(rhiPipelineHeader, "struct RayTracingProgramAssemblyDesc", "RHI should expose one RT program assembly description");
                                                              requirePresent(rhiPipelineHeader, "shaderGroupIndex", "RHI RT pipelines should expose named shader group lookup");
                                                              requirePresent(rhiPipelineHeader, "logicalEntryPointName", "RHI RT stage selections should carry logical names for shader group lookup");
                                                              requirePresent(rhiPipelineSource, "program.logicalEntryPointNames_.push_back(std::move(logicalEntryPointName));", "RHI RT shader program should store logical names for group lookup");
                                                              requirePresent(rhiPipelineSource, "stageInfo.pName = program.shaderEntryPointNames_.back().c_str();", "RHI Vulkan shader stages should use the actual name discovered from each single-entry program");
                                                          }};

const nr::test::CaseRegistrar rendererSubmissionTimelinesCase{"renderer submission batches use producer-owned per-queue timelines", [] {
                                                                   auto executor =
                                                                       readProjectFile("src/renderer/nrRenderGraphExecutor.cpp") +
                                                                       readProjectFile("src/renderer/nrRenderGraphExecutorResources.cpp");
                                                                  auto timeline = readProjectFile("src/renderer/nrRendererSubmission.ixx");
                                                                  auto waitStageBegin = executor.find("RenderGraphExecutor::submissionWaitStage");
                                                                  auto waitStageEnd = executor.find("RenderGraphExecutor::shaderWaitStageForQueue", waitStageBegin);
                                                                  nr::test::require(waitStageBegin != std::string::npos && waitStageEnd != std::string::npos, "executor should define a bounded submission wait-stage helper");
                                                                  auto waitStageFunction = executor.substr(waitStageBegin, waitStageEnd - waitStageBegin);

                                                                  requirePresent(waitStageFunction, "queue == QueueDomain::Graphics", "executor should select the graphics submission wait scope explicitly");
                                                                  requirePresent(waitStageFunction, "vk::PipelineStageFlagBits2::eAllCommands", "graphics submission waits should cover batch-head acquire and RT shader work");
                                                                  requireAbsent(waitStageFunction, "vk::PipelineStageFlagBits2::eColorAttachmentOutput", "graphics submission waits must not be limited to color attachment output");
                                                                  requirePresent(executor, "timelines->get().semaphore(previousSignalToken.queue)", "adjacent RDG batches should wait on the producer queue timeline semaphore");
                                                                  requirePresent(executor, "timelines->get().acquireSignalToken(planBatch.queue)", "each inter-batch signal should acquire a token from its queue timeline");
                                                                  requirePresent(executor, "eQueueFamilyOwnershipTransferUseAllStagesKHR", "explicit QFOT barriers should opt into maintenance8 stage semantics");
                                                                  requirePresent(executor, "dstStageMask = srcStageMask", "maintenance8 release barriers should keep both scopes at the producer stage");
                                                                  requirePresent(executor, "srcStageMask = dstStageMask", "maintenance8 acquire barriers should keep both scopes at the consumer stage");
                                                                  requirePresent(executor, "applyQueueFamilyTransferPolicy(compiled, context.device);", "executor preparation should specialize ownership transitions from the runtime device policy");
                                                                  requirePresent(executor, "policy.canOmitBufferQueueFamilyTransfer", "renderer buffers and acceleration structures should consult maintenance9 before explicit QFOT");
                                                                  requirePresent(executor, "policy.canOmitImageQueueFamilyTransfer", "renderer images should consult maintenance9 before explicit QFOT");
                                                                  requirePresent(executor, "transition.strength = needsLayoutTransition", "omitted image QFOT should retain any required consumer-side layout transition");
                                                                  requirePresent(executor, "remainingOwnershipTransitions", "prepared ownership diagnostics should exclude omitted QFOTs");
                                                                  requirePresent(executor, "signalTokenByBatch.insert_or_assign", "normal graph submits should retain their queue timeline token for cross-frame resources");
                                                                  requirePresent(executor, "initialReleaseBatches", "non-omittable retained initial ownership changes should preserve the source-queue release fallback");
                                                                  requirePresent(timeline, "std::array<QueueTimeline, timelineCount> timelines_", "renderer should retain one timeline state per queue domain");
                                                                  requirePresent(timeline, ".queue = queue", "renderer submission tokens should identify their producer queue");
                                                                  requirePresent(timeline, "++timeline.nextSignalValue", "each queue timeline value should remain strictly increasing across frames");
                                                              }};

const nr::test::CaseRegistrar pathTracingShaderOrganizationCase{"path tracing shader keeps raygen core separate from material hit shaders", [] {
                                                                    auto raygenEntry = readProjectFile("shader/renderer/pathTracing/raygen.slang");
                                                                    auto missEntry = readProjectFile("shader/renderer/pathTracing/miss.slang");
                                                                    auto anyHitEntry = readProjectFile("shader/renderer/pathTracing/anyHit.slang");
                                                                    auto closestHitEntry = readProjectFile("shader/renderer/pathTracing/closestHit.slang");
                                                                    auto entryPoints = raygenEntry + missEntry + anyHitEntry + closestHitEntry;
                                                                    auto core = readProjectFile("shader/renderer/pathTracing/core.slang");
                                                                    auto guides = readProjectFile("shader/renderer/pathTracing/guides.slang");
                                                                    auto params = readProjectFile("shader/renderer/pathTracing/params.slang");
                                                                    auto pathState = readProjectFile("shader/renderer/pathTracing/pathState.slang");
                                                                    auto resources = readProjectFile("shader/renderer/pathTracing/resources.slang");
                                                                    auto environment = readProjectFile("shader/renderer/pathTracing/environment.slang");
                                                                    auto random = readProjectFile("shader/include/pathTracing/random.slang");
                                                                    auto scheduler = readProjectFile("shader/renderer/pathTracing/scheduler.slang");
                                                                    auto visibility = readProjectFile("shader/renderer/pathTracing/visibility.slang");
                                                                    auto hitShaders = readProjectFile("shader/renderer/pathTracing/hitShaders.slang");
                                                                    auto materialBsdf = readProjectFile("shader/include/material/bsdf.slang");
                                                                    auto materialPayload = readProjectFile("shader/include/material/payload.slang");
                                                                    auto materialSampling = readProjectFile("shader/include/material/sampling.slang");
                                                                    auto stochasticTextureFiltering = readProjectFile("shader/include/material/stochasticTextureFiltering.slang");
                                                                    auto hitSurface = readProjectFile("shader/include/rt/hitSurface.slang");
                                                                    auto roulette = readProjectFile("shader/include/pathTracing/roulette.slang");
                                                                    auto chs = readProjectFile("shader/include/pathTracing/chs.slang");
                                                                    auto pathTracingNode = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
                                                                    auto rhiPipelineHeader = readProjectFile("src/rhi/nrPipeline.ixx");
                                                                    auto rhiPipelineSource = readProjectFile("src/rhi/nrPipeline.cpp");

                                                                    requirePresent(raygenEntry, "Scheduler scheduler;", "PathTracing raygen entry should construct the scheduler");
                                                                    requirePresent(raygenEntry, "scheduler.traceSample(pixel, dimensions);", "PathTracing raygen entry should delegate work to the scheduler");
                                                                    requirePresent(closestHitEntry, "CHS chs = CHS();", "PathTracing closest-hit entry should construct its entry-local CHS type");
                                                                    requirePresent(closestHitEntry, "chs.handleClosestHit", "PathTracing closest-hit entry should delegate to CHS");
                                                                    requirePresent(anyHitEntry, "ahMaterialPolicy", "PathTracing any-hit entry should expose the shared material-policy ABI");
                                                                    requireAbsent(anyHitEntry, "ahMain", "PathTracing should not keep the old universal any-hit entry name");
                                                                    requireAbsent(entryPoints, "evaluateSceneLightAt", "PathTracing entries should not own lighting logic");
                                                                    requireAbsent(entryPoints, "resolveRtMaterialPayload", "PathTracing entries should not own material payload decoding");
                                                                    requirePresent(missEntry, "payload.missRadiance = sampleEnvironment", "material-ray miss should reuse the resolved position slot for environment radiance");
                                                                    requirePresent(missEntry, "payload.kind == RayKind.material", "visibility-ray misses should skip environment sampling");

                                                                    requirePresent(scheduler, "public struct Scheduler", "PathTracing scheduler should be a shader-side struct");
                                                                    requirePresent(scheduler, "Pt pt = makePt(pixel, dimensions);", "PathTracing scheduler should construct the PT path object");
                                                                    requirePresent(scheduler, "while (pt.isActive())", "PathTracing scheduler should own the raygen path loop");
                                                                    requirePresent(scheduler, "pt.traceMaterialRay(payload);", "PathTracing scheduler should ask Pt to issue material rays");
                                                                    requirePresent(scheduler, "pt.handleTraceResult(payload);", "PathTracing scheduler should ask Pt to handle hit or miss results");
                                                                    requirePresent(scheduler, "pt.writeOutput();", "PathTracing scheduler should run exactly one camera sample per pixel");
                                                                    requireAbsent(scheduler, "kSamplesPerPixel", "PathTracing scheduler must not expose a samples-per-pixel loop");
                                                                    requireAbsent(scheduler, "for (uint sampleIndex", "PathTracing scheduler must stay fixed at one camera sample per pixel");
                                                                    requirePresent(core, "public struct Pt", "PathTracing core should define the PT path object");
                                                                    requirePresent(core, "public PathState path", "Pt should hold the per-path state");
                                                                    requirePresent(core, "public bool isActive()", "Pt should expose active-state testing");
                                                                    requirePresent(core, "public void traceMaterialRay", "Pt should own material ray scheduling");
                                                                    requirePresent(core, "HitObject hitObject = HitObject::TraceRay(", "material rays should separate traversal from hit/miss shader invocation");
                                                                    requireAbsent(core, "RayPayload traversalPayload", "material rays should reuse the output payload instead of carrying a second payload across SER");
                                                                    requirePresent(core, "materialRayCoherenceHint(path.vertexIndex)", "material rays should compute a path-specific SER coherence hint");
                                                                    requirePresent(core, "kMaterialRayCoherenceHintBitCount = 3u", "material-ray SER should use the three architecturally meaningful hint bits");
                                                                    requirePresent(core, "HitObject::Invoke(scene, hitObject, payload);", "material rays should invoke their reordered hit or miss shader");
                                                                    requireAbsent(core, "\n        TraceRay(", "material rays must not retain the synchronous TraceRay path");
                                                                    requirePresent(core, "RAY_FLAG_CULL_BACK_FACING_TRIANGLES,\n            0xFF,\n            0,\n            1,\n            0,", "material rays should use hit SBT offset/stride 0/1");
                                                                    requirePresent(materialPayload, "payload.layers.transmissionIor == 0.0f", "IOR zero compatibility mode should force the final interface Fresnel to one");
                                                                    requirePresent(hitShaders, "shouldIgnoreSingleSidedBackFace", "PathTracing any-hit should restore per-material back-face policy for mixed BLAS instances");
                                                                    requirePresent(hitSurface, "rtObjectToWorldHandedness()", "RT tangent frames should account for mirrored instance transforms independently from object-space facing");
                                                                    requirePresent(core, "public void handleTraceResult", "Pt should own hit or miss dispatch");
                                                                    requirePresent(core, "public void handleHit", "Pt should expose hit handling");
                                                                    requirePresent(core, "public void handleMiss", "Pt should expose miss handling");
                                                                    requirePresent(core, "handleMiss(inout PathState path, float3 missRadiance)", "PathTracing miss handling should consume the miss shader's environment radiance");
                                                                    requirePresent(core, "sampleEnvironment(path.direction) * (1.0f - hitAlpha)", "primary alpha blend should use the same directional environment background");
                                                                    requirePresent(core, "public void writeOutput", "Pt should expose output writing");
                                                                    requirePresent(core, "makePt", "PathTracing core should provide Pt construction");
                                                                    requirePresent(core, "handleHit", "PathTracing core should own hit shading");
                                                                    requirePresent(core, "sampleDirectLighting", "PathTracing core should own direct lighting");
                                                                    requirePresent(core, "writeOutput", "PathTracing core should own output writes");
                                                                    requirePresent(core, "makeErrorDiffusionSequence(pixel, gFrame.frameState.xy)", "PathTracing core should seed error-diffusion sampling from the complete 64-bit sample-frame ordinal");
                                                                    requireAbsent(core, "makeErrorDiffusionSequence(pixel, gFrame.frameState.x)", "PathTracing core must not truncate the sample-frame ordinal to its low lane");
                                                                    requireAbsent(params, "kSamplesPerPixel", "PathTracing params must not expose configurable camera samples per pixel");
                                                                    requirePresent(params, "public extern static const uint kMaxSurfaceBounces;", "PathTracing max bounce variant must be provided by C++ VariantDesc");
                                                                    requireAbsent(params, "kMaxSurfaceBounces =", "PathTracing max bounce variant must not have a shader-side default");
                                                                    requirePresent(stochasticTextureFiltering, "public extern static const bool kEnableFilterAfterShading;", "The common material filtering include must expose the FAS root link-time constant to linked CHS programs");
                                                                    requireAbsent(stochasticTextureFiltering, "kEnableFilterAfterShading =", "The closest-hit FAS variant must not have a shader-side default");
                                                                    requireAbsent(params, "kEnableFilterAfterShading", "The FAS constant should have one common-visible declaration rather than a PathTracing-local duplicate");
                                                                    requireAbsent(params, "kMissRadiance", "PathTracing should not retain a constant miss radiance after environment integration");
                                                                    requirePresent(environment, "Sampler2D<float4> gEnvironmentMap", "environment should use a dedicated combined sampler");
                                                                    requirePresent(environment, "ConstantBuffer<EnvironmentMapParameters> gEnvironment", "environment decode controls should use push constants");
                                                                    requirePresent(environment, "SampleLevel(uv, 0.0f)", "mipless environment sampling should explicitly use level zero");
                                                                    requirePresent(environment, "gEnvironment.radianceDecodeScale", "environment sampling should restore scaled source radiance");
                                                                    requirePresent(environment, "gEnvironment.intensity", "environment sampling should apply independent user intensity");
                                                                    requireAbsent(visibility, "sampleEnvironment", "visibility rays must not evaluate environment radiance");
                                                                    requirePresent(visibility, "TraceRay(", "visibility/shadow rays should retain direct TraceRay traversal");
                                                                    requireAbsent(visibility, "HitObject::TraceRay", "visibility/shadow rays should remain outside the SER path");
                                                                    requireAbsent(visibility, "ReorderThread", "visibility/shadow rays should not pay an SER reorder");
                                                                    requirePresent(roulette, "public extern struct RussianRoulettePolicy : IRussianRoulettePolicy;", "PathTracing roulette policy variant must be provided by C++ VariantDesc");
                                                                    requireAbsent(roulette, "RussianRoulettePolicy : IRussianRoulettePolicy =", "PathTracing roulette policy variant must not rely on a shader-side default");
                                                                     requirePresent(random, "uint2 sampleFrameOrdinal", "PathTracing random sequence should receive both lanes of the 64-bit sample-frame ordinal");
                                                                     requirePresent(random, "int tileBits = 11", "PathTracing random sequence should default to Hilbert11 with 22 spatial bits and 10 in-era frame bits");
                                                                     requireAbsent(random, "int tileBits = 8", "PathTracing random sequence must not retain the 256x256 Hilbert8 default");
                                                                     requirePresent(random, "sampleFrameOrdinal.x & frameInEraMask", "PathTracing random sequence should preserve the low ordinal bits in its Sobol index");
                                                                     requirePresent(random, "sampleFrameOrdinal.y << spatialBitCount", "PathTracing random sequence should fold the high ordinal lane into the frame era");
                                                                     requirePresent(random, "sampleFrameOrdinal.y >> frameBitCount", "PathTracing random sequence should consume all remaining high-lane era bits");
                                                                     requirePresent(random, "seq.sampleSeed = strongIntegerHash(frameEra.x ^ strongIntegerHash(frameEra.y));", "PathTracing random sequence should derive a frame-shared scramble seed from the complete era while preserving hash zero");
                                                                     requirePresent(random, "public float4 rand4()", "PathTracing random sequence should expose one complete four-lane packet draw");
                                                                     requireAbsent(random, "IRandomSequence", "PathTracing random sequence should not retain a generic variable-width sampling interface");
                                                                     requireAbsent(random, "get1D", "PathTracing random sequence should not expose scalar packet draws");
                                                                     requireAbsent(random, "get2D", "PathTracing random sequence should not expose two-lane packet draws");
                                                                     requireAbsent(random, "get3D", "PathTracing random sequence should not expose three-lane packet draws");
                                                                     requireAbsent(random, "get4D", "PathTracing random sequence should use the single rand4 packet API");
                                                                     requireAbsent(random, "getBits", "PathTracing random sequence should not retain a hidden variable-width bit draw");
                                                                     requireAbsent(random, "split(", "PathTracing random sequence should not retain a secondary packet draw API");
                                                                     requireAbsent(random, "random01FromHash", "PathTracing random sequence should not retain the legacy white-noise light-sample hash");
                                                                     requirePresent(core, "float4 lightRandomValues = path.rng.rand4();", "PathTracing direct lighting should draw its first pair of low-discrepancy sample pairs from one four-lane packet");
                                                                     requirePresent(core, "\n    lightRandomValues = path.rng.rand4();", "PathTracing direct lighting should draw its second pair of low-discrepancy sample pairs from one four-lane packet");
                                                                     requirePresent(core, "lightRandomValues.xy", "PathTracing direct lighting should consume the first pair from each four-lane packet");
                                                                     requirePresent(core, "lightRandomValues.zw", "PathTracing direct lighting should consume the second pair from each four-lane packet");
                                                                     requireAbsent(core, "makeAliasLightSample", "PathTracing direct lighting should not hash native low-discrepancy packet lanes into white noise");
                                                                     requirePresent(core, "path.rng.rand4().x", "PathTracing roulette should consume one lane from the only public random packet API");
                                                                     requirePresent(core, "float4 scatterRandomValues = path.rng.rand4();", "PathTracing scatter should consume one complete four-lane random packet");
                                                                     requirePresent(random, "public void advanceThreeRand4Packets()", "RandomSequence should expose the fixed three-packet material-filter skip");
                                                                     requirePresent(random, "sampleSeed = sampleSeed * 0x98a5741du + 0xacfbeaa7u;", "The three-packet reservation should use the validated closed-form fifteen-step LCG jump");
                                                                     requirePresent(core, "RandomSequence materialFilterSequence = path.rng;", "Each material segment should snapshot its FAS sequence immediately after the scatter packet");
                                                                     requirePresent(core, "path.rng.advanceThreeRand4Packets();", "PathTracing must reserve exactly three rand4 packets for every material segment");
                                                                     requireOrdered(
                                                                         core,
                                                                          "RandomSequence materialFilterSequence = path.rng;",
                                                                         "path.rng.advanceThreeRand4Packets();",
                                                                         "The material CHS must receive the pre-advance sequence while the path reserves the same dimensions unconditionally");
                                                                     requirePresent(materialPayload, "RandomSequence localFilterSequence = filterSequence;", "Material resolution must draw from a by-value sequence copy");
                                                                     requirePresent(materialPayload, "float4 filterPacket0 = localFilterSequence.rand4();", "Packet 0 should provide base color, metallic-roughness, emissive, and base-normal lanes");
                                                                     requirePresent(materialPayload, "filterPacket0.xyz", "Packet 0 XYZ should feed the three core material textures");
                                                                     requirePresent(materialPayload, "filterPacket0.w", "Packet 0 W should feed the base normal");
                                                                     requirePresent(materialPayload, "float4 filterPacket1 = localFilterSequence.rand4();", "Packet 1 should provide anisotropy and the three clearcoat lanes");
                                                                     requirePresent(materialPayload, "filterPacket1.x", "Packet 1 X should feed anisotropy");
                                                                     requirePresent(materialPayload, "filterPacket1.yzw", "Packet 1 YZW should feed clearcoat factor, roughness, and normal");
                                                                     requirePresent(materialSampling, "float4 layerFilterRandomValues = filterSequence.rand4();", "Packet 2 should be generated after packet 1 is dead");
                                                                     requirePresent(materialSampling, "MaterialTextureSlot.sheenColor", "Packet 2 X should have a sheen-color consumer");
                                                                     requirePresent(materialSampling, "MaterialTextureSlot.sheenRoughness", "Packet 2 Y should have a sheen-roughness consumer");
                                                                     requirePresent(materialSampling, "MaterialTextureSlot.transmission", "Packet 2 Z should have a transmission consumer");
                                                                     requirePresent(materialSampling, "layerFilterRandomValues.x", "Packet 2 X should feed sheen color");
                                                                     requirePresent(materialSampling, "layerFilterRandomValues.y", "Packet 2 Y should feed sheen roughness");
                                                                     requirePresent(materialSampling, "layerFilterRandomValues.z", "Packet 2 Z should feed transmission");
                                                                     requireAbsent(materialSampling, "layerFilterRandomValues.w", "Packet 2 W must remain the explicit twelfth padding lane");
                                                                     requireAbsent(materialSampling, "MaterialTextureSlot.occlusion", "The unsampled occlusion slot must not consume a FAS lane");
                                                                     requirePresent(params, "public static const uint kDirectLightSampleCount = 4u;", "PathTracing direct-light packet layout requires exactly four samples");
                                                                     requirePresent(pathState, "public RandomSequence rng = {};", "PathTracing path state should keep a per-pixel/per-frame random sequence");
                                                                     requireAbsent(
                                                                         sourceSection(
                                                                             pathState,
                                                                             "public struct PathState",
                                                                             "public void terminatePathState"),
                                                                         "sampleIndex",
                                                                         "PathTracing path state must not carry camera sample state in fixed 1spp mode");
                                                                    requirePresent(pathState, "public float3 specularThroughput", "PathTracing path state should track primary-specular throughput independently");
                                                                    requirePresent(pathState, "public float3 diffuseThroughput", "PathTracing path state should track primary-diffuse throughput independently");
                                                                    requirePresent(pathState, "public float3 specularRadiance", "PathTracing path state should accumulate primary-specular radiance independently");
                                                                    requirePresent(pathState, "public float3 diffuseRadiance", "PathTracing path state should accumulate primary-diffuse radiance independently");
                                                                    requirePresent(core, "pathCombinedThroughput(path) * path.etaScale", "PathTracing roulette should use eta-compensated combined split throughput");
                                                                    requirePresent(core, "initializePrimaryPathThroughput", "PathTracing should split primary continuation throughput by BSDF component");
                                                                    requirePresent(core, "pathCombinedRadiance(path)", "PathTracing should merge split radiance only at output");
                                                                    requirePresent(core, "writePathTracingGuides(path.pixel, path.guides)", "PathTracing should write all RR guides with the noisy color");
                                                                    requirePresent(pathState, "public PathTracingGuideState guides = {};", "PathTracing should carry guide state with its camera path");
                                                                    requirePresent(resources, "RWTexture2D<float4> outputImage", "RR noisy color should use RGBA float storage");
                                                                    requirePresent(resources, "RWTexture2D<float> depthImage", "RR depth should use one float channel");
                                                                    requirePresent(resources, "RWTexture2D<float4> diffuseAlbedoImage", "RR diffuse albedo should use linear float storage");
                                                                    requirePresent(resources, "RWTexture2D<float4> specularAlbedoImage", "RR specular albedo should use linear float storage");
                                                                    requirePresent(resources, "RWTexture2D<float4> normalRoughnessImage", "RR normal and roughness should share one packed texture");
                                                                    requirePresent(resources, "RWTexture2D<float2> motionVectorsImage", "RR dense motion vectors should use two float channels");
                                                                    requirePresent(resources, "RWTexture2D<float> specularHitDistanceImage", "RR specular hit distance should use one float channel");
                                                                    requireAbsent(resources, "disocclusion", "PathTracing must not generate a disocclusion guide");
                                                                    requireAbsent(resources, "motionVectors3D", "PathTracing must not generate experimental 3D motion vectors");
                                                                    requireAbsent(resources, "rayDirection", "PathTracing must not generate experimental ray-direction guides");
                                                                    requirePresent(guides, "gFrame.previousViewProjection", "RR motion vectors should use the jitter-decoupled previous transform");
                                                                    requirePresent(hitSurface, "float3x4 worldToObject = WorldToObject3x4()", "RT normal reconstruction should start from the inverse object transform");
                                                                    requirePresent(hitSurface, "transformRtObjectNormalToWorld(objectNormal)", "RT normals should use inverse-transpose transformation under non-uniform instance scaling");
                                                                    requireAbsent(hitSurface, "transformRtObjectVectorToWorld(objectNormal)", "RT normals must not use the direct object-to-world vector transform");
                                                                    requirePresent(guides, "guides.depth = saturate(clip.z", "RR hardware depth should remain in Vulkan clip depth range");
                                                                    requirePresent(guides, "guides.normalRoughness = float4(normal, saturate(material.roughness))", "RR should pack linear roughness in normal alpha");
                                                                    requirePresent(guides, "pathTracingGuideEnvBrdfApprox2", "RR specular albedo should use NVIDIA's view-dependent EnvBRDF approximation");
                                                                    requirePresent(guides, "length(hitPosition - guides.primaryPosition)", "RR specular hit distance should be measured in world space from the primary surface");
                                                                    requirePresent(materialPayload, "evaluateResolvedMaterialBsdfComponentsVariant", "Material payload should expose variant-aware BSDF component evaluation");
                                                                    requirePresent(materialPayload, "evaluateResolvedMaterialDirectComponentsVariant", "Material payload should expose variant-aware direct-light component evaluation");
                                                                    requireAbsent(core, "path.throughput", "PathTracing core must not keep the old monolithic throughput path");
                                                                    requireAbsent(core, "path.radiance", "PathTracing core must not keep the old monolithic radiance path");
                                                                    requirePresent(visibility, "RAY_FLAG_CULL_BACK_FACING_TRIANGLES", "visibility rays should share material-ray single-sided culling");
                                                                    requirePresent(visibility, "0xFF,\n        0,\n        1,\n        0,", "visibility rays should use the same hit SBT offset/stride 0/1");

                                                                    requirePresent(hitShaders, "resolveAlphaCoverage", "PathTracing any-hit should resolve only alpha coverage");
                                                                    requirePresent(hitShaders, "makeClosestHitInput", "PathTracing hit shaders should prepare CHS closest-hit inputs");
                                                                    requireAbsent(hitShaders, "handleClosestHitWithPolicy", "PathTracing should not keep wrapper-policy closest-hit contract");
                                                                    requireAbsent(chs, "RtHitAlphaPolicy", "PathTracing CHS variants should not specialize alpha policy");
                                                                    requireAbsent(hitShaders, "sampleDirectLighting", "PathTracing hit shaders must not own direct lighting");
                                                                    requireAbsent(hitShaders, "evaluateResolvedMaterialDirect", "PathTracing hit shaders must not shade direct light");
                                                                    requireAbsent(hitShaders, "outputImage", "PathTracing hit shaders must not write the output image");
                                                                    requirePresent(chs, "public interface ICHS", "PathTracing CHS contract should define the closest-hit interface");
                                                                    requireAbsent(anyHitEntry, "materialFilter", "PathTracing any-hit must not receive or consume FAS state");
                                                                    requireAbsent(hitShaders, "materialFilter", "Alpha coverage and hit reconstruction must remain independent from FAS");
                                                                     requirePresent(closestHitEntry, "input.materialFilterSequence = payload.materialFilterSequence;", "Closest hit should decode only its pre-reserved material-filter sequence before overwriting shared payload slots");
                                                                     requirePresent(pathState, "public property RandomSequence materialFilterSequence", "RayPayload should decode the transient material-filter sequence from shared output slots without adding it to PathState");
                                                                     requirePresent(pathState, "public struct ResolvedMaterialRayPayload", "PathTracing should use a dedicated packed ray-transport record instead of the BSDF working record");
                                                                     requirePresent(pathState, "public void initializeMaterialRayPayload(", "Material-ray invoke input should be encoded into shared result slots");
                                                                     requirePresent(pathState, "public void writeResolvedMaterialRayPayload(", "Closest hit should overwrite the shared invoke slots with the resolved hit result");
                                                                     requirePresent(pathState, "packSnorm2x16(encoded)", "Closest hit should encode ray-boundary directions with standard oct32 storage");
                                                                     requirePresent(pathState, "unpackSnorm2x16ToFloat(value)", "Raygen should decode oct32 directions back to full-precision working vectors");
                                                                     requirePresent(pathState, "packUnorm2x16(saturate(value))", "Closest hit should saturate and pair-pack bounded material scalars");
                                                                     requirePresent(pathState, "unpackUnorm2x16ToFloat(", "Raygen should decode bounded material scalar pairs into full-precision working values");
                                                                     requirePresent(pathState, "anisotropyTangent - shadingNormal * dot(shadingNormal, anisotropyTangent)", "Decoded anisotropy tangents should be reprojected onto the decoded shading-normal plane");
                                                                     requirePresent(core, "ResolvedMaterialPayload material = resolvedMaterialFromRayPayload(", "Raygen should decode packed material transport before integration updates PathState");
                                                                     requirePresent(core, "resolvedMaterialScatterFromRayPayload(payload)", "Raygen should decode packed scatter transport before integration updates PathState");
                                                                     requirePresent(core, "pathCurrentMediumIor(path)", "Raygen should restore the current medium IOR while decoding the compact hit payload");
                                                                     requirePresent(core, "pathExteriorMediumIor(path)", "Raygen should restore the exterior medium IOR while decoding the compact hit payload");
                                                                     auto const rayPayloadRecord = sourceSection(
                                                                         pathState,
                                                                         "public struct RayPayload",
                                                                         "// Invoke input and hit output have disjoint lifetimes");
                                                                     requirePresent(rayPayloadRecord, "public ResolvedMaterialRayPayload resolved = {};", "RayPayload should contain only the packed shared-lifetime transport record");
                                                                     requireAbsent(rayPayloadRecord, "public RandomSequence materialFilterSequence", "RayPayload must not retain a dedicated RNG storage field after lifetime reuse");
                                                                     requireAbsent(rayPayloadRecord, "ResolvedMaterialPayload material", "RayPayload must not embed the full BSDF working material");
                                                                     requireAbsent(rayPayloadRecord, "ResolvedMaterialScatter scatter", "RayPayload must not embed the full working scatter record");
                                                                     auto const resolvedMaterialRecord = sourceSection(
                                                                         materialPayload,
                                                                         "public struct ResolvedMaterialPayload",
                                                                         "public struct ResolvedMaterialScatter");
                                                                     requirePresent(materialPayload, "[Flags]\npublic enum ResolvedMaterialFlag : uint", "Resolved material bool, enum, and flag state should use a strong uint-backed flag enum");
                                                                     requirePresent(resolvedMaterialRecord, "public ResolvedMaterialFlag flags", "Resolved material metadata should use the shared strong flag enum");
                                                                     requirePresent(resolvedMaterialRecord, "public property RtMaterialLayerFlag layerFlags", "Resolved material layer flags should use a typed property over packed metadata");
                                                                     requirePresent(resolvedMaterialRecord, "public property AlphaMode alphaMode", "Resolved material alpha mode should use a typed property over packed metadata");
                                                                     requirePresent(resolvedMaterialRecord, "public property bool frontFace", "Resolved material front-face state should use a property over packed metadata");
                                                                     requireAbsent(resolvedMaterialRecord, "featureFlags", "Resolved material should not copy header feature flags that have no downstream consumer");
                                                                     requireAbsent(resolvedMaterialRecord, "alphaCutoff", "Resolved material should not copy the any-hit-only alpha cutoff");
                                                                     requireAbsent(resolvedMaterialRecord, "hitT", "Resolved material should not retain unused hit distance beside full-precision position");
                                                                     requireAbsent(resolvedMaterialRecord, "public float alpha", "Resolved material alpha should derive from baseColor.a");
                                                                     auto const persistentPathState = sourceSection(
                                                                         pathState,
                                                                         "public struct PathState",
                                                                         "public void terminatePathState");
                                                                    requireAbsent(persistentPathState, "materialFilterSequence", "FAS must not add persistent RNG state to PathState");
                                                                    requirePresent(materialPayload, "RandomSequence filterSequence", "Material payload resolution should receive the pre-reserved sequence by value rather than draw from the live path RNG");
                                                                    requirePresent(materialSampling, "public float4 sampleMaterialTextureVariant(", "RT material sampling should centralize the root-controlled FAS A/B policy");
                                                                    requirePresent(materialSampling, "if (kEnableFilterAfterShading)", "Only the enabled closest-hit variant should stochastically select a bilinear reconstruction tap");
                                                                    requirePresent(materialSampling, "gSceneTextures[textureRef.textureId].GetDimensions(width, height);", "LOD0 FAS should derive the texel grid from the sampled texture");
                                                                    requirePresent(materialSampling, "stochasticBilinearTexelCenterUv(", "Enabled FAS should select one bilinear tap and convert it to a texel-center UV");
                                                                    requirePresent(materialSampling, "gSceneTextures[textureRef.textureId].SampleLevel(uv, 0.0f);", "Both FAS states should fetch exactly one nearest texel at mip zero");
                                                                    requirePresent(stochasticTextureFiltering, "selectStochasticFilterUpperTap(", "FAS should use scalar remapping for bilinear tap selection");
                                                                    requirePresent(stochasticTextureFiltering, "uniformValue = selectUpper", "The X decision should remap the scalar before it is reused for Y");
                                                                    requireAbsent(materialSampling, "ddx(", "RT material sampling should not use derivative footprints");
                                                                    requireAbsent(materialSampling, "ddy(", "RT material sampling should not use derivative footprints");
                                                                    requireAbsent(materialSampling, "RayCone", "First-stage FAS should not introduce ray cones");
                                                                    requireAbsent(materialSampling, "rayCone", "First-stage FAS should not introduce ray cones");
                                                                    requirePresent(chs, "let LayerFlags : RtMaterialLayerFlag", "PathTracing CHS contract should expose one combined material-flag specialization target");
                                                                    requireAbsent(chs, "let EnableFilterAfterShading", "PathTracing CHS must retain only the material-layer generic dimension");
                                                                    requirePresent(materialSampling, "kEnableFilterAfterShading", "Material sampling should consume the common root link-time FAS constant");
                                                                    requireAbsent(chs, "RtBaseLobeVariant", "PathTracing CHS contract should not retain a separate base-lobe specialization type");
                                                                    requirePresent(chs, "public extern struct CHS : ICHS;", "PathTracing CHS contract should require C++ link-time type binding");
                                                                    requirePresent(chs, "resolveLitMaterialPayloadVariant", "MaterialCHS should resolve lit material payloads through the layer-flag variant");
                                                                    requirePresent(pathTracingNode, "makePathTracingRaygenVariantDesc", "PathTracing should isolate bounce and roulette assignments to raygen requests");
                                                                    requirePresent(pathTracingNode, "makePathTracingClosestHitVariantDesc", "PathTracing should combine FAS and material CHS assignments only for closest-hit requests");
                                                                    requirePresent(pathTracingNode, "\"MaterialCHS<RtMaterialLayerFlag({}u)>\"", "CHS variants should remain keyed only by the material layer flags");
                                                                    requireAbsent(pathTracingNode, "makePathTracingSyntheticRootSource", "PathTracing node should no longer generate synthetic closest-hit wrappers");
                                                                    requireAbsent(pathTracingNode, "RtHitPolicy_", "PathTracing node should no longer generate shader-side policy structs");
                                                                    requireAbsent(pathTracingNode, ".linkVariants", "Single-entry compile requests must not retain the old secondary link-variant list");
                                                                    requirePresent(pathTracingNode, "compileProgramsByFile", "PathTracing should submit all required single-entry shaders through one batch compiler call");
                                                                    requirePresent(pathTracingNode, "renderer/pathTracing/raygen", "PathTracing should compile its raygen entry from its own file");
                                                                    requirePresent(pathTracingNode, "renderer/pathTracing/miss", "PathTracing should compile its non-variant miss entry from its own file");
                                                                    requirePresent(pathTracingNode, "renderer/pathTracing/anyHit", "PathTracing should compile its non-variant any-hit entry from its own file when required");
                                                                    requirePresent(pathTracingNode, "renderer/pathTracing/closestHit", "PathTracing should compile each closest-hit variant from its own file");
                                                                    requirePresent(pathTracingNode, "permutation.key.bsdf", "PathTracing node should derive CHS variants from BSDF keys, not full hit-group keys");
                                                                    requirePresent(pathTracingNode, "createRayTracingPipeline(\n        programs.raygen,", "PathTracing should use raygen as the explicit canonical reflection program");
                                                                    requirePresent(pathTracingNode, ".sampledImage(\"gEnvironmentMap\"", "PathTracing should bind the renderer-global environment through reflection");
                                                                    requirePresent(pathTracingNode, ".pushConstants(\"gEnvironment\"", "PathTracing should bind environment parameters through reflection");
                                                                    requirePresent(pathTracingNode, "addressModeU = vk::SamplerAddressMode::eRepeat", "lat-long environment longitude should repeat");
                                                                    requirePresent(pathTracingNode, "addressModeV = vk::SamplerAddressMode::eClampToEdge", "lat-long environment latitude should clamp at poles");
                                                                    auto const missingInputs = pathTracingNode.find("if (!frameInputs.has_value())");
                                                                    auto const environmentBinding = pathTracingNode.find("auto const environmentMap", missingInputs);
                                                                    nr::test::require(missingInputs != std::string::npos && environmentBinding != std::string::npos && missingInputs < environmentBinding, "missing TLAS/sideband should retain the clear path before environment binding");
                                                                    requirePresent(materialPayload, "ResolvedMaterialPayload", "Common material payload helper should define resolved hit material data");
                                                                    requirePresent(materialPayload, "public struct BaseSurfaceBsdfLobe<", "Common material payload helper should expose the shared base/transmission lobe");
                                                                    requirePresent(materialPayload, "public struct BaseGgxDistribution<let LayerFlags", "Common material payload helper should derive its GGX distribution from combined material flags");
                                                                    requirePresent(materialPayload, "alphaT = lerp(isotropicAlpha, 1.0f, strength * strength)", "Anisotropic GGX should use the approved alphaT mapping");
                                                                    requirePresent(materialPayload, "visibleHalfVectorPdf", "Anisotropic evaluate, PDF and VNDF sampling should share the base GGX helper");
                                                                    requirePresent(materialBsdf, "GgxSpecularEnergyTerms", "GGX shading should expose Spec.W compensation and Spec.E directional albedo");
                                                                    requirePresent(materialBsdf, "ggxDirectionalAlbedoAnalytic", "GGX energy compensation should use the resource-free UE analytic lookup");
                                                                    requirePresent(materialBsdf, "noL * lenV + noV * lenL", "Isotropic GGX should use joint correlated Smith masking-shadowing");
                                                                    requirePresent(materialPayload, "energyWeight * fresnel * distribution * geometry", "Base GGX reflection should apply Spec.W to the single-scattering lobe");
                                                                    requirePresent(materialPayload, "reflectionImportance = energy.E", "Base lobe selection should use Spec.E directional albedo");
                                                                    requirePresent(materialPayload, "if (!hasActiveTransmission(payload))", "Opaque Spec.W/Spec.E should not be reused for active glass energy compensation");
                                                                    requirePresent(materialPayload, "clearcoatBaseAttenuation", "Clearcoat Spec.E should attenuate lower layers");
                                                                    requirePresent(materialPayload, "adjustMaterialPayloadSpecularNormal", "Specular lobes should derive a view-dependent normal without replacing the raw shading normal");
                                                                    requirePresent(materialPayload, "MaterialPayloadReflectionEvaluation", "Folded reflection should carry both unprojected and projected evaluation kernels");
                                                                    requirePresent(materialPayload, "materialPayloadMirrorReflectionDirection", "Reflection folding should expose its geometry-plane mirror isometry");
                                                                    requirePresent(materialPayload, "materialPayloadFoldReflectionDirection", "Base, sheen, and clearcoat samples should fold into the exterior geometry hemisphere");
                                                                    requirePresent(materialPayload, "evaluateFoldedReflection", "Reflection evaluation should sum the exterior direction and mirrored preimage");
                                                                    requirePresent(materialPayload, "foldedReflectionPdf", "Reflection PDFs should use the same two-preimage push-forward as evaluation");
                                                                    requirePresent(materialPayload, "materialPayloadGeometrySupportsReflection", "Folded reflection queries should retain exterior-only geometry support");
                                                                    requirePresent(materialPayload, "materialPayloadGeometrySupportsTransmission", "transmission should use the complementary view-facing geometric hemisphere contract");
                                                                    requirePresent(materialPayload, "bsdf.diffuseProjected / pdf", "continuation throughput should use the diffuse lobe's own projected contribution");
                                                                    requirePresent(materialPayload, "bsdf.specularProjected / pdf", "continuation throughput should use the specular lobe's own projected contribution");
                                                                    requirePresent(hitSurface, "surface.tangent = -surface.tangent;", "double-sided orientation should reverse tangent with normal and tangent sign");
                                                                    requireAbsent(materialPayload, "TransmissionBsdfLobe", "Transmission must not remain an independent top-level lobe");
                                                                     requirePresent(materialPayload, "scatterDelta", "Common material scatter should carry the delta-lobe flag in packed metadata");
                                                                     requirePresent(materialPayload, "if (scatter.delta)", "Common material scatter sampling should keep delta lobes out of continuous PDF mixing");
                                                                    requirePresent(materialPayload, "sampleResolvedMaterialScatterVariant", "Common material payload helper should expose variant-aware scatter sampling");
                                                                    requirePresent(materialPayload, "resolvedMaterialCombinedPdfVariant", "Common material payload helper should expose variant-aware combined PDFs");
                                                                    requirePresent(materialPayload, "evaluateResolvedMaterialDirect", "Common material payload helper should expose direct lighting evaluation");
                                                                    requirePresent(materialPayload, "sampleResolvedMaterialScatter", "Common material payload helper should expose v1 scatter sampling");
                                                                }};

const nr::test::CaseRegistrar accumulateShaderCase{"accumulate shader owns capped current-frame weight", [] {
                                                       auto shader = readProjectFile("shader/renderer/accumulate.slang");
                                                       auto node = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");

                                                       requirePresent(shader, "Texture2D<float4> gCurrentColor", "Accumulate shader should read current frame color");
                                                       requirePresent(shader, "Texture2D<float4> gHistoryColor", "Accumulate shader should read previous history color");
                                                       requirePresent(shader, "RWTexture2D<float4> gAccumulatedColor", "Accumulate shader should write history output");
                                                       requirePresent(shader, "max(exactWeight, cappedWeight)", "Accumulate shader should clamp current-frame weight");
                                                       requirePresent(node, "std::array<nr::renderer::RetainedImageState, 2u> historyStates{}", "Accumulate history slots should use retained image state tracking");
                                                       requirePresent(node, ".retainedState = std::ref(state)", "Accumulate history imports should attach retained state to the graph");
                                                       requireAbsent(node, "ImageLayoutIntent::ShaderReadOnly", "Accumulate should not hard-code previous-slot layout outside retained state");
                                                   }};

const nr::test::CaseRegistrar retainedImportedImageStateCase{"renderpasses use retained state for renderer-persistent imported images", [] {
                                                                 auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");
                                                                 auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
                                                                 auto pathTracing = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
                                                                 auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
                                                                 auto rendererInterface = readProjectFile("src/renderer/nrRenderer.ixx");

                                                                 requirePresent(rendererInterface, "importRetainedStorageColor", "NodeBuildContext should expose a retained imported storage-color helper");
                                                                 requirePresent(present, "RetainedImageState convertedColorState", "Present should retain converted-color image state across frames");
                                                                 requirePresent(present, "convertedColorState.reset()", "Present should reset converted-color state when the image is recreated");
                                                                 requirePresent(present, "context.importRetainedStorageColor", "Present.ConvertedColor should use retained import tracking");
                                                                 requirePresent(accumulate, "std::array<nr::renderer::RetainedImageState, 2u> historyStates{}", "Accumulate history ping-pong images should each have retained state");
                                                                 requirePresent(accumulate, "runtime.historyStates[slot].reset()", "Accumulate should reset retained history state on image recreation");
                                                                 requirePresent(pathTracing, "std::array<PathTracingGuideFrameSlot, nr::maxFrameInFlight> guideFrameSlots{}", "PathTracing should own one complete RR guide set per frame in flight");
                                                                 requirePresent(pathTracing, ".retainedState = std::ref(state)", "PathTracing persistent guides should attach retained layout and ownership state");
                                                                 requirePresent(pathTracing, "frameSlot.states[guideResourceIndex].reset()", "PathTracing should reset retained guide state when images are recreated");
                                                                 requirePresent(ui, "nr::renderer::RetainedImageState state{}", "Ui texture entries should use the shared retained image state object");
                                                                 requirePresent(ui, "textureEntry.state.layout = nr::renderer::ImageLayoutIntent::ShaderReadOnly", "Ui upload completion should seed texture retained layout");
                                                                 requireAbsent(ui, "currentLayout", "Ui should not keep an ad-hoc currentLayout field");
                                                             }};

const nr::test::CaseRegistrar skeletonPatchCapabilityCase{"all rtobject nodes expose exact patch-only Skeleton materialization", [] {
                                                               auto light = readProjectFile("src/renderPasses/LightPrepare/nrLightPrepareNode.cpp");
                                                               auto accumulate = readProjectFile("src/renderPasses/Accumulate/nrAccumulateNode.cpp");
                                                               auto present = readProjectFile("src/renderPasses/Present/nrPresentNode.cpp");
                                                               auto path = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.cpp");
                                                               auto dlss = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.cpp");
                                                               auto ui = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
                                                               auto asInterface = readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.ixx");
                                                               auto asSource = readProjectFile("src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp");
                                                               auto renderer = readProjectFile("src/renderer/nrRenderer.cpp");
                                                               auto pathInterface = readProjectFile("src/renderPasses/PathTracing/nrPathTracingNode.ixx");
                                                               auto dlssInterface = readProjectFile("src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx");
                                                               auto uiInterface = readProjectFile("src/renderPasses/Ui/nrUiNode.ixx");
                                                               auto lightHit = sourceSection(light, "bool LightPrepareNode::materializeRenderGraphSkeleton(", "void LightPrepareNode::materializeCurrentFrame(");
                                                               auto accumulateHit = sourceSection(accumulate, "bool AccumulateNode::materializeRenderGraphSkeleton(", "void AccumulateNode::materializeCurrentFrame(");
                                                               auto presentHit = sourceSection(present, "bool PresentNode::materializeRenderGraphSkeleton(", "void PresentNode::materializeCurrentFrame(");
                                                               auto pathHit = sourceSection(path, "bool PathTracingNode::materializeRenderGraphSkeleton(", "void PathTracingNode::materializeCurrentFrame(");
                                                               auto dlssHit = sourceSection(dlss, "bool DlssRayReconstructionNode::materializeRenderGraphSkeleton(", "void DlssRayReconstructionNode::materializeCurrentFrame(");
                                                               auto uiHit = sourceSection(ui, "bool UiNode::materializeRenderGraphSkeleton(", "void UiNode::materializeCurrentFrame(");
                                                               auto asHit = sourceSection(asSource, "bool AccelerationStructureBuildNode::materializeRenderGraphSkeleton(", "void AccelerationStructureBuildNode::materializeCurrentFrame(");
                                                               auto presentCold = sourceSection(present, "void PresentNode::materializeCurrentFrame(", "void PresentNode::advanceContinuations(");
                                                               auto presentAdvance = sourceSection(present, "void PresentNode::advanceContinuations(", "void PresentNode::flushContinuations(");
                                                               auto asCold = sourceSection(asSource, "void AccelerationStructureBuildNode::materializeCurrentFrame(", "} // namespace nr::renderPasses");

                                                               requirePresent(light, "nr::renderer::RenderGraphSkeletonPatchContext& context", "LightPrepare hit path must use patch-only context");
                                                               requirePresent(light, "context.patchFrameData", "LightPrepare hit path must patch current upload payload");
                                                               requirePresent(accumulate, "nr::renderer::ComputePassPatchBuilder", "Accumulate hit path must patch compute callbacks and bindings");
                                                               requirePresent(accumulate, "context.patchResource", "Accumulate hit path must patch ping-pong imports");
                                                               requirePresent(present, "nr::renderer::ComputePassPatchBuilder", "Present hit path must patch compute callbacks and bindings");
                                                               requirePresent(present, "patchCopyImageToBuffer", "Present hit path must patch readback copies");
                                                               requirePresent(present, "patchCopyImageToImage", "Present hit path must patch the swapchain copy");
                                                               requirePresent(pathHit, "RayTracingPassPatchBuilder", "PathTracing hit path must patch ray tracing bindings and callbacks");
                                                               requirePresent(dlssHit, "context.patchPass", "DLSS hit path must patch current evaluation callbacks");
                                                               requirePresent(uiHit, "RasterPassPatchBuilder", "UI hit path must patch raster state and draw callbacks");
                                                               requirePresent(ui, "runtime_->preparedDrawFrame = detail::prepareUiDrawFrame", "UI structural snapshot must prepare draw data and synchronize textures before keying");
                                                               requirePresent(uiHit, "runtime_->preparedDrawFrame", "UI hit path must consume the structural preflight draw frame");
                                                               requireAbsent(uiHit, "finalizeFrame()", "UI hit path must not finalize a second frame after structural preflight");
                                                               requireAbsent(uiHit, "synchronizeUiTextures(", "UI hit path must not mutate texture topology after structural key lookup");
                                                               requirePresent(asHit, "context.patchFrameData", "AS hit path must patch BLAS/TLAS frame data");
                                                               requirePresent(asHit, "context.patchPass", "AS hit path must patch current build callbacks");
                                                               requirePresent(asHit, "entry.retainedState", "AS hit path must patch retained BLAS backing state");
                                                               requirePresent(asHit, "prepared.instances", "AS hit path must patch every BLAS referenced by the current TLAS");
                                                               requirePresent(asSource, "RetainedAccelerationStructureState retainedState", "AS cache entries must retain cross-frame BLAS build/read state");
                                                               requirePresent(asSource, "blasResourceByMesh.at(instance.mesh)", "TLAS declarations must read every current BLAS, including stable entries");
                                                               requirePresent(asSource, "\"no-instances\"", "AS preflight must key the unavailable branch");
                                                               requirePresent(asSource, "\"tlas-only\"", "AS preflight must key the TLAS-only branch");
                                                               requirePresent(asSource, "\"dirty-blas\"", "AS preflight must key the dirty-BLAS branch");
                                                               requirePresent(asSource, "prepared.dirtyMeshes", "AS exact key must include the current dirty mesh set");
                                                               requirePresent(asSource, "cached.geometries.size()", "AS dirty variants must include geometry topology");
                                                               requirePresent(asSource, "frameSlot.instanceBufferSize", "AS exact key must include current per-slot resource capacity");
                                                               requireAbsent(asHit, "detail::prepareAsFrame", "AS hit patch must not rebuild or advance preflight state when its prepared packet is unavailable");
                                                               requireOrdered(asHit, "snapshot.branchKey != expectedBranch", "auto prepared = std::move(*runtime_->preparedFrame)", "AS hit patch must validate the branch before consuming its prepared packet");
                                                               requireOrdered(asHit, "snapshot.branchKey != expectedBranch", "runtime_->preparedFrame.reset()", "AS hit patch must preserve its prepared packet on a branch mismatch");
                                                               requireOrdered(asCold, "runtime_->preparedFrame.has_value()", "auto prepared = std::move(*runtime_->preparedFrame)", "AS build must require preflight preparation before consuming its packet");
                                                               requireOrdered(asCold, "auto prepared = std::move(*runtime_->preparedFrame)", "detail::declarePreparedAsFrame(context, *runtime_, std::move(prepared))", "AS build must declare only the preflight-prepared packet");
                                                               requireAbsent(asCold, "detail::prepareAsFrame(", "AS build must not retain a no-packet fallback preparation path");
                                                               requirePresent(presentAdvance, "processCompletedScreenshot(frameSlot)", "Present must process completed screenshots through the renderer continuation hook");
                                                               requireOrdered(renderer, "installedNode.runtime->advanceContinuations(begin.frameIndex)", "installedNode.runtime->structuralSnapshot(nodeFrameParameters)", "renderer must harvest Present screenshot continuations before capturing structural snapshots");
                                                               requireAbsent(presentHit, "processCompletedScreenshot(", "Present hit patch must not change screenshot topology after key selection");
                                                               requireAbsent(presentCold, "processCompletedScreenshot(", "Present cold materialization must not harvest a continuation during graph construction");
                                                               requireOrdered(presentHit, "expectedSnapshot->branchKey != snapshot.branchKey", "context.patchResource(", "Present hit patch must validate the selected screenshot branch before patching slots");
                                                               requireOrdered(dlssHit, "!snapshot.branchKey.starts_with(\"disabled;\")", "previousBuildTime_ = {}", "disabled DLSS hit patch must validate its branch before changing reset state");
                                                               auto hitSections = std::array{lightHit, asHit, pathHit, accumulateHit, dlssHit, uiHit, presentHit};
                                                               std::ranges::for_each(hitSections, [](std::string_view hit) {
                                                                   requireAbsent(hit, "NodeBuildContext", "migrated hit path must not receive structural context");
                                                                   requireAbsent(hit, "materializeCurrentFrame", "migrated hit path must not call cold materialization");
                                                                   requireAbsent(hit, ".addNode(", "migrated hit path must not declare nodes");
                                                                   requireAbsent(hit, ".addResource(", "migrated hit path must not declare resources");
                                                                   requireAbsent(hit, ".addFrameData(", "migrated hit path must not declare frame data");
                                                                   requireAbsent(hit, ".addPass(", "migrated hit path must not declare passes");
                                                                   requireAbsent(hit, ".addSubmitNode(", "migrated hit path must not declare submits");
                                                               });
                                                               requirePresent(asInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }", "AS supports Skeleton patching");
                                                               requirePresent(pathInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }", "PathTracing supports Skeleton patching");
                                                               requirePresent(dlssInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }", "DLSS supports Skeleton patching");
                                                               requirePresent(uiInterface, "supportsRenderGraphSkeleton() const noexcept override { return true; }", "UI supports Skeleton patching");
                                                               auto const screenshotValidation = presentCold.find("sourceDesc = context.describeImageResource(sourceColor)");
                                                               auto const screenshotConsume = presentCold.find("screenshotPrepared_ = detail::PresentScreenshotPrepared");
                                                               nr::test::require(
                                                                   screenshotValidation != std::string_view::npos &&
                                                                       screenshotConsume != std::string_view::npos &&
                                                                       screenshotValidation < screenshotConsume,
                                                                   "Present capture must validate screenshot source metadata before preparing the one-shot effect");
                                                           }};
} // namespace
