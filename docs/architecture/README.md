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
- RHI device creation requires graphics, compute, and a dedicated copy/transfer queue family; the frame-present policy is compute-final, and the selected compute queue family must support surface presentation.
- Command invocation should stay on Vulkan-Hpp RAII member functions instead of project-local dispatch tables.
- RHI command-buffer helper APIs expose `const vk::raii::CommandBuffer&`; raw `vk::CommandBuffer` handles stay internal implementation details.
- Public command-recording helper interfaces in `nr.rhi` (for example `bindResourcesToCommandBuffer`, `pushConstantsToCommandBuffer`, and `ops::ScopedRendering`) take `const vk::raii::CommandBuffer&` as the primary boundary type.
- `nr.rhi` exposes descriptor-indexing, buffer-device-address, and Vulkan 1.4 capability/property snapshots from `Device`, and its descriptor/pipeline layer supports runtime-sized descriptor arrays driven by Slang reflection with a semantic multi-set ABI for runtime arrays.
- RHI copy helpers record Vulkan-Hpp copy commands 2 while keeping narrow adapters for existing copy-region structs.
- `PipelineState` retains the source `SlangProgram` so reflection-backed cursor access remains valid after pipeline creation.
- `rhi` is the execution layer, not the content-organization layer.

Entry points:

- Module aggregation: [../../src/rhi/exportModule.ixx](../../src/rhi/exportModule.ixx)
- Device and frame lifetime: [../../src/rhi/nrDevice.ixx](../../src/rhi/nrDevice.ixx)
- RAII resources: [../../src/rhi/nrResource.ixx](../../src/rhi/nrResource.ixx)
- Descriptor and pipeline services: [../../src/rhi/nrDescriptor.ixx](../../src/rhi/nrDescriptor.ixx), [../../src/rhi/nrPipeline.ixx](../../src/rhi/nrPipeline.ixx)
- Topic docs: [../rhi_command_execution_strategy.md](../rhi_command_execution_strategy.md), [../slang_bindingtype_descriptor_mapping.md](../slang_bindingtype_descriptor_mapping.md), [../rhi_vulkan14_modernization_audit.md](../rhi_vulkan14_modernization_audit.md), [../rhi_rt_completion_audit.md](../rhi_rt_completion_audit.md), [../rhi_upload_readback_limits_audit.md](../rhi_upload_readback_limits_audit.md)

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

- Consumers reach legacy third-party declarations through the shared named C++ module `dependency`, implemented by [`src/extern/exportDependency.ixx`](../../src/extern/exportDependency.ixx).
- External headers are confined to the `src/extern` boundary; internal project source imports `dependency` instead of including third-party headers directly.

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

`Renderer::installGraph(spec)` installs long-lived nodes once -> each `renderFrame(input)` begins the device frame -> optionally drives `scene` extraction and bridge building (with optional app-side camera override) -> forwards optional per-frame service state through `FrameServices` -> builds the graph -> compiles -> prepares -> executes -> presents

Boundary notes:

- the preferred scene-facing input is `SceneBridgeFrame`
- renderer can use scene-resolved camera data or an optional app/viewer camera override
- when camera override is present, scene extraction uses `customFrustum` and bridge frame constants come from override data
- `FrameServices` is the current renderer-side sideband for app-owned per-frame services that render passes may consume without creating a direct app-layer dependency on renderer internals
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
- pass record callbacks that use `rhi` services

It does not own:

- scene lifetime
- render-graph core orchestration
- asset import

Current boundary notes:

- nodes consume `NodeBuildContext` and `NodeFrameParameters`
- `EmbeddedTriangleNode` is the scene-less graphics demo path that records a single triangle draw and consumes CPU camera uniforms.
- `PresentNode` is the final compute conversion and composition path. It converts the scene color buffer into swapchain-ready output, applies flip/gamma/channel conversion, and alpha-composites the standalone UI buffer before copy-to-swapchain
- `NormalBufferNode` consumes bridge draw geometry contracts and records real scene mesh draw calls (indexed/non-indexed) with world-space normal visualization
- `NormalBufferNode` now also consumes `nr::app::UiSystem` through `NodeFrameParameters::frameServices` to expose runtime controls such as FPS display, frame time, and front/back-face cull switching
- `UiNode` is the Dear ImGui overlay build pass. It finalizes the app-owned UI frame, consumes draw data emitted earlier in the graph, honors Dear ImGui 1.92.6 `ImTextureData` requests from the vcpkg dependency, manages UI textures through a descriptor-indexed runtime sampled-image array, selects the sampled texture per draw through push constants, and renders the overlay into its own transparent `uiBuffer` for later composition in `PresentNode`
- node record callbacks should route command recording through `PassRecordContext::commandBuffer` as a RAII `vk::raii::CommandBuffer` reference when calling `nr.rhi` command helpers.

Entry points:

- Node type aliases: [../../src/renderPasses/nrNodeType.ixx](../../src/renderPasses/nrNodeType.ixx)
- NormalBuffer node: [../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx](../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx)
- Ui node: [../../src/renderPasses/Ui/nrUiNode.ixx](../../src/renderPasses/Ui/nrUiNode.ixx)
- Present node: [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx)
- Module note: [../../src/renderPasses/README.md](../../src/renderPasses/README.md)
- UI runtime handoff: [../ui_runtime_integration_plan.md](../ui_runtime_integration_plan.md)

## 7. `Overall`

The current main chain is:

external asset files  
-> `nr.load` produces `SceneAsset`  
-> `nr.scene` bridges, registers, instantiates, uploads, and extracts runtime packets  
-> `nr.scene` builds `SceneBridgeFrame`  
-> `nr.renderer` builds and executes the installed graph  
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
- [../../src/extern/CMakeLists.txt](../../src/extern/CMakeLists.txt) and [../../src/extern/exportDependency.ixx](../../src/extern/exportDependency.ixx) are the current source-of-truth for the centralized third-party module boundary used by the LLVM/Ninja build path.
- [../../test/app/embeddedTriangle.cpp](../../test/app/embeddedTriangle.cpp) is the renderer-only window loop where `EmbeddedTriangle` feeds `Present.sourceColor` and `Ui` feeds `Present.uiBuffer`, using the same `nr::app::AppSession` camera and UI wrapper with the default camera path.
- [../../test/app/normalBufferUiSmoke.cpp](../../test/app/normalBufferUiSmoke.cpp) is the current headless smoke path that validates the `NormalBuffer + Ui -> Present` integration, non-empty ImGui draw data, and the runtime normal-buffer cull toggle.
