# Agent Global Architecture Context

This is a high-density global architecture context for agents. It only describes module responsibilities, primary data flow, dependency frameworks, and current boundaries. It does not duplicate coding rules. All coding constraints, style rules, and agent behavior requirements are maintained in [AGENTS.md](../../AGENTS.md).

For concrete interfaces, enums, records, comments, and implementation details, follow the linked code files and topic documents instead of expanding this document into an implementation manual.

## 1. `rhi`

`nr.rhi` is the Vulkan execution substrate of the project and the host-side shader ABI boundary. It owns device and swapchain lifetime, RAII GPU resources, descriptor and pipeline layout infrastructure, command recording helpers, synchronization, upload/readback utilities, and Slang compilation/reflection services.

Its meaning to upper layers is:

- `scene` uses it for upload, residency, and deferred retirement.
- `renderer` and `renderPasses` use it to create pipelines, resolve descriptor layouts, and record graphics/compute/copy/ray tracing commands.
- It does not understand scene templates, ECS semantics, render graph topology, or pass-level business intent.

Current dependency frameworks:

- Vulkan-Hpp RAII
- VMA
- GLFW for window and surface bootstrap
- Slang for shader compilation, module organization, and reflection

Current boundary decisions:

- `rhi` is the execution and ABI layer, not the content-organization layer.
- Windows + Vulkan + RTX-class hardware are hard assumptions; no compatibility path is expected for non-target platforms.
- Command invocation should center on Vulkan-Hpp RAII member functions, not project-local dispatch tables.
- Shader binding capture is cursor-centric: nodes record descriptor/push bindings through `ShaderCursor` snapshots, and execute-time descriptor/push replay is handled through RHI helper APIs rather than per-node writer objects.

Detail entry points:

- Module aggregation: [../../src/rhi/exportModule.ixx](../../src/rhi/exportModule.ixx)
- Device and frame lifetime: [../../src/rhi/nrDevice.ixx](../../src/rhi/nrDevice.ixx)
- RAII GPU resources: [../../src/rhi/nrResource.ixx](../../src/rhi/nrResource.ixx)
- Slang service: [../../src/rhi/nrSlang.ixx](../../src/rhi/nrSlang.ixx)
- Descriptor and pipeline details: [../../src/rhi/nrDescriptor.ixx](../../src/rhi/nrDescriptor.ixx), [../../src/rhi/nrPipeline.ixx](../../src/rhi/nrPipeline.ixx)
- Topic documents: [../rhi_command_execution_strategy.md](../rhi_command_execution_strategy.md), [../slang_bindingtype_descriptor_mapping.md](../slang_bindingtype_descriptor_mapping.md)

## 2. `load`

`nr.load` is the file-import and texture-decode front end. It converts external asset formats into a unified `nr::load::SceneAsset`, but it does not own ECS state, canonical resource registries, GPU upload, or runtime rendering orchestration.

The primary data flow is:

`SceneLoadRequest` -> importer backend dispatch -> Assimp scene import -> texture discovery and multithreaded decode -> `SceneAsset`

Its current outputs are mainly:

- authoring data for nodes, meshes, materials, textures, cameras, and lights
- import error structures and import statistics
- importer registry and backend dispatch
- texture decode helpers

Current dependency frameworks:

- Assimp
- stb_image
- libjpeg-turbo

Current boundary decisions:

- `load` produces import results, not runtime objects.
- `SceneAsset` is input to the scene bridge and should not be consumed as a long-lived runtime source by `renderer` or `rhi`.
- Deeper normalization, validation, and handle allocation belong to `scene` and `resource`, not `load`.

Detail entry points:

- Load data model: [../../src/load/nrLoadType.ixx](../../src/load/nrLoadType.ixx)
- Backend dispatch: [../../src/load/nrLoadBackend.ixx](../../src/load/nrLoadBackend.ixx)
- Default loader entry: [../../src/load/nrLoadLoader.ixx](../../src/load/nrLoadLoader.ixx)
- Assimp backend: [../../src/load/nrLoadAssimp.ixx](../../src/load/nrLoadAssimp.ixx)
- Texture decode path: [../../src/load/nrLoadDecode.ixx](../../src/load/nrLoadDecode.ixx)

## 3. `resources`

`nr.resource` is the CPU-side canonical resource data model. It defines value types, typed handles, lightweight math/geometry helpers, and local validation/normalization methods. It is the data language of scene registries, but it does not own ECS state or GPU handles.

Its position in the global architecture is:

- `load` converts external formats into authoring data.
- `scene` bridges that authoring data into `nr.resource::*`.
- `scene` then uses typed handles to manage registries, instance bindings, and GPU versions.
- `renderer` and `renderPasses` usually see handles or bridged runtime views instead of directly operating on large canonical CPU objects.

Current boundary decisions:

- `nr.resource` is a data layer, not an orchestration layer.
- It should remain focused on value semantics and validation, not import, ECS, or GPU lifetime.
- The handle family is the stable cross-module reference vocabulary, and registry families should align with it.

Detail entry points:

- Module export entry: [../../src/resource/exportModule.ixx](../../src/resource/exportModule.ixx)
- Detailed topic document: [../resource_module_architecture.md](../resource_module_architecture.md)

## 4. `scene`

`nr.scene` is the main integration hub of the current project. It sits between `load`, `resource`, `rhi`, and `renderer`. It owns canonical key planning, resource registries, template/instance lifetime, the Flecs ECS world, GPU upload and residency state, and selector-driven packet extraction.

The primary data flow is:

`nr::load::SceneAsset` -> `SceneBridge` builds canonical keys and a bridge plan -> `nr.resource::*` registration -> template prefab tree -> runtime ECS instances -> `beginFrame/uploadPending/updateSimulation/extractPackets` -> `ScenePacketSet` -> `SceneRenderBridge` -> `SceneBridgeFrame`

The most important facts for upper layers are:

- `scene` is not view-first; it is selector/profile-first.
- Runtime packet extraction is defined by domain + selection + optional visibility filter, not by a multi-view render-list API.
- The preferred `renderer` input boundary is `SceneBridgeFrame`, not direct access to internal scene registries or Flecs queries.

Current dependency frameworks:

- Flecs
- GLM
- `nr.resource`
- `nr.rhi`

Current boundary decisions:

- `scene` owns runtime organization and GPU lifetime, not disk I/O.
- It is the module closest to "runtime world state" in the rendering stack.
- The currently closed main path focuses on static mesh/material/texture/camera/light integration; skeleton, animation, and particle integration remain future work.

Detail entry points:

- Main Scene implementation: [../../src/scene/nrScene.ixx](../../src/scene/nrScene.ixx)
- Public types, packet types, and bridge types: [../../src/scene/nrSceneType.ixx](../../src/scene/nrSceneType.ixx)
- Canonical key and render bridge logic: [../../src/scene/nrSceneBridge.ixx](../../src/scene/nrSceneBridge.ixx)
- Topic document: [../scene_module_flecs_architecture.md](../scene_module_flecs_architecture.md)

## 5. `renderer`

`nr.renderer` is the rendering-runtime orchestration layer. It owns the installed graph, node lifecycle, frame-graph build/compile/prepare/execute/present flow, and cross-queue submit structure, but it does not own scene asset lifetime.

The current primary flow is:

`Renderer::installGraph(spec)` installs a long-lived graph once -> each frame `renderFrame(input)` optionally drives the scene path -> installed node runtimes generate the frame graph -> compile -> prepare -> execute -> present

Its responsibilities focus on:

- node-level queue constraints
- node metadata shape is name/ports/queue only (no node-kind field in runtime or graph node descriptors)
- graph resource and pass descriptions
- per-pass registration through `addPass(intentList, name, executeLambda)`
- compiler, executor, and submit-boundary orchestration
- injecting `SceneBridgeFrame` into node build each frame
- owning renderer-level shader service configuration during initialization (`Renderer::initialize()`)
- threading pass debug names into execute-time command-buffer debug labels for capture/profiling

Current boundary decisions:

- `renderer` is an orchestration layer, not a scene registry.
- Graph topology is long-lived, and nodes are long-lived runtime objects rather than per-frame scripts.
- The stable boundary between scene and render passes should be `SceneBridgeFrame` whenever possible, instead of leaking scene internals into node implementations.

Detail entry points:

- Renderer runtime entry: [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx)
- Graph types: [../../src/renderer/nrRenderGraphType.ixx](../../src/renderer/nrRenderGraphType.ixx)
- Builder, compiler, and executor: [../../src/renderer/nrRenderGraphBuilder.ixx](../../src/renderer/nrRenderGraphBuilder.ixx), [../../src/renderer/nrRenderGraphCompiler.ixx](../../src/renderer/nrRenderGraphCompiler.ixx), [../../src/renderer/nrRenderGraphExecutor.ixx](../../src/renderer/nrRenderGraphExecutor.ixx)
- Short terminology note: [../../src/renderer/README.md](../../src/renderer/README.md)
- Topic document: [../renderer_renderpasses_two_phase_todo.md](../renderer_renderpasses_two_phase_todo.md)

## 6. `renderpasses`

`nr.renderPasses` is the implementation layer for concrete `NodeRuntime` objects. It is no longer a script-style pass list assembler. It is a collection of feature nodes built on top of the renderer contract. The current built-in nodes are centered on `NormalViewNode` and `PresentNode`.

Its role in the global architecture is:

- receive `NodeBuildContext` and `NodeFrameParameters` from `renderer`
- read upstream node ports and optional `SceneBridgeFrame`
- declare graph resources and pass intents, then register execute lambdas through `addPass(...)`
- use `rhi` pipeline, descriptor, and command facilities to record actual work
- capture scene draw packets during build, while preparing per-draw push-constant payloads inside pass record callbacks

Current boundary decisions:

- `renderPasses` does not own scene lifetime, asset import, or a global cache system.
- It is the concrete feature-node layer, not a new graph or runtime core.
- `PresentNode` is a single-pass copy node (`sourceColor -> swapchain`) with no compute present transform or intermediate image.
- Node inputs and outputs should remain ports and per-frame parameters instead of leaking internal graph-handle details across module boundaries.

Detail entry points:

- Node type alias layer: [../../src/renderPasses/nrNodeType.ixx](../../src/renderPasses/nrNodeType.ixx)
- NormalView node: [../../src/renderPasses/NormalView/nrNormalViewNode.ixx](../../src/renderPasses/NormalView/nrNormalViewNode.ixx)
- Present node: [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx)
- Module note: [../../src/renderPasses/README.md](../../src/renderPasses/README.md)
- Topic document: [../renderer_renderpasses_two_phase_todo.md](../renderer_renderpasses_two_phase_todo.md)

## 7. `Overall`

The current project can be read as the following main chain:

external asset files  
-> `nr.load` produces `SceneAsset`  
-> `nr.scene` performs canonical bridging, resource registration, template/instance management, and GPU residency  
-> `nr.scene` extracts `ScenePacketSet` and builds `SceneBridgeFrame`  
-> `nr.renderer` drives frame build/compile/prepare/execute through an installed graph  
-> `nr.renderPasses` provides concrete feature nodes  
-> `nr.rhi` executes Vulkan/Slang/descriptor/pipeline/queue/swapchain work  
-> present

Dependency frameworks should also be read by layer:

- Third-party non-module dependencies enter the project only through [../../src/extern/exportDependency.ixx](../../src/extern/exportDependency.ixx).
- `rhi` sits directly on Vulkan-Hpp RAII, VMA, Slang, and GLFW.
- `load` sits directly on Assimp, stb_image, and libjpeg-turbo.
- `scene` sits directly on Flecs while depending upward on `load` and downward on `resource` + `rhi`.
- `renderer` and `renderPasses` should depend on stable runtime contracts instead of reaching back into import-layer details.

The most useful workflow for an agent is:

1. Use this document to identify which layer owns the problem.
2. Read the topic document for that layer.
3. Then drill into the layer's interface files, type files, and node implementations.

If you need to verify the real program entry flow quickly, read [../../src/main.cpp](../../src/main.cpp). The current sample graph is `NormalView -> explicit submit boundary -> Present`, which is a compact example of the scene/renderer/renderpasses/rhi layering.
