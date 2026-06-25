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
- RHI physical-device selection and device creation require graphics, compute, and a dedicated physical copy/transfer queue family; the frame-present policy is compute-final, and the selected compute queue family must support surface presentation.
- Command invocation should stay on Vulkan-Hpp RAII member functions instead of project-local dispatch tables.
- RHI command-buffer helper APIs expose `const vk::raii::CommandBuffer&`; raw `vk::CommandBuffer` handles stay internal implementation details.
- RHI device creation optionally enables `VK_EXT_frame_boundary` when a graphics debugger exposes it, and `CommandBatch` can attach frame-boundary submit metadata through `vk::SubmitInfo2::pNext` without making the debugger extension a required runtime capability.
- RHI owns env-driven Nsight Graphics SDK activity setup through `NsightGraphicsFrameHelper`. `Device` calls the helper at Vulkan lifecycle points while the helper reads `NR_NSIGHT_GRAPHICS_ACTIVITY`, `NR_NSIGHT_GRAPHICS_FRAME`, `NR_NSIGHT_GRAPHICS_FRAMES`, `NR_NSIGHT_GRAPHICS_OUTPUT_DIR`, and `NR_NSIGHT_GRAPHICS_INSTALL_DIR`, injects the selected capture-or-trace activity before Vulkan instance creation, initializes it after queues are ready, and emits SDK frame boundaries from the compute-present path with the current swapchain image.
- Public command-recording helper interfaces in `nr.rhi` (for example `updateResourcesForBindingSnapshot`, `bindPreparedResourcesToCommandBuffer`, `pushConstantsToCommandBuffer`, and `ops::ScopedRendering`) take project-owned typed inputs; command-recording helpers take `const vk::raii::CommandBuffer&` as the primary boundary type.
- `nr.rhi` exposes descriptor-indexing, buffer-device-address, ray-tracing, and Vulkan 1.4 capability/property snapshots from `Device`, and its descriptor/pipeline layer supports runtime-sized descriptor arrays driven by Slang reflection with a semantic multi-set ABI for sampler, sampled-image, storage-image, buffer, and acceleration-structure arrays.
- RHI ray-tracing helpers cover BLAS/TLAS build recording, multi-geometry BLAS input, AS copy/compaction/serialization/query operations, SBT record payload packing, trace/indirect-trace recording, maintenance1 indirect2 dispatch, and RT-specific sync2 stage/access helpers.
- RHI copy helpers record Vulkan-Hpp copy commands 2 while keeping narrow adapters for existing copy-region structs.
- `Buffer::writeMappedAndFlush(...)` is the RHI helper for direct CPU writes to mapped GPU-visible buffers; GPU-only buffer/image uploads use the device-level `UploadReadbackContext` staging ring, which defaults both upload and readback rings to 128 MiB and exposes upload timeline polling for higher layers.
- `PipelineState` retains the source `SlangProgram` so reflection-backed cursor access remains valid after pipeline creation.
- `rhi` is the execution layer, not the content-organization layer.

Entry points:

- Module aggregation: [../../src/rhi/exportModule.ixx](../../src/rhi/exportModule.ixx)
- Device and frame lifetime: [../../src/rhi/nrDevice.ixx](../../src/rhi/nrDevice.ixx)
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
- GPU upload and residency tracking
- selector-driven packet extraction
- imported primary-camera resolution plus fallback runtime camera

Primary flow:

`SceneAsset` -> `SceneBridgePlan` -> resource registration -> template prefab tree -> runtime instances -> `beginFrame / uploadPending / updateSimulation / extractPackets` -> `ScenePacketSet` -> `SceneRenderBridge::buildFrame(...)` -> `SceneBridgeFrame`

Stable output boundaries today:

- `SceneExtractProfileCreateInfo` + `SceneExtractInput`
- `ScenePacketSet`
- `SceneResolvedCamera`
- `SceneBridgeFrame`

Boundary notes:

- extraction is profile-first, not multi-view render-list-first
- viewport-dependent projection and frustum resolution already live here
- input-driven free-camera control remains outside `scene` and is currently wrapped by `nr.app` for application-style entry points
- scene GPU uploads keep transfer-completed-but-not-acquired batches pending until the upload timeline reaches the ticket signal, then submit graphics acquire work without blocking the CPU on a fence
- `SceneRenderBridge` now supports frame-constants override and per-draw geometry resolution so render passes can consume draw-ready geometry through bridge contracts

Entry points:

- Main implementation: [../../src/scene/nrScene.ixx](../../src/scene/nrScene.ixx)
- Public types: [../../src/scene/nrSceneType.ixx](../../src/scene/nrSceneType.ixx)
- Bridge logic: [../../src/scene/nrSceneBridge.ixx](../../src/scene/nrSceneBridge.ixx)
- Topic doc: [../scene_module_flecs_architecture.md](../scene_module_flecs_architecture.md)

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

`Renderer::installGraph(spec)` installs long-lived nodes once -> each `renderFrame(input)` begins the device frame -> optionally drives `scene` extraction and bridge building (with optional app-side camera override) -> forwards optional per-frame service state through `FrameServices` -> builds the graph -> compiles -> prepares on the main thread -> records pass secondaries on RDG worker threads -> merges, submits, and presents on the main thread

Boundary notes:

- the preferred scene-facing input is `SceneBridgeFrame`; during graph build, renderer imports the frame once as graph-owned frame data and exposes `NodeFrameParameters::sceneBridgeFrameHandle` to all nodes
- renderer can use scene-resolved camera data or an optional app/viewer camera override
- when camera override is present, scene extraction uses `customFrustum` and bridge frame constants come from override data
- `FrameServices` is the current renderer-side sideband for app-owned per-frame services that render passes may consume without creating a direct app-layer dependency on renderer internals
- `NodeBuildContext` exposes node-scoped resource declaration phrases in [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx) for graph-transient color images, node-owned imported color/storage/depth images, swapchain images, imported acceleration structures, graph-owned frame data, and read-only renderer global resources such as the current frame uniform binding; `nr::renderer::use::*` factories produce the canonical buffer, image, and acceleration-structure pass-use descriptors that still flow through `RenderGraphBuilder` validation.
- Renderer also exposes common node authoring helpers: `PipelineRuntime` owns pipeline state plus per-frame descriptor sets, including runtime-sized descriptor arrays; renderer-owned `FrameUniformArena` owns one large CPU-to-GPU uniform buffer split into `maxFrameInFlight` frame slices and uploads renderer global frame uniforms before node build; and `RasterPassBuilder` / `ComputePassBuilder` generate the standard prepare/record glue for descriptor updates, dynamic binding snapshots, pipeline binding, prepared descriptor binding, 128-byte-limited push constants, dynamic rendering, viewport/scissor, raster state, and dispatch setup. RDG pass contexts can resolve buffers, images, imported acceleration structures, and typed graph frame data; the default logical descriptor resolver can write acceleration-structure descriptors and explicit buffer offset/range descriptors from those resources.
- `RenderGraphExecutor` owns a fixed `std::jthread` pool for RDG CPU recording work. Build, compile, runtime resource resolution, pass prepare callbacks, primary command buffers, result aggregation, queue submit, and present remain main-thread responsibilities.
- RDG pass record work uses worker-only secondary command-pool slots; the frame secondary slot 0 is reserved away from pass record tasks so the main thread stays an aggregation/submit owner during execute.
- `RenderGraphExecutor` also owns delayed Vulkan timestamp queries for compiled addPass work: it reads the previous use of a frame-slot query pool before reuse, resets the current batch's query range in the primary command buffer, wraps each recorded secondary execution with sync2 `writeTimestamp2` timestamp writes, and returns completed GPU pass timing samples for renderer-side averaging.
- `submitNode` is a debug-named batch-splitting marker. The compiler carries its debug name into the opened submit batch, and executor GPU debug labels include that name for capture readability.
- Pass record callbacks are worker-capable and record into executor-retained secondary command buffers. The executor gathers one-shot `std::future` results from `std::packaged_task` work items and executes the recorded secondaries on the primary command buffer in compiled pass order.
- When `VK_EXT_frame_boundary` is enabled by an injected graphics debugger, each RDG submit batch is tagged with the same monotonic frame-boundary ID and the final compute-present submit carries `eFrameEnd` plus the current swapchain image handle so multi-queue captures group graphics and compute work as one frame.
- application-facing code that owns both renderer and scene should prefer `nr::app::AppSession`, which also owns the interactive app camera used to build viewer-style overrides

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
- Built-in shader-visible passes use renderer-side authoring helpers instead of node-local descriptor update/bind paths: `EmbeddedTriangleNode` and `NormalBufferNode` use `PipelineRuntime`, renderer global frame resources, and `RasterPassBuilder`; `UiNode` uses `PipelineRuntime` and `RasterPassBuilder` with a dynamic bindless texture snapshot; and `PresentNode` uses `PipelineRuntime` and `ComputePassBuilder` for compute conversion before the copy-to-swapchain pass.
- Shader sources import the root [../../shader/common.slang](../../shader/common.slang) module. The common module includes [../../shader/globalUniform.slang](../../shader/globalUniform.slang), which implements `common` and declares the shared `gFrame` uniform at Vulkan set 7 binding 0; renderer uploads `Renderer.GlobalFrameUniforms` once per frame and C++ render-pass code binds it from `NodeBuildContext::globalResources` only for shaders that use `gFrame`, currently `EmbeddedTriangleNode` and `NormalBufferNode`.
- `PresentNode` is the final compute conversion and composition path. It converts the scene color buffer into swapchain-ready output, applies flip/gamma/channel conversion, and alpha-composites the standalone UI buffer before copy-to-swapchain
- `NormalBufferNode` consumes `NodeFrameParameters::sceneBridgeFrameHandle`, resolves `SceneBridgeFrame` through the RDG pass context during record, and records real scene mesh draw calls (indexed/non-indexed) with world-space normal visualization
- `NormalBufferNode` now also consumes `nr::app::UiSystem` through `NodeFrameParameters::frameServices` to expose runtime controls such as FPS display, frame time, and front/back-face cull switching
- `UiNode` is the Dear ImGui overlay build node. It finalizes the app-owned UI frame, consumes draw data emitted earlier in the graph, displays renderer/app runtime statistics including averaged CPU timings and delayed GPU addPass timings, honors Dear ImGui 1.92.6 `ImTextureData` requests from the vcpkg dependency, manages UI textures through a descriptor-indexed runtime sampled-image array, uploads texture pixels through the RHI upload ring before importing shader-readable texture images into the graph, selects the sampled texture per draw through push constants, and renders the overlay into its own transparent `uiBuffer` for later composition in `PresentNode`
- Built-in GPU-only intermediate targets are node-owned imported images: `NormalBuffer.Color` / `NormalBuffer.Depth` and `Ui.Buffer` are selected from per-frame image slots to avoid cross-frame overlap, while `Present.ConvertedColor` remains a single imported image. Renderer global frame uniforms are suballocated from the renderer-owned CPU-to-GPU uniform arena before node build; UI vertex/index data remain direct mapped frame resources; UI texture pixels use the device-level RHI upload ring.
- node record callbacks should route command recording through `PassRecordContext::commandBuffer` as a RAII `vk::raii::CommandBuffer` reference when calling `nr.rhi` command helpers.
- common pass resource declarations should prefer `NodeBuildContext` resource phrases and `nr::renderer::use::*` intent factories over hand-written graph descriptor fields where the phrase matches the pass semantics.
- shader-visible node bindings are expressed through renderer-side pass builders. The builders capture reflection-backed descriptor and push-constant snapshots, update descriptors during pass prepare, and bind prepared descriptor sets plus push constants during pass record.
- Direct descriptor update/bind helper calls are reserved for renderer/RHI helper implementations, and the detailed policy remains in [../../AGENTS.md](../../AGENTS.md) and [../../src/renderPasses/README.md](../../src/renderPasses/README.md).
- current audit note: built-in shader-visible passes use `RasterPassBuilder` or `ComputePassBuilder`. `Present.CopyToSwapchain` remains a direct `context.addPass(...)` path because it is non-shader-visible copy work; UI texture uploads now occur through `UploadReadbackContext` before the overlay graph pass imports the texture images.

Entry points:

- Node type aliases: [../../src/renderPasses/nrNodeType.ixx](../../src/renderPasses/nrNodeType.ixx)
- NormalBuffer node: [../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx](../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx)
- Ui node: [../../src/renderPasses/Ui/nrUiNode.ixx](../../src/renderPasses/Ui/nrUiNode.ixx)
- Present node: [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx)
- Module note: [../../src/renderPasses/README.md](../../src/renderPasses/README.md)

## 7. `Overall`

The current main chain is:

external asset files  
-> `nr.load` produces `SceneAsset`  
-> `nr.scene` bridges, registers, instantiates, uploads, and extracts runtime packets  
-> `nr.scene` builds `SceneBridgeFrame`  
-> `nr.renderer` imports the scene bridge frame as graph-owned frame data, then builds and executes the installed graph
-> `nr.renderPasses` records concrete feature work  
-> `nr.rhi` executes Vulkan and present work

Application-facing lifetime wrapper:

- `nr::app::AppSession` is the preferred application boundary when one owner needs both `nr.renderer::Renderer` and an optional `nr.scene::Scene`.
- It owns `AppCamera` as the application-side interactive viewer camera and can initialize it from the scene primary camera or a default fallback.
- It also owns `UiSystem`, begins the Dear ImGui frame before rendering, and exports it to render passes through `makeFrameServices()`.
- It is a safety wrapper, not a new rendering layer: it exists to enforce scene-before-renderer teardown while scene-owned GPU payloads still have a live device/VMA allocator behind them.

Useful reality checks:

- [../../src/app/exportModule.ixx](../../src/app/exportModule.ixx), [../../src/app/nrAppSession.ixx](../../src/app/nrAppSession.ixx), and [../../src/app/nrAppCamera.ixx](../../src/app/nrAppCamera.ixx) provide the application-facing lifetime wrapper plus camera/input encapsulation.
- [../../src/app/nrAppUi.ixx](../../src/app/nrAppUi.ixx) is the app-owned Dear ImGui system wrapper used by render-pass-facing UI.
- [../../src/main.cpp](../../src/main.cpp) is the scene-driven viewer loop where `NormalBuffer` feeds `Present.sourceColor` and `Ui` feeds `Present.uiBuffer`, while `nr::app::AppSession` initializes both the app camera and the UI system.
- [../../src/extern/CMakeLists.txt](../../src/extern/CMakeLists.txt), the `dependency*.ixx` modules under [../../src/extern](../../src/extern), [../../src/extern/exportDependency.ixx](../../src/extern/exportDependency.ixx), and [../../src/extern/nsightGraphicsSdkBridge.h](../../src/extern/nsightGraphicsSdkBridge.h) are the current source-of-truth for the centralized third-party module boundary used by the LLVM/Ninja build path.
- [../../test/smoke/app/embeddedTriangle.cpp](../../test/smoke/app/embeddedTriangle.cpp) is the renderer-only window loop where `EmbeddedTriangle` feeds `Present.sourceColor` and `Ui` feeds `Present.uiBuffer`, using the same `nr::app::AppSession` camera and UI wrapper with the default camera path.
- [../../test/smoke/app/normalBufferUiSmoke.cpp](../../test/smoke/app/normalBufferUiSmoke.cpp) is the current smoke path that validates the `NormalBuffer + Ui -> Present` integration, non-empty ImGui draw data, and the runtime normal-buffer cull toggle.
