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
- Slang

Current boundary notes:

- Windows + Vulkan + RTX-class hardware are hard assumptions.
- Command invocation should stay on Vulkan-Hpp RAII member functions instead of project-local dispatch tables.
- Public command-recording helper interfaces in `nr.rhi` (for example `bindResourcesToCommandBuffer`, `pushConstantsToCommandBuffer`, and `ops::ScopedRendering`) take `const vk::raii::CommandBuffer&` as the primary boundary type.
- `rhi` is the execution layer, not the content-organization layer.

Entry points:

- Module aggregation: [../../src/rhi/exportModule.ixx](../../src/rhi/exportModule.ixx)
- Device and frame lifetime: [../../src/rhi/nrDevice.ixx](../../src/rhi/nrDevice.ixx)
- RAII resources: [../../src/rhi/nrResource.ixx](../../src/rhi/nrResource.ixx)
- Descriptor and pipeline services: [../../src/rhi/nrDescriptor.ixx](../../src/rhi/nrDescriptor.ixx), [../../src/rhi/nrPipeline.ixx](../../src/rhi/nrPipeline.ixx)
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
- input-driven free-camera control remains outside `scene`
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

`Renderer::installGraph(spec)` installs long-lived nodes once -> each `renderFrame(input)` begins the device frame -> optionally drives `scene` extraction and bridge building (with optional app-side camera override) -> builds the graph -> compiles -> prepares -> executes -> presents

Boundary notes:

- the preferred scene-facing input is `SceneBridgeFrame`
- renderer can use scene-resolved camera data or an optional app/viewer camera override
- when camera override is present, scene extraction uses `customFrustum` and bridge frame constants come from override data

Entry points:

- Runtime entry: [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx)
- Viewer camera runtime module: [../../src/renderer/nrViewerCamera.ixx](../../src/renderer/nrViewerCamera.ixx)
- Graph types: [../../src/renderer/nrRenderGraphType.ixx](../../src/renderer/nrRenderGraphType.ixx)
- Builder, compiler, executor: [../../src/renderer/nrRenderGraphBuilder.ixx](../../src/renderer/nrRenderGraphBuilder.ixx), [../../src/renderer/nrRenderGraphCompiler.ixx](../../src/renderer/nrRenderGraphCompiler.ixx), [../../src/renderer/nrRenderGraphExecutor.ixx](../../src/renderer/nrRenderGraphExecutor.ixx)
- Terminology note: [../../src/renderer/README.md](../../src/renderer/README.md)

## 6. `renderpasses`

`nr.renderPasses` is the feature-node implementation layer on top of the renderer contract.

Current built-in nodes:

- `EmbeddedTriangleNode`
- `NormalViewNode`
- `PresentNode`

It owns:

- concrete `NodeRuntime` implementations
- pass intent declaration
- pass record callbacks that use `rhi` services

It does not own:

- scene lifetime
- render-graph core orchestration
- asset import

Current boundary notes:

- nodes consume `NodeBuildContext` and `NodeFrameParameters`
- `EmbeddedTriangleNode` is the scene-less graphics demo path that records a single triangle draw and consumes CPU camera uniforms.
- `PresentNode` is the copy-to-swapchain path
- `NormalViewNode` consumes bridge draw geometry contracts and records real scene mesh draw calls (indexed/non-indexed) with world-space normal visualization
- node record callbacks should route command recording through `PassRecordContext::commandBuffer` as a RAII `vk::raii::CommandBuffer` reference when calling `nr.rhi` command helpers.

Entry points:

- Node type aliases: [../../src/renderPasses/nrNodeType.ixx](../../src/renderPasses/nrNodeType.ixx)
- NormalView node: [../../src/renderPasses/NormalView/nrNormalViewNode.ixx](../../src/renderPasses/NormalView/nrNormalViewNode.ixx)
- Present node: [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx)
- Module note: [../../src/renderPasses/README.md](../../src/renderPasses/README.md)
- Current execution plan: [../normal_view_camera_three_phase_plan.md](../normal_view_camera_three_phase_plan.md)

## 7. `Overall`

The current main chain is:

external asset files  
-> `nr.load` produces `SceneAsset`  
-> `nr.scene` bridges, registers, instantiates, uploads, and extracts runtime packets  
-> `nr.scene` builds `SceneBridgeFrame`  
-> `nr.renderer` builds and executes the installed graph  
-> `nr.renderPasses` records concrete feature work  
-> `nr.rhi` executes Vulkan and present work

Useful reality checks:

- [../../src/main.cpp](../../src/main.cpp) is the renderer-only `EmbeddedTriangle -> Present` window loop with viewer-camera controls.
- [../../test/app/rasterNormalViewer.cpp](../../test/app/rasterNormalViewer.cpp) provides both default smoke mode and explicit `--interactive` camera-control mode.
- default `rasterNormalViewer` behavior remains finite smoke execution for `ctest`, while `--interactive` runs a user-driven loop.
