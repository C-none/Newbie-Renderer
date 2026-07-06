# Agent Global Architecture Context

This document is the project-wide architecture index for agents. It stays high-level on purpose:

- it describes current module responsibilities and stable runtime boundaries
- it points to the code and topic documents that own detail
- it does not duplicate coding policy from [AGENTS.md](../../AGENTS.md)
- it describes current reality, not abandoned plans

## 1. `rhi`

`nr.rhi` is the Vulkan execution substrate and shader ABI layer of the project.

It owns:

- device, queue, frame, surface, and swapchain lifetime
- RAII GPU resources and transient pools
- descriptor and pipeline layout infrastructure
- command recording helpers, synchronization, upload, and readback
- Slang compilation and reflection services

It does not own:

- asset import
- scene registries or ECS state
- render-graph topology
- feature-pass business logic

Current dependency frameworks:

- Vulkan-Hpp RAII
- VMA
- GLFW for window and surface bootstrap
- Slang, built from the `src/extern/slang` submodule as a local CMake package and imported through `find_package(slang)` at the `src/extern` boundary
- Nsight Graphics SDK through the `dependency.nsight` wrapper when enabled

Current boundary notes:

- Windows + Vulkan + RTX-class hardware are hard assumptions.
- RHI physical-device selection and device creation require graphics, compute, and a dedicated physical copy/transfer queue family plus `VK_KHR_maintenance9` and `VK_EXT_full_screen_exclusive`; the frame-present policy is compute-final, and the selected compute queue family must support surface presentation.
- `PresentationContext` owns presentation-facing GLFW window state: input polling, framebuffer availability checks, swapchain acquire/present state, selected swapchain format/color-space, and requested exclusive fullscreen transitions. `Surface` switches the GLFW window monitor and restores saved windowed bounds; `PresentationContext` creates application-controlled fullscreen-exclusive swapchains, queries the current Win32 monitor capability, and releases/re-acquires exclusive mode around swapchain recreation. The viewer loop skips rendering while GLFW reports a zero-sized framebuffer.
- Swapchain format selection prefers scRGB, then HDR10 PQ, then SDR format/color-space pairs reported by Vulkan WSI. HDR10 presentation metadata is applied when `VK_EXT_hdr_metadata` is available.
- Command invocation should stay on Vulkan-Hpp RAII member functions instead of project-local dispatch tables.
- RHI command-buffer helper APIs expose `const vk::raii::CommandBuffer&`; raw `vk::CommandBuffer` handles stay internal implementation details.
- `CommandBatch` submit paths are one-shot consumption boundaries: `Device::submitFrameBatch`, `Device::submitFrame`, `Device::endFrame`, and `GpuQueue::submit(CommandBatch&&)` require an rvalue batch so submit call sites cannot accidentally reuse a batch after present or synchronization metadata has been appended. `CommandBatch` stores submit-ready synchronization arrays, so queue submission builds only stack `vk::SubmitInfo2` / frame-boundary views instead of per-submit packet vectors.
- RHI device creation optionally enables `VK_EXT_frame_boundary` when a graphics debugger exposes it, and `CommandBatch` can attach frame-boundary submit metadata through `vk::SubmitInfo2::pNext` without making the debugger extension a required runtime capability.
- RHI owns env-driven Nsight Graphics SDK activity setup through `NsightGraphicsFrameHelper`. `Device` calls the helper at Vulkan lifecycle points while the helper reads `NR_NSIGHT_GRAPHICS_ACTIVITY`, `NR_NSIGHT_GRAPHICS_FRAME`, `NR_NSIGHT_GRAPHICS_FRAMES`, `NR_NSIGHT_GRAPHICS_OUTPUT_DIR`, and `NR_NSIGHT_GRAPHICS_INSTALL_DIR`, injects the selected capture-or-trace activity before Vulkan instance creation, initializes it after queues are ready, and emits SDK frame boundaries from the compute-present path with the current swapchain image.
- Public command-recording helper interfaces in `nr.rhi` (for example `updateResourcesForBindingSnapshot`, `bindPreparedResourcesToCommandBuffer`, `pushConstantsToCommandBuffer`, and `ops::ScopedRendering`) take project-owned typed inputs; command-recording helpers take `const vk::raii::CommandBuffer&` as the primary boundary type.
- `nr.rhi` exposes descriptor-indexing, buffer-device-address, ray-tracing, and Vulkan 1.4 capability/property snapshots from `Device`, and its descriptor/pipeline layer supports runtime-sized descriptor arrays driven by Slang reflection with a semantic multi-set ABI for sampler, sampled-image, storage-image, buffer, and acceleration-structure arrays.
- `ShaderService` owns Slang module loading, `.slang-module` frontend-cache use, ABI-stable link-time variant composition, linked `SlangProgram` process-memory caching, and reflection-facing program objects. Variants may specialize link-time constants and `common`-visible type aliases, but descriptor and push-constant ABI changes are rejected by `ShaderDescriptorLayout` ABI signature validation before variant PSO creation.
- RHI ray-tracing helpers cover BLAS/TLAS build recording, multi-geometry BLAS input, AS copy/compaction/serialization/query operations, SBT record payload packing, trace/indirect-trace recording, maintenance1 indirect2 dispatch, and RT-specific sync2 stage/access helpers.
- RHI copy helpers record Vulkan-Hpp copy commands 2 while keeping narrow adapters for existing copy-region structs.
- `Buffer::writeMappedAndFlush(...)` is the RHI helper for direct CPU writes to mapped GPU-visible buffers; GPU-only buffer/image uploads use the device-level `UploadReadbackContext` staging ring, which defaults both upload and readback rings to 128 MiB and exposes upload timeline polling for higher layers. Maintenance9-backed transfer policy omits explicit queue-family ownership transfers when Vulkan guarantees cross-family content preservation, while keeping semaphore queue waits and required image layout transitions. Buffer uploads can target concurrent-sharing buffers without a queue-ownership acquire when the destination is intentionally shared across transfer, graphics, and compute queues.
- `PipelineState` retains the source `SlangProgram` so reflection-backed cursor access remains valid after pipeline creation.
- `rhi` is the execution layer, not the content-organization layer.

Entry points:

- Module aggregation: [../../src/rhi/exportModule.ixx](../../src/rhi/exportModule.ixx)
- Device and frame lifetime: [../../src/rhi/nrDevice.ixx](../../src/rhi/nrDevice.ixx)
- Window surface and presentation context: [../../src/rhi/nrSurface.ixx](../../src/rhi/nrSurface.ixx), [../../src/rhi/nrSwapchain.ixx](../../src/rhi/nrSwapchain.ixx)
- Nsight Graphics SDK frame helper: [../../src/rhi/nrNsightGraphics.ixx](../../src/rhi/nrNsightGraphics.ixx)
- RAII resources: [../../src/rhi/nrResource.ixx](../../src/rhi/nrResource.ixx)
- Descriptor and pipeline services: [../../src/rhi/nrDescriptor.ixx](../../src/rhi/nrDescriptor.ixx), [../../src/rhi/nrPipeline.ixx](../../src/rhi/nrPipeline.ixx)
- Ray tracing helpers: [../../src/rhi/nrAccelerationStructure.ixx](../../src/rhi/nrAccelerationStructure.ixx), [../../src/rhi/nrRayTracing.ixx](../../src/rhi/nrRayTracing.ixx), [../../src/rhi/nrResourceOps.ixx](../../src/rhi/nrResourceOps.ixx)
- Topic docs: [../rhi_command_execution_strategy.md](../rhi_command_execution_strategy.md), [../slang_bindingtype_descriptor_mapping.md](../slang_bindingtype_descriptor_mapping.md)

## 2. `load`

`nr.load` is the file-import and texture-decode front end.

It owns:

- importer backend dispatch
- Assimp-based scene import
- texture discovery and decode
- construction of `nr::load::SceneAsset`

It does not own:

- runtime ECS state
- canonical CPU registries
- GPU residency
- renderer orchestration

Primary flow:

`SceneLoadRequest` -> backend dispatch -> import/decode -> `SceneAsset`

Material boundary facts:

- `MaterialTextureBinding` carries an enum `MaterialTextureSlotSemantic` plus the original source semantic name for diagnostics; Assimp raw texture types are mapped at the load boundary.
- Load keeps importer-specific authoring data, while the scene bridge is responsible for turning supported semantics into canonical resource material slots and optional PBR extension blocks.
- Assimp-imported glTF/glb texture coordinates are normalized at the load boundary so `SceneAsset` UVs use glTF image-space V orientation before renderer/material sampling sees them.
- Assimp light import preserves static punctual light authoring data for `directional`, `point`, and `spot` lights, including glTF `KHR_lights_punctual` photometric intensity, color, source range, and cone data when present; broader Assimp-only light kinds remain source data until the scene bridge decides whether to accept them.

Current dependency frameworks:

- Assimp
- stb_image
- libjpeg-turbo

Third-party boundary note:

- Consumers reach legacy third-party declarations through narrow named C++ modules such as `dependency.vulkan`, `dependency.vma`, `dependency.window`, `dependency.math`, `dependency.ui`, `dependency.assets`, `dependency.slang`, `dependency.ecs`, and `dependency.nsight`; [`src/extern/exportDependency.ixx`](../../src/extern/exportDependency.ixx) remains a compatibility umbrella that re-exports them.
- External headers are confined to the `src/extern` boundary; internal project source imports the narrow `dependency.*` module it needs instead of including third-party headers directly.
- The bundled Slang submodule is configured and installed into the build tree as a local package before the `dependency` target links the imported `slang::slang` target.
- Nsight Graphics SDK headers stay private to [`src/extern/nsightGraphicsSdkImpl.cpp`](../../src/extern/nsightGraphicsSdkImpl.cpp); engine modules use the exported `nr::platform` wrapper API from `dependency.nsight`.

Entry points:

- Data model: [../../src/load/nrLoadType.ixx](../../src/load/nrLoadType.ixx)
- Backend dispatch: [../../src/load/nrLoadBackend.ixx](../../src/load/nrLoadBackend.ixx)
- Loader entry: [../../src/load/nrLoadLoader.ixx](../../src/load/nrLoadLoader.ixx)
- Assimp bridge: [../../src/load/nrLoadAssimp.ixx](../../src/load/nrLoadAssimp.ixx)

## 3. `resources`

`nr.resource` is the canonical CPU-side resource data layer.

It owns:

- value-type resource structures
- typed handles used across runtime layers
- geometry and math helpers close to resource data
- local validation and normalization helpers

It does not own:

- import backends
- ECS or scene lifetime
- GPU handles or Vulkan execution
- viewer/input-driven runtime camera control

Important current facts:

- handles are the stable cross-module reference vocabulary
- `CameraAsset` stores authored projection data, not runtime view state
- `LightAsset` stores static punctual light parameters such as type, unitless linear color, glTF photometric intensity (`lux` for directional, `candela` for point/spot), point/spot source range, spot cone angles, and shadow flag; world-space light instances live in `scene`
- `Material` stores metallic-roughness `MaterialCorePbr` data, optional clearcoat/sheen/transmission/anisotropy blocks, and enum-indexed texture slots instead of string semantic slots; specular-glossiness authoring inputs are converted before entering `nr.resource`
- `Mesh` owns shared vertex/index arrays plus `MeshGeometry` ranges; each geometry is a material-mapped source primitive and future BLAS geometry
- scene registries mirror this handle family

Entry points:

- Module export entry: [../../src/resource/exportModule.ixx](../../src/resource/exportModule.ixx)
- Topic doc: [../resource_module_architecture.md](../resource_module_architecture.md)

## 4. `scene`

`nr.scene` is the runtime world and bridge layer between `load`, `resource`, `rhi`, and `renderer`.

It owns:

- canonical key planning from `SceneAsset`
- CPU registries for meshes, materials, textures, cameras, and lights
- template and instance lifetime
- Flecs world organization
- GPU upload and residency tracking, including scene-level vertex/index geometry atlas buffers
- selector-driven packet extraction
- imported primary-camera resolution plus fallback runtime camera

Primary flow:

`SceneAsset` -> `SceneBridgePlan` -> resource registration -> template prefab tree -> runtime instances -> `beginFrame / uploadPending / updateSimulation / extractPackets` -> `ScenePacketSet` with draw/TLAS/light instance packets -> `SceneRenderBridge::buildFrame(...)` -> `SceneBridgeFrame`

Stable output boundaries today:

- `SceneExtractProfileCreateInfo` + `SceneExtractInput`
- `ScenePacketSet`
- `SceneResolvedCamera`
- `SceneBridgeFrame`
- `SceneAccelerationStructureMesh`
- `SceneAccelerationStructureMeshSemanticKey`

Boundary notes:

- extraction is profile-first, not multi-view render-list-first
- viewport-dependent projection and frustum resolution already live here
- material import no longer classifies texture strings in the production scene bridge; it consumes the enum slot semantic emitted by `load`
- material import converts specular-glossiness factors to metallic-roughness and warns when unsupported spec/gloss texture baking would be required
- light import accepts `directional`, `point`, and `spot` only. It keeps the resource `LightAsset` as static authoring data with glTF photometric intensity and point/spot source range, attaches runtime `SceneLightBinding` components to template light entities, and emits active-instance light packets with world position and world -Z direction during extraction; unsupported light kinds such as ambient or area are warned and skipped before runtime packets are created.
- raster extraction fans out per mesh geometry, while ray tracing/TLAS extraction stays at node mesh instance granularity and is not camera/frustum culled; the detailed RT/TLAS visibility contract lives in [../scene_module_flecs_architecture.md](../scene_module_flecs_architecture.md)
- ray tracing/TLAS extraction uses the RT material readiness contract: resident meshes are required, invalid geometry material handles use the RT fallback material, and valid geometry material handles require resident material CPU data plus resident referenced material textures before the instance is extracted.
- input-driven free-camera control remains outside `scene` and is currently wrapped by `nr.app` for application-style entry points
- scene GPU uploads keep transfer-completed batches pending until the upload timeline reaches the ticket signal, then submit graphics queue synchronization and any required acquire barriers without blocking the CPU on a fence
- mesh uploads append CPU vertex/index data into scene-owned GPU-only geometry atlas buffers via `UploadReadbackContext`; mesh GPU payloads store atlas slice metadata, while atlas growth copies the old prefix into larger scene buffers and retires old buffers after submitted copy work completes. Atlas buffers include acceleration-structure build-input and device-address usage and are created with transfer/graphics/compute queue-family visibility when those queues differ, so renderer AS nodes can build BLAS from resident scene geometry on graphics or compute.
- `Scene::tryGetAccelerationStructureMeshSemanticKey(...)` exposes the lightweight AS semantic key used by renderer AS nodes to decide whether cached per-mesh BLAS build descriptions need refresh. `Scene::tryGetAccelerationStructureMesh(...)` exposes the matching current resident mesh as atlas-backed AS build input: vertex/index bindings, geometry ranges, GPU version, opaque/non-opaque geometry flags, and mesh/material-derived RT instance flags for imported winding and double-sided culling. `Mesh::clockwiseFrontFace` is imported through the load-to-scene bridge; CCW meshes set Vulkan RT triangle-facing flip, clockwise meshes keep the Vulkan RT default, and the renderer AS node XORs the flip state for negative-determinant instance transforms while writing TLAS instances. It is a query boundary, not a scene-owned RT build frame.
- `SceneRenderBridge` supports frame-constants override, frame-level geometry atlas bindings, per-draw geometry resolution, per-material raster-state resolution, and enum-ordered 16-bit material texture IDs so render passes can consume draw-ready atlas-backed geometry, material culling policy, and texture-table indices through bridge contracts

Entry points:

- Main implementation: [../../src/scene/nrScene.ixx](../../src/scene/nrScene.ixx)
- Public types: [../../src/scene/nrSceneType.ixx](../../src/scene/nrSceneType.ixx)
- Bridge logic: [../../src/scene/nrSceneBridge.ixx](../../src/scene/nrSceneBridge.ixx)
- Topic doc and RT/TLAS visibility contract: [../scene_module_flecs_architecture.md](../scene_module_flecs_architecture.md)

## 5. `renderer`

`nr.renderer` is the frame orchestration layer.

It owns:

- installed graph lifetime
- node initialize/build/shutdown lifecycle
- frame-graph build, compile, prepare, execute, and present flow
- submit-boundary planning across queues
- scene-path integration inside `renderFrame(...)`

It does not own:

- scene asset lifetime
- scene registries
- input handling

Current frame path:

`Renderer::installGraph(spec)` installs long-lived nodes once -> each `renderFrame(input)` begins the device frame -> optionally drives `scene` extraction and bridge building (with optional app-side camera override) -> forwards optional per-frame service state through `FrameServices` -> collects optional node UI sections in installed pipeline order -> builds the graph -> compiles through the renderer/RDG cache suite -> prepares on the main thread -> records pass secondaries on RDG worker threads -> merges, submits, and presents on the main thread

Boundary notes:

- the preferred raster scene-facing input is `SceneBridgeFrame`; during graph build, renderer imports the frame once as graph-owned frame data and exposes `NodeFrameParameters::sceneBridgeFrameHandle` to all nodes
- renderer also extracts a lightweight `ScenePacketDomain::tlasBuildInput` packet set each frame with `SceneVisibilityMode::none` and exposes `NodeFrameParameters::scene` plus `sceneTlasBuildInputs` so AS-capable nodes can decide BLAS/TLAS create, rebuild, storage-atlas allocation, and retire policy without a separate scene RT build frame. The default scene packet set also carries active light instance packets for nodes that need frame-local scene light uploads.
- renderer can use scene-resolved camera data or an optional app/viewer camera override
- when camera override is present, raster scene extraction uses `customFrustum` and bridge frame constants come from override data; TLAS extraction remains uncullable and covers the whole active RT scene
- renderer resolves mesh geometry draw parameters against scene atlas allocations: indexed draws carry atlas-adjusted `firstIndex` and `vertexOffset`, non-indexed draws carry atlas-adjusted `firstVertex`, frame-level atlas buffer bindings are copied into `SceneBridgeFrame::geometryBuffers`, and resident material textures are exposed through the frame's global bindless sampled-texture table with ID 0 reserved for the purple fallback. The renderer seeds this table from both raster bridge material texture IDs and TLAS/RT material texture references before graph build, so PathTracing samples the same resident scene textures as raster passes.
- renderer frame-resource keys include the RT-only material and geometry-atlas sideband published by the AS node: `sceneRtInstanceMetadata`, `sceneRtGeometryMetadata`, `sceneRtMaterialHeaders`, `sceneRtMaterialLayers`, `sceneRtMaterialTextureRefs`, `sceneRtVertexAtlas`, and `sceneRtIndexAtlas`. `LightPrepareNode` publishes `sceneLightHeader`, `sceneLights`, and `sceneLightAliasTable` as the frame-local scene light upload. Shader-visible RT nodes bind those buffers through `RayTracingPassBuilder::uniform(...)` / `storageBuffer(...)` in the same prepare/record split used by other descriptor-backed resources.
- `FrameServices` is the current renderer-side sideband for app-owned per-frame services that render passes may consume without creating a direct app-layer dependency on renderer internals
- Renderer owns the node UI collection contract: `NodeRuntime::collectUi(...)` can add `NodeUiSection`s through `NodeUiBuildContext`, renderer stores them for the current frame in installed node order, and `NodeFrameParameters::nodeUiSections` exposes them to the UI render node. The app layer supplies the concrete `NodeUiWriter`; node UI callbacks should write node-owned staged state so changes become visible on the next frame.
- `Renderer::uninstallGraph()` is the explicit installed-graph release boundary for pipeline replacement: it waits for the device to go idle, clears frame-graph builder state, executor-retained command buffers, renderer/RDG cache-suite state, and timing state, shuts down installed nodes, and leaves scene/model ownership untouched.
- `NodeBuildContext` exposes a frame-local named resource registry for cross-node `GraphResourceHandle` handoff through `nr::renderer::frameResource::*` keys, plus node-scoped resource declaration phrases in [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx) for graph-transient color images, node-owned imported color/storage/depth images, retained imported storage images, sampled-only imported images, swapchain images, imported buffers, imported acceleration structures, graph-owned frame data, and read-only renderer global resources such as the current frame uniform binding, scene bindless texture table, and renderer-owned bindless table cache; `nr::renderer::use::*` factories produce the canonical buffer, image, and acceleration-structure pass-use descriptors that still flow through `RenderGraphBuilder` validation. Shader-access uses may carry an explicit shader-stage override, while AS build input buffers map to acceleration-structure-build stage plus shader-read access and AS scratch buffers map to acceleration-structure-build stage plus AS read/write access. `use::orderedAfterPrevious(...)` marks an explicit same-resource ordering edge when a later pass needs a barrier even if queue and layout do not otherwise force one.
- Renderer also exposes common node authoring helpers: `RendererCacheSuite` centralizes the RDG compile cache, bindless image table application cache, and renderer global scene texture descriptor table versioning; `PipelineRuntime` owns pipeline state plus per-frame descriptor sets, including runtime-sized descriptor arrays and retained graphics pipeline dynamic-rendering metadata; renderer-owned `FrameUniformArena` owns one large CPU-to-GPU uniform buffer split into `maxFrameInFlight` frame slices and uploads renderer global frame uniforms before node build, including current view/projection data, inverse view-projection, previous view plus derived previous view-projection, camera-world data, and frame state with monotonic sample-frame ordinal plus resource frame slot; and `RasterPassBuilder` / `ComputePassBuilder` / `RayTracingPassBuilder` generate the standard prepare/record glue for descriptor updates, dynamic binding snapshots, pipeline binding, prepared descriptor binding, 128-byte-limited push constants, dynamic rendering/ray tracing setup, viewport/scissor, raster state, and dispatch or trace setup. These helpers stamp pass shader-stage scopes separately from queue domain: raster stays graphics-scoped with optional per-resource vertex/fragment overrides, compute uses `eComputeShader`, and ray tracing uses `eRayTracingShaderKHR`; the compiler uses those scopes for shader-access barrier stages while fixed-function transfer, attachment, AS, SBT, and present scopes stay intent-driven. `RasterPassBuilder` can opt a pass into unordered parallel range recording through the generic RDG parallel addPass contract. RDG pass contexts can resolve buffers, images, imported acceleration structures, and typed graph frame data; the default logical descriptor resolver can write acceleration-structure descriptors and explicit buffer offset/range descriptors from those resources.
- `RenderGraphExecutor` uses the `nr.utils:threading` static worker pool for RDG CPU recording work. Build, compile, runtime resource resolution, pass prepare callbacks, primary command buffers, per-batch result aggregation, queue submit, and present remain main-thread responsibilities, while pass record tasks for all submit batches are launched before ordered batch assembly begins.
- RDG pass record work uses worker-only secondary command-pool slots; the frame secondary slot 0 is reserved away from pass record tasks so the main thread stays an aggregation/submit owner during execute.
- `RenderGraphExecutor` also owns delayed Vulkan timestamp queries for compiled addPass work: it reads the previous use of a frame-slot query pool before reuse, resets the current batch's query range in the primary command buffer, wraps each recorded secondary execution with sync2 `writeTimestamp2` timestamp writes on the main thread, and returns completed GPU pass timing samples for renderer-side averaging.
- `submitNode` is a debug-named batch-splitting marker. The compiler carries its debug name into the opened submit batch, and executor GPU debug labels include that name for capture readability.
- Pass record callbacks are worker-capable and record into executor-retained secondary command buffers. The executor queues one-shot future-returning work items across all compiled submit batches, then gathers each batch's results only when the main thread reaches that batch and executes the recorded secondaries on the primary command buffer in compiled pass order. A pass that opts into parallel record is split into executor-planned contiguous ranges using the shared `nr.utils:threading` range planner, with unordered chunk replay semantics; ordered rendering work must be expressed as multiple ordered `addPass` calls with resource-use declarations between them. See [../renderer_parallel_drawcall_recording.md](../renderer_parallel_drawcall_recording.md).
- When `VK_EXT_frame_boundary` is enabled by an injected graphics debugger, each RDG submit batch is tagged with the same monotonic frame-boundary ID and the final compute-present submit carries `eFrameEnd` plus the current swapchain image handle so multi-queue captures group graphics and compute work as one frame.
- application-facing code that owns both renderer and scene should prefer `nr::app::AppSession`, which also owns the interactive app camera used to build viewer-style overrides
- `AppSession::destroyScene()` waits for the renderer device to go idle, asks `Renderer` to reset cached scene extraction bindings, and then destroys the optional scene. This keeps model reloads from reusing stale scene-local extract profile handles when the next `Scene` is reconstructed at the same storage address.

Entry points:

- Runtime entry: [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx)
- Frame service bridge: [../../src/renderer/nrFrameServices.ixx](../../src/renderer/nrFrameServices.ixx)
- Viewer camera runtime module: [../../src/renderer/nrViewerCamera.ixx](../../src/renderer/nrViewerCamera.ixx)
- Graph types: [../../src/renderer/nrRenderGraphType.ixx](../../src/renderer/nrRenderGraphType.ixx)
- Builder, compiler, executor: [../../src/renderer/nrRenderGraphBuilder.ixx](../../src/renderer/nrRenderGraphBuilder.ixx), [../../src/renderer/nrRenderGraphCompiler.ixx](../../src/renderer/nrRenderGraphCompiler.ixx), [../../src/renderer/nrRenderGraphExecutor.ixx](../../src/renderer/nrRenderGraphExecutor.ixx)
- Terminology note: [../../src/renderer/README.md](../../src/renderer/README.md)

## 6. `renderpasses`

`nr.renderPasses` is the feature-node implementation layer on top of the renderer contract.

Current built-in nodes:

- `EmbeddedTriangleNode`
- `AccelerationStructureBuildNode`
- `LightPrepareNode`
- `PathTracingNode`
- `AccumulateNode`
- `NormalBufferNode`
- `UiNode`
- `PresentNode`

It owns:

- concrete `NodeRuntime` implementations
- pass intent declaration
- pass prepare callbacks for descriptor updates and per-frame mutable setup
- pass record callbacks that use `rhi` command-recording services

It does not own:

- scene lifetime
- render-graph core orchestration
- asset import

Current boundary notes:

- nodes consume `NodeBuildContext` and `NodeFrameParameters`; scene-wide deferred data reaches record callbacks through graph frame-data handles instead of borrowed build-time references
- Built-in shader-visible passes use renderer-side authoring helpers instead of node-local descriptor update/bind paths: `EmbeddedTriangleNode` and `NormalBufferNode` use `PipelineRuntime`, renderer global frame resources, and `RasterPassBuilder`; `UiNode` uses `PipelineRuntime` and `RasterPassBuilder` with a dynamic bindless texture snapshot; `PresentNode` and `AccumulateNode` use `PipelineRuntime` and `ComputePassBuilder` for compute work; and `PathTracingNode` uses `PipelineRuntime`, `RayTracingPassBuilder`, and an RHI shader-binding table to trace the scene TLAS into a storage image.
- `RasterPassBuilder` supports explicit viewport Y modes. Scene/model passes that produce clip-space Y-up output use the negative-height viewport mode so Vulkan Y-axis adaptation happens before rasterization and front-face determination; screen-space UI passes keep the default top-left framebuffer mode.
- Shader sources import the root [../../shader/common.slang](../../shader/common.slang) module. The common module includes [../../shader/include/globalUniform.slang](../../shader/include/globalUniform.slang), which implements `common` and declares the shared `gFrame` uniform at Vulkan set 5 binding 0, [../../shader/include/sceneTextures.slang](../../shader/include/sceneTextures.slang), which declares the global `gSceneTextures[]` combined sampler table at set 1 binding 0, [../../shader/include/sceneLights.slang](../../shader/include/sceneLights.slang), which declares the global scene light header/list/alias ABI at set 6 bindings 0, 1, and 2, [../../shader/include/materialTextureIds.slang](../../shader/include/materialTextureIds.slang), which owns the shared material texture ID slot constants and 16-bit pair unpack helpers, [../../shader/include/pathTracing/roulette.slang](../../shader/include/pathTracing/roulette.slang), which declares the resource-free link-time Russian roulette policy for PathTracing variants, and [../../shader/include/material/base.slang](../../shader/include/material/base.slang), which defines the shader-side multi-layer material interface. Renderer uploads `Renderer.GlobalFrameUniforms` once per frame with current render camera matrices, previous view data for future motion-vector work, camera world position, and frame state whose `xy` lanes carry a monotonic 64-bit sample-frame ordinal while `z` preserves the resource frame slot; PT graphs can enable Halton(2,3) subpixel jitter so the uploaded projection/view-projection are jittered while scene extraction and camera stability use the unjittered camera constants. C++ render-pass code binds `gFrame` from `NodeBuildContext::globalResources` only for shaders that use it; passes that use or may use `gSceneTextures[]` share [../../src/renderPasses/nrSceneTextureTableBinding.ixx](../../src/renderPasses/nrSceneTextureTableBinding.ixx) as a thin adapter into the renderer-owned bindless table cache, with ID 0 reserved for the purple fallback texture and a linear immutable sampler installed during PSO layout creation.
- `PresentNode` is the final compute conversion, optional UI composition, readback, screenshot, and swapchain-copy path. It converts the scene color buffer into swapchain-ready SDR, HDR10 PQ, or scRGB output from the selected swapchain format/color-space pair, applies optional source flip plus final output encoding, alpha-composites the standalone UI buffer as SDR reference-white content when `frameResource::uiColor` is published, and uses a transparent graph-local UI fallback for non-UI graph variants. It optionally copies the converted image into a caller-provided readback buffer on the compute queue, then copies to the swapchain. The optional readback pass is inserted before copy-to-swapchain and uses `PresentReadbackTarget` from [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx). UI-triggered screenshots copy `frameResource::presentSourceColor` before present encoding or UI composition into a node-owned host-visible buffer and write a linear EXR only after the captured frame slot has completed. The default scene path leaves `flipY` disabled because scene raster passes already apply Vulkan Y-axis adaptation through viewport state. Its node UI exposes next-frame UI opacity and screenshot request staging.
- `LightPrepareNode` is a non-shader-visible upload node. It prepends a warm default directional sun, consumes active light instance packets from `NodeFrameParameters::scenePackets`, packs the set 6 scene light ABI into CPU-visible per-frame buffers, builds a Rec.709-luminance-times-photometric-intensity alias table, and publishes `frameResource::sceneLightHeader`, `frameResource::sceneLights`, and `frameResource::sceneLightAliasTable`.
- `PathTracingNode` consumes `frameResource::sceneTlas` plus the RT metadata/material buffers, scene vertex/index atlas resources, and published scene light buffers, then publishes `frameResource::presentSourceColor` as a fixed 1spp single-frame result. It owns a node-local `std::map<PathTracingVariantKey, PathTracingVariantRuntime>` so each ABI-stable shader variant has its own RT pipeline runtime and SBT; these PSOs are not stored in `RendererCacheSuite`. The shader entry [../../shader/renderer/pathTracing.slang](../../shader/renderer/pathTracing.slang) keeps the ray tracing entry-point ABI and constructs a scheduler from focused `shader/renderer/pathTracing/*` modules: `Scheduler` owns exactly one raygen path per pixel, `Pt` owns the per-path `PathState` plus trace/hit/miss/output methods, closest-hit reconstructs the hit triangle from `InstanceID()`, `GeometryIndex()`, `PrimitiveIndex()`, RT metadata, and scene atlas data through [../../shader/include/rt/resources.slang](../../shader/include/rt/resources.slang) and [../../shader/include/rt/hitSurface.slang](../../shader/include/rt/hitSurface.slang), and closest-hit only returns a resolved material payload using [../../shader/include/material/payload.slang](../../shader/include/material/payload.slang). Raygen owns pixel- and monotonic-sample-frame-based RNG seeding, direct lighting from glTF photometric scene lights, finite-distance visibility rays for punctual lights, miss/emissive contribution, and v1 diffuse/GGX scatter recursion. Point/spot direct lighting uses inverse-square attenuation with the optional glTF range fade. Alpha-mask material coverage remains in any-hit with `IgnoreHit()`. If no TLAS or complete RT sideband is published for the frame, it clears its output instead of requiring RT consumers to run.
- `AccumulateNode` consumes the PT single-frame `frameResource::presentSourceColor`, reads/writes node-owned ping-pong history images on the compute queue, republishes `presentSourceColor` for `PresentNode`, and resets when renderer camera stability state reports a real camera or extent change. It uses exact running average weights up to the configured stable sample cap, defaulting to 256 and clamped to `[1,4096]`, then clamps the current-frame EMA weight to `1 / cap`. Its node UI exposes next-frame history-sample staging through a uint input.
- `NormalBufferNode` consumes `NodeFrameParameters::sceneBridgeFrameHandle`, resolves `SceneBridgeFrame` through the RDG pass context during record, binds the scene geometry atlas vertex/index buffers once at the start of each parallel chunk, and records atlas-adjusted scene mesh draw calls (indexed/non-indexed) with world-space normal visualization while honoring the per-material culling state already resolved by the scene bridge. Its raster push constants keep three model rows plus material texture IDs packed as 16-bit pairs, and its raster pass opts into `RasterPassBuilder::recordParallel(...)`; each executor-planned chunk records an independent contiguous range of `SceneBridgeFrame::rasterDraws`.
- `AccelerationStructureBuildNode` is a non-shader-visible graph node. It consumes `NodeFrameParameters::sceneTlasBuildInputs` plus scene AS semantic/mesh/material queries, owns a renderer-side BLAS cache backed by one BLAS storage atlas buffer plus frame-slot TLAS/instance/scratch/RT-material-sideband resources, refreshes cached per-mesh BLAS build descriptions only when the scene AS semantic key changes, batches dirty rebuild-only BLAS builds, rebuilds TLAS every frame, assigns dense TLAS instance custom indices for RT metadata lookup, and publishes `frameResource::sceneTlas` with RT instance, geometry, material-header, material-layer, material-texture-ref, scene-vertex-atlas, and scene-index-atlas resources when at least one TLAS instance is available.
- `UiNode` is the Dear ImGui overlay build node. It renders app-queued sections, renderer-collected node UI sections, and trailing frame/performance status sections, then finalizes the app-owned UI frame and consumes the resulting draw data. Frame status includes swapchain format and color space. `UiNode` honors Dear ImGui 1.92.6 `ImTextureData` requests from the vcpkg dependency, manages UI textures through a descriptor-indexed runtime sampled-image array, keeps texture content/lifetime revision tracking locally, uploads texture pixels through the RHI upload ring before importing shader-readable texture images into the graph, selects the sampled texture per draw through push constants, and renders the overlay into its own transparent `uiBuffer` for later composition in `PresentNode`.
- Built-in GPU-only intermediate targets are node-owned imported images: `NormalBuffer.Color` / `NormalBuffer.Depth` and `Ui.Buffer` are selected from per-frame image slots to avoid cross-frame overlap, `Accumulate.History` uses two renderer-persistent ping-pong images with retained image states for PT history layout/access tracking, while the final present converted-color image is a single retained imported image whose format follows the selected SDR/HDR swapchain output path. Renderer global frame uniforms are suballocated from the renderer-owned CPU-to-GPU uniform arena before node build; the renderer-owned purple scene texture fallback, scene-resident material texture images, and UI texture images are referenced by bindless descriptor tables whose per-frame applied-version state lives in `RendererCacheSuite`; scene texture descriptor writes carry image views/layouts while the shared linear sampler is immutable in each consuming PSO layout; UI vertex/index data remain direct mapped frame resources; UI texture pixels use the device-level RHI upload ring.
- node record callbacks should route command recording through `PassRecordContext::commandBuffer` as a RAII `vk::raii::CommandBuffer` reference when calling `nr.rhi` command helpers.
- common pass resource declarations should prefer `NodeBuildContext` resource phrases and `nr::renderer::use::*` intent factories over hand-written graph descriptor fields where the phrase matches the pass semantics.
- shader-visible node bindings are expressed through renderer-side pass builders. The builders capture reflection-backed descriptor and push-constant snapshots, update descriptors during pass prepare, and bind prepared descriptor sets plus push constants during pass record.
- Direct descriptor update/bind helper calls are reserved for renderer/RHI helper implementations, and the detailed policy remains in [../../AGENTS.md](../../AGENTS.md) and [../../src/renderPasses/README.md](../../src/renderPasses/README.md).
- current audit note: built-in shader-visible passes use `RasterPassBuilder`, `ComputePassBuilder`, or `RayTracingPassBuilder`. `AccelerationStructureBuildNode`, `LightPrepare.Upload`, `Present.CopyToReadback`, `Present.CopyScreenshotToReadback`, and `Present.CopyToSwapchain` remain direct `context.addPass(...)` paths because they are non-shader-visible AS-build/upload/copy work; UI texture uploads now occur through `UploadReadbackContext` before the overlay graph pass imports the texture images.
- cross-node built-in resource handoff is not modeled as node input/output ports; producers publish `frameResource::presentSourceColor`, `frameResource::normalDepth`, `frameResource::uiColor`, `frameResource::sceneTlas`, `frameResource::sceneLightHeader`, `frameResource::sceneLights`, `frameResource::sceneLightAliasTable`, and the RT material/geometry atlas sideband keys during build, while consumers require the relevant keys before declaring their passes. `PresentNode` treats `uiColor` as optional, and `AccumulateNode` intentionally republishes `presentSourceColor` after PathTracing so Present sees the accumulated PT result.

Entry points:

- Node type aliases: [../../src/renderPasses/nrNodeType.ixx](../../src/renderPasses/nrNodeType.ixx)
- AccelerationStructureBuild node: [../../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.ixx](../../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.ixx)
- LightPrepare node: [../../src/renderPasses/LightPrepare/nrLightPrepareNode.ixx](../../src/renderPasses/LightPrepare/nrLightPrepareNode.ixx)
- NormalBuffer node: [../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx](../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx)
- Ui node: [../../src/renderPasses/Ui/nrUiNode.ixx](../../src/renderPasses/Ui/nrUiNode.ixx)
- Present node: [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx)
- PathTracing node: [../../src/renderPasses/PathTracing/nrPathTracingNode.ixx](../../src/renderPasses/PathTracing/nrPathTracingNode.ixx)
- Accumulate node: [../../src/renderPasses/Accumulate/nrAccumulateNode.ixx](../../src/renderPasses/Accumulate/nrAccumulateNode.ixx)
- Module note: [../../src/renderPasses/README.md](../../src/renderPasses/README.md)

## 7. `Overall`

The current main chain is:

external asset files  
-> `nr.load` produces `SceneAsset`  
-> `nr.scene` bridges, registers, instantiates, uploads, and extracts runtime packets  
-> `nr.scene` builds `SceneBridgeFrame` with frame-level geometry atlas bindings and exposes TLAS build-input packets, active light instance packets, plus resident AS mesh/material queries
-> `nr.renderer` imports the scene bridge frame as graph-owned frame data, forwards TLAS and light packet input to nodes, then builds and executes the installed graph
-> `nr.renderPasses` records concrete feature work, including optional AS build passes, LightPrepare scene light uploads, and RT-only material/geometry atlas sideband publication/consumption
-> `nr.rhi` executes Vulkan and present work

Application-facing lifetime wrapper:

- `nr::app::AppSession` is the preferred application boundary when one owner needs both `nr.renderer::Renderer` and an optional `nr.scene::Scene`.
- It owns `AppCamera` as the application-side interactive viewer camera and can initialize it from the scene primary camera or a default fallback.
- It is also responsible for resetting renderer scene bindings before replacing the active scene, so model reloads create fresh renderer extract profiles for the new scene.
- It also owns `UiSystem`, begins the Dear ImGui frame before rendering, and exports it to render passes through `makeFrameServices()`.
- It is a safety wrapper, not a new rendering layer: it exists to enforce scene-before-renderer teardown while scene-owned GPU payloads still have a live device/VMA allocator behind them.
- `nr.pipeline` is the viewer orchestration layer above `nr.app`: it owns the registered viewer pipeline list, builds the selected `RendererGraphSpec`, routes model-path loads through `nr.load` and `nr.scene`, stores model history under `build/app`, and queues app-owned UI controls before the built-in frame statistics section. Switching viewer pipelines builds the replacement graph first, then uninstalls the active renderer graph, rebuilds the Slang session so `loadModule` rechecks shader freshness, and installs the new graph without reloading the active scene/model.

Useful reality checks:

- [../../src/app/exportModule.ixx](../../src/app/exportModule.ixx), [../../src/app/nrAppSession.ixx](../../src/app/nrAppSession.ixx), and [../../src/app/nrAppCamera.ixx](../../src/app/nrAppCamera.ixx) provide the application-facing lifetime wrapper plus camera/input encapsulation.
- [../../src/app/nrAppUi.ixx](../../src/app/nrAppUi.ixx) is the app-owned Dear ImGui system wrapper used by render-pass-facing UI.
- [../../src/pipeline/exportModule.ixx](../../src/pipeline/exportModule.ixx) and [../../src/pipeline/nrPipeline.cpp](../../src/pipeline/nrPipeline.cpp) provide the single viewer runtime used by the executable. The registered `normalview` graph is `NormalBuffer -> Ui -> Present`; the registered `rtobject` graph is `AccelerationStructureBuild -> LightPrepare -> PathTracing -> Ui -> Present`.
- [../../src/main.cpp](../../src/main.cpp) is the only app executable entry point; it parses command-line arguments through `nr.pipeline` and can select `normalview` or `rtobject` with `--pipeline`.
- [../../src/extern/CMakeLists.txt](../../src/extern/CMakeLists.txt), the `dependency*.ixx` modules under [../../src/extern](../../src/extern), [../../src/extern/exportDependency.ixx](../../src/extern/exportDependency.ixx), and [../../src/extern/nsightGraphicsSdkBridge.h](../../src/extern/nsightGraphicsSdkBridge.h) are the current source-of-truth for the centralized third-party module boundary used by the LLVM/Ninja build path.
- [../../test/smoke/app/embeddedTriangle.cpp](../../test/smoke/app/embeddedTriangle.cpp) is the renderer-only window loop where `EmbeddedTriangle` publishes `frameResource::presentSourceColor` and `Ui` publishes `frameResource::uiColor` for `PresentNode`, using the same `nr::app::AppSession` camera and UI wrapper with the default camera path.
- [../../test/smoke/app/normalBufferUiSmoke.cpp](../../test/smoke/app/normalBufferUiSmoke.cpp) is the current smoke path that validates the `NormalBuffer + Ui -> Present` integration and non-empty ImGui draw data.
