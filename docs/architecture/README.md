# Agent Global Architecture Context

This document is the project-wide architecture index for agents. It stays high-level on purpose:

- it describes current module responsibilities and stable runtime boundaries
- it points to the code and topic documents that own detail
- it does not duplicate coding policy from [AGENTS.md](../../AGENTS.md)
- it describes current reality, not abandoned plans

The unified OptionSystem vertical slices are implemented. The stable boundaries are
summarized below and specified in
[Agent Interaction and Offline Automation Design](../agent_interaction_and_automation_design.md).
There is no parallel legacy mutation path.

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
- NVIDIA NGX DLSS 310.7.0 through the typed `dependency.dlss` boundary and the project-owned `nr_dlss_bridge` C ABI. NGX headers, C++ helpers, static loader, parameters, features, and shutdown remain inside the MSVC-built DLL; both MSVC and clang++/libc++ hosts dynamically consume the same function-table contract. LLVM builds validate and deploy the Git-tracked Release bridge plus the submodule's Release feature DLL.

Current boundary notes:

- Windows + Vulkan + RTX-class hardware are hard assumptions.
- RHI physical-device selection and device creation require graphics, compute, and a dedicated physical copy/transfer queue family plus `VK_KHR_maintenance8`, `VK_KHR_maintenance9`, and `VK_EXT_full_screen_exclusive`; the frame-present policy is compute-final, and the selected compute queue family must support surface presentation.
- Shader execution reordering is a hard target capability: `Device` enables `VK_EXT_ray_tracing_invocation_reorder`, chains the matching EXT feature structure, and fails fast unless the device property reports actual reordering mode. `ShaderService` declares `spvShaderInvocationReorderEXT` in the common Slang target so SER shaders lower directly to the ratified EXT SPIR-V instructions without an implicit-capability warning.
- `PresentationContext` owns presentation-facing GLFW window state: input polling, framebuffer availability checks, swapchain acquire/present state, selected swapchain format/color-space, and requested exclusive fullscreen transitions. `Device::beginFrame()` waits and resets only the current three-frame `FrameSlot`; `Device::acquireFrameImage()` borrows that slot's image-available semaphore. Ordinary graphs call it later at the renderer's explicit acquire boundary, while graphs with a non-empty frame-resolution resolver call it immediately after `beginFrame()` so any acquire-time recreation precedes resolution planning; the executor then reuses that image at the compiled boundary. `Surface` switches the GLFW window monitor and restores saved windowed bounds; `PresentationContext` creates application-controlled fullscreen-exclusive swapchains, queries the current Win32 monitor capability, and releases/re-acquires exclusive mode around swapchain recreation. The viewer loop skips rendering while GLFW reports a zero-sized framebuffer.
- Swapchain format selection only accepts the project's three supported output color spaces, matched by exact format+color-space pair in priority order (scRGB extended-linear, then HDR10 ST2084, then SDR sRGB `*_SRGB`/`*_UNORM`); it fails fast when none of those pairs is reported by Vulkan WSI instead of falling back to an unsupported gamut. HDR10 presentation metadata is applied when `VK_EXT_hdr_metadata` is available.
- Command invocation should stay on Vulkan-Hpp RAII member functions instead of project-local dispatch tables.
- RHI command-buffer helper APIs expose `const vk::raii::CommandBuffer&`; raw `vk::CommandBuffer` handles stay internal implementation details.
- `CommandBatch` submit paths are one-shot consumption boundaries: `Device::submitFrameBatch`, `Device::submitFrame`, `Device::endFrame`, and `GpuQueue::submit(CommandBatch&&)` require an rvalue batch so submit call sites cannot accidentally reuse a batch after present or synchronization metadata has been appended. `CommandBatch` stores submit-ready synchronization arrays, so queue submission builds only stack `vk::SubmitInfo2` / frame-boundary views instead of per-submit packet vectors.
- RHI device creation optionally enables `VK_EXT_frame_boundary` when a graphics debugger exposes it, and `CommandBatch` can attach frame-boundary submit metadata through `vk::SubmitInfo2::pNext` without making the debugger extension a required runtime capability.
- RHI owns env-driven Nsight Graphics SDK activity setup through `NsightGraphicsFrameHelper`. `Device` calls the helper at Vulkan lifecycle points while the helper reads `NR_NSIGHT_GRAPHICS_ACTIVITY`, `NR_NSIGHT_GRAPHICS_FRAME`, `NR_NSIGHT_GRAPHICS_FRAMES`, `NR_NSIGHT_GRAPHICS_OUTPUT_DIR`, and `NR_NSIGHT_GRAPHICS_INSTALL_DIR`, injects the selected capture-or-trace activity before Vulkan instance creation, initializes it after queues are ready, and emits SDK frame boundaries from the compute-present path with the current swapchain image.
- Public command-recording helper interfaces in `nr.rhi` (for example `updateResourcesForBindingSnapshot`, `bindPreparedResourcesToCommandBuffer`, `pushConstantsToCommandBuffer`, and `ops::ScopedRendering`) take project-owned typed inputs; command-recording helpers take `const vk::raii::CommandBuffer&` as the primary boundary type.
- `nr.rhi` exposes descriptor-indexing, buffer-device-address, ray-tracing, and Vulkan 1.4 capability/property snapshots from `Device`, and its descriptor/pipeline layer supports runtime-sized descriptor arrays driven by Slang reflection with a semantic multi-set ABI for sampler, sampled-image, storage-image, buffer, and acceleration-structure arrays.
- `nr.rhi:dlss` owns the move-only Ray Reconstruction feature wrapper and the device-shared lazy context. `Device` merges RR-specific instance/device extension requirements before Vulkan creation, while `dependency.dlss` loads the bridge from the absolute executable directory and keeps the DLL resident for the process lifetime. The bridge serializes NGX calls and owns every NGX allocation. `DlssRayReconstructionNode::shutdown` waits for device idle before releasing its feature; each feature retains its shared context until its bridge-owned NGX handle and parameters are released, and `Device` drops its owned context before Vulkan device destruction.
- `ShaderService` treats one shader-root-relative file as one compile unit and rejects roots that do not expose exactly one Slang entry point. Compile requests identify the source path and optional entry-local link-time assignments; they never select an entry point by name. The service serializes shared-session module load, specialization, composition, link, reflection, and authoritative entry-point hashing, then sends cache misses to a dedicated backend pool whose default size resolves dynamically from host hardware concurrency up to `nr::maxThreads`. `.slang-module` files persist frontend state, while project-owned content-addressed SPIR-V artifacts persist Slang target output across processes. Runtime initialization and the CMake shader checker use the same batch compiler; batch statistics separate frontend, backend, and total elapsed time alongside hit/compile counts. Descriptor and push-constant ABI changes are rejected by `ShaderDescriptorLayout` ABI signature validation before variant PSO creation.
- Multi-file pipelines use one canonical reflection program for layout/cursor ownership: graphics uses the first ordered program, compute uses its sole program, and ray tracing supplies an explicit reflection program. That program must expose the complete global resource, push-constant, and root-field ABI required by all physical stages. `PipelineService` validates every other stage's physical descriptor/push layout against the canonical root before PSO creation, while shader contract tests pin the root fields used by renderer bindings.
- `PipelineService` owns the Vulkan-Hpp RAII `vk::raii::PipelineCache` used by graphics, compute, and ray-tracing PSO creation. `RendererCreateInfo::pipelineCache` can provide a persistence directory; when configured, the service loads the cache blob during device binding and saves it through `PipelineCache::getData()` only after all host PSO jobs and GPU work are idle. The cache keeps Vulkan's default internal synchronization so concurrent creation jobs can share it without a project-wide serialization lock.
- `PipelineService` submits every graphics, compute, and ray-tracing PSO to one reusable host build pool. Its worker count uses the project-wide automatic hardware-concurrency policy with no separate PSO cap. Node initialization retains move-only `PipelineBuild<T>` futures, allowing the renderer to enqueue every static PSO before joining the batch; reflection/layout preparation remains on the collecting thread because shared Slang reflection objects are non-reentrant. RT jobs additionally request `VK_KHR_deferred_host_operations`; when the driver defers work, the calling build worker participates in the join while a separate pool supplies the remaining useful host concurrency. Driver-reported concurrency is clamped only to available host/project capacity to avoid oversubscription, and the completed raw handle enters Vulkan-Hpp RAII only after the final result succeeds.
- RHI ray-tracing helpers cover BLAS/TLAS build recording, multi-geometry BLAS input, AS copy/compaction/serialization/query operations, SBT record payload packing, trace/indirect-trace recording, maintenance1 indirect2 dispatch, and RT-specific sync2 stage/access helpers.
- RT PSO creation receives one `RayTracingProgramAssemblyDesc`: its named groups reference logical stage names, and the created `RayTracingPipeline` retains the name-to-group-index mapping used when building SBT records. Pipeline-state options remain separate in `RayTracingPipelineDesc`.
- RHI copy helpers record Vulkan-Hpp copy commands 2 while keeping narrow adapters for existing copy-region structs.
- `Buffer::writeMappedAndFlush(...)` is the RHI helper for direct CPU writes to mapped GPU-visible buffers; GPU-only buffer/image uploads use the device-level `UploadReadbackContext` staging ring, which defaults both upload and readback rings to 128 MiB and exposes upload timeline polling for higher layers. Explicit queue-family ownership barriers opt into maintenance8 all-stage scope semantics so their producer/consumer stages can participate in precise semaphore synchronization. Maintenance9-backed transfer policy omits explicit queue-family ownership transfers when Vulkan guarantees cross-family content preservation, while keeping semaphore queue waits and required image layout transitions. Buffer uploads can target concurrent-sharing buffers without a queue-ownership acquire when the destination is intentionally shared across transfer, graphics, and compute queues.
- Linear image uploads larger than the upload ring are split into array-layer/depth-slice/row chunks. The first transfer submit performs the source acquire/layout transition, the final submit performs the destination release, and queue order plus the upload timeline preserve the intermediate copies; an 8192x4096 RGBA16F payload therefore crosses the default 128 MiB ring as two 2048-row submissions instead of requiring a 256 MiB staging allocation.
- `PipelineState` retains the source single-entry Slang program so reflection-backed cursor access remains valid after pipeline creation.
- `rhi` is the execution layer, not the content-organization layer.

Entry points:

- Module aggregation: [../../src/rhi/exportModule.ixx](../../src/rhi/exportModule.ixx)
- Device and frame lifetime: [../../src/rhi/nrDevice.ixx](../../src/rhi/nrDevice.ixx)
- Window surface and presentation context: [../../src/rhi/nrSurface.ixx](../../src/rhi/nrSurface.ixx), [../../src/rhi/nrSwapchain.ixx](../../src/rhi/nrSwapchain.ixx)
- Nsight Graphics SDK frame helper: [../../src/rhi/nrNsightGraphics.ixx](../../src/rhi/nrNsightGraphics.ixx)
- DLSS NGX RAII and compiler-ABI boundary: [../../src/rhi/nrDlss.ixx](../../src/rhi/nrDlss.ixx), [../../src/extern/dependencyDlss.ixx](../../src/extern/dependencyDlss.ixx), [../../src/extern/dlssBridge/include/nrDlssBridge.h](../../src/extern/dlssBridge/include/nrDlssBridge.h)
- RAII resources: [../../src/rhi/nrResource.ixx](../../src/rhi/nrResource.ixx)
- Descriptor, shader, and pipeline services: [../../src/rhi/nrDescriptor.ixx](../../src/rhi/nrDescriptor.ixx), [../../src/rhi/nrSlang.ixx](../../src/rhi/nrSlang.ixx), [../../src/rhi/nrPipeline.ixx](../../src/rhi/nrPipeline.ixx)
- Ray tracing helpers: [../../src/rhi/nrAccelerationStructure.ixx](../../src/rhi/nrAccelerationStructure.ixx), [../../src/rhi/nrRayTracing.ixx](../../src/rhi/nrRayTracing.ixx), [../../src/rhi/nrResourceOps.ixx](../../src/rhi/nrResourceOps.ixx)
- Topic docs: [../rhi_command_execution_strategy.md](../rhi_command_execution_strategy.md), [../slang_bindingtype_descriptor_mapping.md](../slang_bindingtype_descriptor_mapping.md)

## 2. `load`

`nr.load` is the file-import and texture-decode front end.

It owns:

- importer backend dispatch
- Assimp-based scene import
- texture discovery and decode
- strict single-part scanline OpenEXR environment decode
- construction of `nr::load::SceneAsset`

It does not own:

- runtime ECS state
- canonical CPU registries
- GPU residency
- renderer orchestration

Primary flow:

`SceneLoadRequest` -> backend dispatch -> import/decode -> `SceneAsset`

`ExrEnvironmentLoadRequest` -> OpenEXR FLOAT/HALF RGB(A) decode -> non-finite rejection and negative clamp -> peak-based linear scaling -> `nr::resource::EnvironmentMap` with RGBA16F bytes plus `radianceDecodeScale`

Material boundary facts:

- `MaterialTextureBinding` carries an enum `MaterialTextureSlotSemantic`, the original source semantic name for diagnostics, UV set 0 or 1, and an identity-default row-major 2x2 UV transform plus offset; Assimp raw texture types are mapped at the load boundary.
- Load keeps importer-specific authoring data, while the scene bridge is responsible for turning supported semantics into canonical resource material slots and optional PBR extension blocks.
- Assimp material import carries scalar transmission, IOR, and volume thickness into separate canonical extension blocks. Positive transmission with zero thickness selects thin transmission; positive thickness selects a volume dielectric boundary. The RT material compiler currently consumes IOR through the transmission layer only; IOR-only changes to base reflection are outside this implementation. Thickness textures and volume attenuation inputs remain diagnosed as unsupported because the runtime intentionally has no Beer absorption or volume-scattering state.
- Assimp-imported glTF/glb `TEXCOORD_0` and `TEXCOORD_1` values are normalized at the load boundary so `SceneAsset` UVs use glTF image-space V orientation before renderer/material sampling sees them. The Assimp overlay preserves the extension-level `KHR_texture_transform.texCoord` override, and load converts Assimp's UV-transform convention back to the canonical glTF affine before the scene bridge copies it into resource texture slots. Authored glTF tangents are preserved; when they are absent and tangent generation is requested, the load boundary uses MikkTSpace with the base-normal texture's effective UV set and affine transform, or the clearcoat-normal mapping when no base normal exists. Per-corner tangent discontinuities split vertices instead of overwriting a shared tangent. A material whose base and clearcoat normal textures use different effective UV mappings emits a warning because the current vertex ABI stores only one tangent frame.
- The project-owned Assimp 6.0.5 overlay recognizes and validates glTF `EXT_mesh_gpu_instancing` transform accessors, then lowers each extension node into a meshless hierarchy anchor plus one ordinary shared-mesh child node per instance. `TRANSLATION`, `ROTATION`, and `SCALE` become child-local transforms; application-specific attribute declarations remain only in Assimp's generic extension metadata, and their accessor values are not promoted into `SceneAsset`.
- Assimp light import preserves static punctual light authoring data for `directional`, `point`, and `spot` lights, including glTF `KHR_lights_punctual` photometric intensity, color, source range, and cone data when present; broader Assimp-only light kinds remain source data until the scene bridge decides whether to accept them.
- Environment EXRs are treated as latitude-longitude, scene-linear sRGB radiance. The loader accepts required unsampled `R/G/B` channels plus optional `A`, ignores source alpha and extra channels, writes alpha one, clamps negative radiance to zero, and scales peaks above 60000 before HALF conversion; the shader restores the scale independently from user intensity. Tiled, deep, multipart, UINT-channel, missing-RGB, incomplete, and non-finite inputs are rejected.

Current dependency frameworks:

- Assimp 6.0.5 (project-owned vcpkg overlay)
- stb_image
- libjpeg-turbo
- OpenEXR

Third-party boundary note:

- Consumers reach legacy third-party declarations through narrow named C++ modules such as `dependency.vulkan`, `dependency.vma`, `dependency.window`, `dependency.math`, `dependency.ui`, `dependency.assets`, `dependency.slang`, `dependency.ecs`, `dependency.nsight`, `dependency.json`, and `dependency.network`; [`src/extern/exportDependency.ixx`](../../src/extern/exportDependency.ixx) remains their compatibility umbrella. [`dependency.json`](../../src/extern/dependencyJson.ixx) is the repository-wide project JSON boundary backed by Boost.JSON, while `dependency.network` is networking-only. The generated `dependency.shaderShare` module intentionally stays outside that umbrella and is linked through the narrow `dependency_shader_share` provider only by direct shared-ABI consumers.
- External headers are confined to the `src/extern` boundary; internal project source imports the narrow `dependency.*` module it needs instead of including third-party headers directly.
- The bundled Slang submodule is configured and installed into the build tree as a local package before the `dependency` target links the imported `slang::slang` target. The lightweight [`src/extern/tools/nrShaderShareCodegen.cpp`](../../src/extern/tools/nrShaderShareCodegen.cpp) host tool synthesizes an in-memory Slang root from only the data declarations under [`shader/include/share`](../../shader/include/share), then emits the generated `dependency.shaderShare` module behind an independent content-stable stamp. The dedicated `dependency_shader_share` target compiles that module after `dependency.math` is available without gating unrelated `dependency` modules. For shared `enum : uint` types the same pass also emits `SlangEnumMeta`/`slangEnumLiteral` enumerator-name reflection so C++ can spell Slang enum literals from the translated enum (a codegen stopgap pending C++26 static reflection); see [`shader/SHADER_NAMING_AND_ORGANIZATION.md`](../../shader/SHADER_NAMING_AND_ORGANIZATION.md).
- Nsight Graphics SDK headers stay private to [`src/extern/nsightGraphicsSdkImpl.cpp`](../../src/extern/nsightGraphicsSdkImpl.cpp); engine modules use the exported `nr::platform` wrapper API from `dependency.nsight`.

Entry points:

- Data model: [../../src/load/nrLoadType.ixx](../../src/load/nrLoadType.ixx)
- Backend dispatch: [../../src/load/nrLoadBackend.ixx](../../src/load/nrLoadBackend.ixx)
- Loader entry: [../../src/load/nrLoadLoader.ixx](../../src/load/nrLoadLoader.ixx)
- Assimp bridge: [../../src/load/nrLoadAssimp.ixx](../../src/load/nrLoadAssimp.ixx)
- OpenEXR environment loader: [../../src/load/nrLoadExr.ixx](../../src/load/nrLoadExr.ixx), [../../src/load/nrLoadExr.cpp](../../src/load/nrLoadExr.cpp)

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
- `Material` stores metallic-roughness `MaterialCorePbr` data, optional clearcoat/sheen/transmission/IOR/volume-boundary/anisotropy blocks, and enum-indexed texture slots instead of string semantic slots; every texture slot carries its UV set plus an identity-default affine transform. Specular-glossiness authoring inputs are converted before entering `nr.resource`. IOR is kept as a separate canonical block even though the current RT compiler consumes it only for transmission-enabled materials.
- `Mesh` owns shared vertex/index arrays plus `MeshGeometry` ranges; each geometry is a material-mapped source primitive and future BLAS geometry
- `EnvironmentMap` is the CPU handoff record for one latitude-longitude linear-sRGB radiance texture. Its texture is fixed to mipless RGBA16F, while decode scale, intensity, and yaw remain explicit scalar parameters; it owns no RHI handle.
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
- `SceneRevisionSnapshot`, captured by value in every `ScenePacketSet`

Boundary notes:

- extraction is profile-first, not multi-view render-list-first
- viewport-dependent projection and frustum resolution already live here
- material import no longer classifies texture strings in the production scene bridge; it consumes the enum slot semantic emitted by `load`
- material import converts specular-glossiness factors to metallic-roughness and warns when unsupported spec/gloss texture baking would be required
- light import accepts `directional`, `point`, and `spot` only. It keeps the resource `LightAsset` as static authoring data with glTF photometric intensity and point/spot source range, attaches runtime `SceneLightBinding` components to template light entities, and emits active-instance light packets with world position and world -Z direction during extraction; unsupported light kinds such as ambient or area are warned and skipped before runtime packets are created.
- raster extraction fans out per mesh geometry and applies the selected camera/frustum profile, while ray tracing/TLAS extraction stays at node mesh instance granularity and is deliberately not camera/frustum culled; packet construction and the readiness predicates are implemented in [../../src/scene/nrScene.cpp](../../src/scene/nrScene.cpp)
- Assimp-lowered `EXT_mesh_gpu_instancing` children cross the load-to-scene bridge as ordinary nodes that retain one shared mesh identity. The existing RT path therefore reuses the per-mesh BLAS and emits one TLAS instance packet for each expanded child without a separate extension-specific scene or renderer contract.
- ray tracing/TLAS extraction uses the RT material readiness contract: resident meshes are required, invalid geometry material handles use the RT fallback material, and valid geometry material handles require resident material CPU data plus resident referenced material textures before the instance is extracted.
- RT extraction disables triangle-facing cull for an instance containing a positive-thickness transmissive boundary so material rays can observe both entry and exit faces. Because this is an instance-granularity Vulkan flag, unrelated single-sided geometries in the same mesh are made non-opaque and routed through the shared any-hit material policy, which ignores their back-face candidates while preserving double-sided and volume-boundary faces.
- `Scene` owns one nonzero identity and the authoritative global RT revision set. Template/instance lifecycle, simulation, successful mesh/material/texture residency completion, and the explicit `commitExternalMutation(...)` boundary advance domains through one declarative mutation policy. Handle generations, asset CPU/GPU versions, frame serials, and retirement tokens remain separate lifetime/version concepts.
- input-driven free-camera control remains outside `scene` and is currently wrapped by `nr.app` for application-style entry points
- staged scene GPU uploads remain `waitingGraphicsSync` while `UploadReadbackContext` owns the transfer work. `Scene` immediately chains each batch to a graphics submission that waits on the upload timeline and records any required destination acquire barriers; the CPU does not poll the transfer timeline. The submitted work retains only asset identity/version completion records, and only a successful graphics fence reap with no submitted geometry-atlas grow work remaining publishes `resident`, advances `gpuVersion`/resident revisions, and exposes the asset to extraction.
- mesh uploads append CPU vertex/index data into scene-owned GPU-only geometry atlas buffers via `UploadReadbackContext`; mesh GPU payloads store atlas slice metadata, while atlas growth copies the old prefix into larger scene buffers and retires old buffers after submitted copy work completes. Atlas buffers include acceleration-structure build-input and device-address usage and are created with transfer/graphics/compute queue-family visibility when those queues differ, so renderer AS nodes can build BLAS from resident scene geometry on graphics or compute.
- assets released while `waitingGraphicsSync` defer collection until their completion fence is reaped, so upload tickets and submitted GPU work cannot outlive their destination payloads. `Scene::~Scene()` is the final RAII boundary: with a live initialized `Device`, it waits for device idle before clearing scene-owned pending/submitted synchronization work and allowing GPU payload members to destruct. The referenced `Device` must therefore outlive the `Scene`.
- `Scene::tryGetAccelerationStructureMeshSemanticKey(...)` exposes the lightweight AS semantic key used by renderer AS nodes to decide whether cached per-mesh BLAS build descriptions need refresh. `Scene::tryGetAccelerationStructureMesh(...)` exposes the matching current resident mesh as atlas-backed AS build input: vertex/index bindings, geometry ranges, GPU version, opaque/non-opaque geometry flags, and mesh/material-derived RT instance flags for imported winding and double-sided culling. `Mesh::clockwiseFrontFace` is imported through the load-to-scene bridge; CCW meshes keep Vulkan RT's default facing and clockwise meshes set the triangle-facing flip. Vulkan determines RT facing in object space, so instance transforms do not alter this flag; mirrored transforms instead adjust the world-space tangent-frame handedness used by normal mapping. It is a query boundary, not a scene-owned RT build frame.
- `SceneRenderBridge` supports frame-constants override, frame-level geometry atlas bindings, per-draw geometry resolution, per-material raster-state resolution, and one material texture record containing enum-ordered 16-bit IDs plus the normal slot's UV set, affine transform, and scale so render passes can consume draw-ready atlas-backed geometry and sampling state through bridge contracts

Entry points:

- Main implementation: [../../src/scene/nrScene.ixx](../../src/scene/nrScene.ixx)
- Public types: [../../src/scene/nrSceneType.ixx](../../src/scene/nrSceneType.ixx)
- Bridge logic: [../../src/scene/nrSceneBridge.ixx](../../src/scene/nrSceneBridge.ixx)
- Packet extraction and RT/TLAS readiness implementation: [../../src/scene/nrScene.cpp](../../src/scene/nrScene.cpp)

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

Current OptionSystem boundary:

- the standalone `nroptions` target sits after the `nrutils` target and before
  `nrrenderer`;
  `AppSession`, not renderer or a node, owns the sole `OptionSystem`
- every `RendererFrameInput`, `NodeFrameParameters`, and frame-resolution resolver receives
  the same required immutable `OptionFrameSnapshot`; `FrameServices` does not expose the
  mutable system
- `NodeRuntime::declareOptions(...) const` contributes pure definitions before graph
  initialization; graph preflight validates node/submit indices, actionable semantic
  singletons, option definitions/defaults, resolver keys, graph scope, and serialized
  snapshot size before old-graph teardown
- preflight failure preserves the old graph and catalog; failure after the destructive
  teardown barrier remains fail-fast rather than invoking a generic rollback
- a frame carries at most one effect. The target node claims a concrete pass, the compiler
  maps it to a submit batch, and the frame finalizer succeeds only when that exact batch is
  accepted
- capture continuation harvest runs inside Renderer only after `Device::beginFrame()`
  reclaims the owning frame slot. Since the app snapshot is already frozen, availability
  updates no earlier than the next snapshot; minimized iterations delay harvest until
  rendering resumes, graph flush, or shutdown
- node-specific mutable UI callbacks are removed. The catalog presenter is interactive in
  human mode and renders the same snapshot as a disabled read-only mirror in agent and
  offline-Lua modes; separate read-only performance diagnostics remain app-owned

Current frame path:

`Renderer::installGraph(spec)` installs long-lived nodes and an optional generic frame-resolution resolver once -> each `renderFrame(input)` obtains one of the existing three free `FrameSlot`s -> resolver graphs acquire first, while ordinary graphs defer acquire -> renderer resolves display/render extents from post-acquire presentation state when required, using the same immutable option snapshot carried by the frame -> optionally drives display-sized `scene` extraction and bridge building (with optional app-side camera override) -> forwards optional per-frame diagnostic services through `FrameServices` -> builds and compiles the graph using the resolved plan and snapshot -> prepares/submits batches -> at the compiled acquire boundary either acquires late or validates/reuses the pre-acquired image -> records/submits the swapchain copy batch -> presents on the main thread

Boundary notes:

- `Renderer::installGraph(...)` asks every `NodeRuntime` for its ordered, CPU-only `shaderRequests()`, flattens all static requests into one `ShaderService::compileProgramsByFile(...)` batch, validates the complete result, and only then initializes nodes with their corresponding `NodeInitContext::shaderPrograms` slice. Static PSO preparation starts after that shader batch; each node enqueues its prepared graphics/compute/RT build without waiting, and the renderer joins the complete host PSO batch after every node has submitted. A serial `finalizeInitialization()` pass then resolves every static future, surfaces failures, and performs any default descriptor-set allocation before the installed graph becomes visible. Scene-derived permutations such as PathTracing hit-group variants remain deferred to the owning node, where one pipeline miss still collects and compiles the complete required shader batch before submitting the RT PSO. SBT construction materializes that future because it requires the completed pipeline handles, while the RHI distributes deferred driver work across its automatic host concurrency internally.
- Renderer owns a bounded global RenderGraph Skeleton cache of immutable normalized resource/pass/use/edge structure. The exact key covers installed graph and node revisions, display/render/swapchain shape, shader-session generation, submit/acquire policy, and the monotonic swapchain recreation generation. Enabled hits instantiate the cached structure and patch current imports, frame data, callbacks, and copy payloads without structural declarations before the existing compile cache and executor specialize barriers and retained ownership. Every built-in `rtobject` node implements this contract. AccelerationStructureBuild preflights its current AS plan before lookup so no-instance, TLAS-only, dirty-BLAS, atlas, structural-plan, and resource-capacity variants participate in the exact node key while dynamic transforms and masks preserve reusable topology. Its cold materialization consumes that prepared packet exclusively and fails fast if preflight was skipped; there is no node-local AS planning fallback.
- the preferred raster scene-facing input is `SceneBridgeFrame`; during graph build, renderer imports the frame once as graph-owned frame data and exposes `NodeFrameParameters::sceneBridgeFrameHandle` to all nodes
- renderer also extracts a lightweight `ScenePacketDomain::tlasBuildInput` packet set each frame with `SceneVisibilityMode::none` and exposes `NodeFrameParameters::scene` plus `sceneTlasBuildInputs` so AS-capable nodes can decide BLAS/TLAS create, rebuild, storage-atlas allocation, and retire policy without a separate scene RT build frame. The default scene packet set also carries active light instance packets for nodes that need frame-local scene light uploads.
- renderer can use scene-resolved camera data or an optional app/viewer camera override; `NodeFrameParameters::renderCameraConstants` carries the actual selected unjittered camera constants to every node, so temporal external APIs do not fall back to scene-only primary-camera metadata when an override is active
- `RendererGraphSpec::frameResolutionResolver` is API-agnostic: empty resolvers produce an identity `FrameResolutionPlan` and preserve late acquire, while a non-empty resolver requires the graph's single compiled acquire boundary, pre-acquires with `RendererFrameInput::acquireTimeout`, then receives `nr.rhi::Device` plus the post-acquire display extent and returns a display/render extent plus temporal-reset contribution. Renderer validates the extents, merges renderer-global invalidations into `resetHistory`, and publishes the final plan once through `NodeFrameParameters`; resolver-specific snapshots do not copy that renderer-final reset. `NodeFrameParameters::swapchainExtent` remains the display extent. The executor validates the pre-acquired image/index and current swapchain extent/format before binding it at the boundary; image-available waiting, final submission, and present semantics are unchanged. Scene extraction and default camera projection use display extent; pixel-space camera jitter converts to NDC with render extent, while `RendererCameraFrameState` remains jitter-only.
- when camera override is present, raster scene extraction uses `customFrustum` and bridge frame constants come from override data; TLAS extraction remains uncullable and covers the whole active RT scene
- renderer resolves mesh geometry draw parameters against scene atlas allocations: indexed draws carry atlas-adjusted `firstIndex` and `vertexOffset`, non-indexed draws carry atlas-adjusted `firstVertex`, frame-level atlas buffer bindings are copied into `SceneBridgeFrame::geometryBuffers`, and resident material textures are exposed through the frame's global bindless sampled-texture table with ID 0 reserved for the neutral white fallback (1x1 linear white RGBA(1,1,1,1)). The renderer seeds this table from both raster bridge material texture IDs and TLAS/RT material texture references before graph build, so PathTracing and NormalBuffer use the same resident images while retaining each slot's independent UV selection and transform.
- The TLAS-only half of scene-texture discovery is renderer-owned and revision-keyed. Its exact key contains scene identity, a static RT-domain projection, and ordered packet identity (`entity`, mesh handle, TLAS bucket); transform and trace-mask changes do not retraverse mesh/material/texture relations. A cache hit merges the owned descriptor-ID map into the frame-local raster request, while scene changes replace the key and `resetSceneBinding()` clears it. `NodeFrameParameters::sceneRevisions` carries the immutable extraction snapshot to graph nodes.
- Renderer owns one app-global environment image independently of scene/model and installed-graph lifetime. `Renderer::setEnvironmentMap(...)` synchronously uploads the GPU-only RGBA16F image through `UploadReadbackContext`, preserves retained shader-read layout/access/graphics ownership across frames, exposes its RDG handle plus 16-byte decode/intensity/yaw parameters through `FrameGlobalResources`, and merges a one-shot temporal reset into the next frame plan. Initialization installs a 1x1 legacy-miss-color fallback; the viewer replaces it with the default EXR before model loading and may later replace it through the extension-free `viewer.environment.source` selection, while model reloads or pipeline switches do not re-upload it.
- renderer frame-resource keys include the RT-only material and geometry-atlas sideband published by the AS node: `sceneRtInstanceMetadata`, `sceneRtGeometryMetadata`, `sceneRtMaterialHeaders`, `sceneRtMaterialLayers`, `sceneRtMaterialTextureRefs`, `sceneRtVertexAtlas`, and `sceneRtIndexAtlas`. The AS node publishes keyed graph frame data `scene.rt.hitSbtPlan` as a `shared_ptr<const SceneRtHitSbtPlan>`; PathTracing resolves and dereferences that immutable plan during graph build, while old graph frames retain safe ownership after structural-plan replacement. `LightPrepareNode` publishes `sceneLightHeader`, `sceneLights`, and `sceneLightAliasTable` as the frame-local scene light upload. Shader-visible RT nodes bind those buffers through `RayTracingPassBuilder::uniform(...)` / `storageBuffer(...)` in the same prepare/record split used by other descriptor-backed resources.
- `FrameServices` is the current renderer-side sideband for app-owned per-frame services that render passes may consume without creating a direct app-layer dependency on renderer internals
- Renderer has no node-specific mutable UI contract. `NodeRuntime::declareOptions(...)` contributes pure graph option definitions during preflight, and resolver/node execution reads `NodeFrameParameters::optionSnapshot`; the app's catalog presenter is the sole human control surface. `FrameServices` may still expose read-only diagnostic state to UiNode.
- `NodeConfig.instanceName` is the required, single source for an installed node's runtime name. Renderer uses it for initialization, graph nodes, build contexts, submit defaults, and node-owned pipeline debug labels; Graphics remains the default queue domain.
- `Renderer::uninstallGraph()` is the explicit installed-graph release boundary for pipeline replacement: it waits for the device to go idle, clears frame-graph builder state, executor-retained command buffers, renderer/RDG cache-suite state, and timing state, shuts down installed nodes, and leaves scene/model ownership untouched.
- `Renderer` is a non-copyable, non-movable RAII owner. Its destructor calls the same idempotent `shutdown()` used by explicit application shutdown, so device idle, retained graph callback/command-buffer release, node `flushContinuations()`/`shutdown()`, renderer GPU-resource release, and final `Device` destruction always occur in that order. Cached scene extraction state stores the non-owning `Scene` identity value rather than a `Scene&`, preventing a destroyed scene or reused object address from leaving a dangling observer.
- `NodeBuildContext` exposes frame-local named registries for cross-node `GraphResourceHandle` handoff through `nr::renderer::frameResource::*` keys and build-time `GraphFrameDataHandle` handoff through `nr::renderer::frameData::*` keys, the stable node `runtimeName`, plus node-scoped resource declaration phrases in [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx) for graph-transient color images, node-owned imported color/storage/depth images, retained imported storage images, sampled-only imported images, swapchain images, imported buffers, imported acceleration structures, graph-owned frame data, and read-only renderer global resources such as the current frame uniform binding, scene bindless texture table, and renderer-owned bindless table cache; `nr::renderer::use::*` factories produce the canonical buffer, image, and acceleration-structure pass-use descriptors that still flow through `RenderGraphBuilder` validation. Shader-access uses may carry an explicit shader-stage override, while AS build input buffers map to acceleration-structure-build stage plus shader-read access and AS scratch buffers map to acceleration-structure-build stage plus AS read/write access. `use::orderedAfterPrevious(...)` marks an explicit same-resource ordering edge when a later pass needs a barrier even if queue and layout do not otherwise force one.
- Renderer also exposes common node authoring helpers: `RendererCacheSuite` centralizes the RDG compile cache, bindless image table application cache, and renderer global scene texture descriptor table versioning; `PipelineRuntime` owns pipeline state plus per-frame descriptor sets, including runtime-sized descriptor arrays and retained graphics pipeline dynamic-rendering metadata; renderer-owned `FrameUniformArena` owns one large CPU-to-GPU uniform buffer split into `maxFrameInFlight` frame slices and uploads renderer global frame uniforms before node build, including current view/projection data, inverse view-projection, previous view plus derived previous view-projection, camera-world data, and frame state with monotonic sample-frame ordinal plus resource frame slot; `nr::renderer::ops` declares common transfer clear/copy graph passes for buffer fill, color/depth-stencil image clears, buffer/image copies, readback host-read barriers, and present-destination image transitions; and `RasterPassBuilder` / `ComputePassBuilder` / `RayTracingPassBuilder` generate the standard prepare/record glue for descriptor updates, dynamic binding snapshots, pipeline binding, prepared descriptor binding, 128-byte-limited push constants, dynamic rendering/ray tracing setup, viewport/scissor, raster state, and dispatch or trace setup. These helpers stamp pass shader-stage scopes separately from queue domain: raster stays graphics-scoped with optional per-resource vertex/fragment overrides, compute uses `eComputeShader`, and ray tracing uses `eRayTracingShaderKHR`; the compiler uses those scopes for shader-access barrier stages while fixed-function transfer, attachment, AS, SBT, and present scopes stay intent-driven. `RasterPassBuilder` can opt a pass into unordered parallel range recording through the generic RDG parallel addPass contract. RDG pass contexts can resolve buffers, images, imported acceleration structures, and typed graph frame data; the default logical descriptor resolver can write acceleration-structure descriptors and explicit buffer offset/range descriptors from those resources.
- `RenderGraphExecutor` uses the `nr.utils:threading` static worker pool for RDG CPU recording work. Build, compile, runtime resource resolution, pass prepare callbacks, primary command buffers, per-batch result aggregation, queue submit, WSI acquire, and present remain main-thread responsibilities. Pass record tasks are launched one batch at a time immediately before ordered assembly so every pre-acquire batch can already be submitted when acquire blocks.
- Renderer submission synchronization owns independent Graphics, Compute, and Transfer timeline semaphores. Every emitted timeline token records its producer queue plus that queue's monotonic value, and the next serialized batch or synthetic compute-final batch waits on the producer queue's semaphore rather than a renderer-global timeline.
- RDG pass record work uses worker-only secondary command-pool slots; the frame secondary slot 0 is reserved away from pass record tasks so the main thread stays an aggregation/submit owner during execute.
- `RenderGraphExecutor` also owns delayed Vulkan timestamp queries for compiled addPass work: it reads the previous use of a frame-slot query pool before reuse, resets the current batch's query range in the primary command buffer, wraps each recorded secondary execution with sync2 `writeTimestamp2` timestamp writes on the main thread, and returns completed GPU pass timing samples for renderer-side averaging.
- `submitNode` is a debug-named batch-splitting marker. The compiler carries both its debug name and control kind into the opened submit batch, and executor GPU debug labels include the name for capture readability. The `SwapchainAcquire` kind is a CPU control boundary: swapchain resources remain unresolved during initial prepare, earlier batches submit first, then the executor acquires and late-binds the actual image before preparing/recording the opened batch.
- The `rtobject` graph records `AccelerationStructureBuild -> LightPrepare -> PathTracing -> Ui` in one graphics batch, crosses `rtobject.GraphicsToCompute`, then records the selected RR or Accumulate post-processing node followed by `Present.Convert` and optional screenshot/readback transfers in the pre-binding compute batch. `Present.AcquireSwapchainImage` opens a final compute batch containing only `Present.CopyToSwapchain`. The default RR variant is the early-acquire exception: it acquires before resolution planning and reuses that image at this final boundary; Accumulate and other resolver-free graphs retain late acquire. TLAS build-to-trace visibility stays within the graphics batch through an AS-build-write to ray-tracing-read barrier. PathTracing selects a distinct retained seven-image guide set for each frame slot, grows or reformats only the current completed slot, and the selected compute post-processing node consumes that set after the graphics-to-compute ownership transition. Adjacent batches wait and signal through the producer queue's entry in `RendererSubmissionTimelines`; every normal graph batch signals its queue timeline, each queue's values increase monotonically across frames, and ordinary batch waits use the union of the actual first consumer scopes, including compute and transfer stages when both consume graphics output. Before execution planning, every RDG ownership transition is specialized through the runtime `QueueFamilyTransferPolicy`: same-family resources, concurrent-sharing resources, all maintenance9 buffers and acceleration-structure storage buffers, and maintenance9-eligible linear or optimal images omit explicit QFOT. Same-frame omissions retain their existing timeline dependency; retained resources save the timeline value of their final-use batch and wait on it at the next implicit initial acquire. Images keep any required layout transition at the consumer. Transfers with no retained source signal or not covered by the policy retain split release/acquire barriers with maintenance8 all-stage dependency semantics, including the synthetic source-queue release fallback for initialized retained resources. The final image-available semaphore wait is therefore `eTransfer`, the first and only compiled access to the swapchain image.
- Pass record callbacks are worker-capable and record into executor-retained secondary command buffers. The executor queues one-shot future-returning work items for the current compiled submit batch, gathers that batch's results, and executes the recorded secondaries on the primary command buffer in compiled pass order before submission. A pass that opts into parallel record is split into contiguous ranges by [`ParallelRecordPlanner`](../../src/renderer/nrRenderGraphType.ixx), then recorded and replayed with unordered chunk semantics by [../../src/renderer/nrRenderGraphExecutor.cpp](../../src/renderer/nrRenderGraphExecutor.cpp); ordered rendering work must be expressed as multiple ordered `addPass` calls with resource-use declarations between them.
- When `VK_EXT_frame_boundary` is enabled by an injected graphics debugger, each RDG submit batch is tagged with the same monotonic frame-boundary ID and the final compute-present submit carries `eFrameEnd` plus the current swapchain image handle so multi-queue captures group graphics and compute work as one frame.
- application-facing code that owns both renderer and scene should prefer `nr::app::AppSession`, which also owns the interactive app camera used to build viewer-style overrides
- `AppSession` owns the active Scene through `unique_ptr<Scene>`. `destroyScene()` keeps the stronger application boundary—wait for renderer device idle, reset cached scene extraction bindings, then destroy the active Scene—for shutdown/explicit clearing. Runtime model replacement instead builds a detached candidate, then commits it at the frame boundary by resetting the old renderer binding and swapping ownership; candidate failure preserves the old Scene and camera. This ordering remains useful even though `Scene` and `Renderer` now each provide their own destructor safety net.

Entry points:

- Runtime entry: [../../src/renderer/nrRenderer.ixx](../../src/renderer/nrRenderer.ixx)
- Frame service bridge: [../../src/renderer/nrFrameServices.ixx](../../src/renderer/nrFrameServices.ixx)
- Viewer camera runtime module: [../../src/renderer/nrViewerCamera.ixx](../../src/renderer/nrViewerCamera.ixx)
- Graph types: [../../src/renderer/nrRenderGraphType.ixx](../../src/renderer/nrRenderGraphType.ixx)
- Builder, compiler, executor: [../../src/renderer/nrRenderGraphBuilder.ixx](../../src/renderer/nrRenderGraphBuilder.ixx), [../../src/renderer/nrRenderGraphCompiler.ixx](../../src/renderer/nrRenderGraphCompiler.ixx), [../../src/renderer/nrRenderGraphExecutor.ixx](../../src/renderer/nrRenderGraphExecutor.ixx)
- Transfer pass helpers: [../../src/renderer/nrRenderGraphOps.ixx](../../src/renderer/nrRenderGraphOps.ixx)
- Terminology note: [../../src/renderer/README.md](../../src/renderer/README.md)

## 6. `renderpasses`

`nr.renderPasses` is the feature-node implementation layer on top of the renderer contract.

Current built-in nodes:

- `EmbeddedTriangleNode`
- `AccelerationStructureBuildNode`
- `LightPrepareNode`
- `PathTracingNode`
- `DlssRayReconstructionNode`
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

Current OptionSystem render-pass boundary:

- In the current OptionSystem boundary, actionable PathTracing, Accumulate, DLSS, and
  Present semantics are graph singletons. Their adjustable values come only from the
  immutable frame snapshot; node-local UI drafts and semantic pending fields are removed.
- DLSS resolution planning, structural snapshots, Skeleton patching/materialization, and
  build consume the same option snapshot. A reset succeeds only with submission of its
  DLSS evaluation batch.
- Present tone mapping and UI opacity read the snapshot. EXR capture claims its exact
  image-to-readback copy pass, arms real in-flight state only after that batch submits, and
  retains only GPU/readback/file-write continuation state. Multi-request screenshot
  counters and retained result/status UI are not part of the target boundary.

Current boundary notes:

- nodes consume `NodeBuildContext` and `NodeFrameParameters`; scene-wide deferred data reaches record callbacks through graph frame-data handles instead of borrowed build-time references
- Built-in project-shader passes use renderer-side authoring helpers instead of node-local descriptor update/bind paths: `EmbeddedTriangleNode` and `NormalBufferNode` use `PipelineRuntime`, renderer global frame resources, and `RasterPassBuilder`; `UiNode` uses `PipelineRuntime` and `RasterPassBuilder` with a dynamic bindless texture snapshot; `PresentNode` and the standalone `AccumulateNode` use `PipelineRuntime` and `ComputePassBuilder`; and `PathTracingNode` uses `PipelineRuntime`, `RayTracingPassBuilder`, and an RHI shader-binding table. `DlssRayReconstructionNode` records its typed external NGX evaluation after declaring every image use to RDG; its optional project-shader MV visualization follows that evaluation through `PipelineRuntime` and `ComputePassBuilder`.
- `RasterPassBuilder` supports explicit viewport Y modes. Scene/model passes that produce clip-space Y-up output use the negative-height viewport mode so Vulkan Y-axis adaptation happens before rasterization and front-face determination; screen-space UI passes keep the default top-left framebuffer mode.
- Shader sources import the root [../../shader/common.slang](../../shader/common.slang) module. The common module includes data-only Slang/C++ ABI declarations from [../../shader/include/share](../../shader/include/share), which are reflected into the generated `dependency.shaderShare` module, [../../shader/include/globalUniform.slang](../../shader/include/globalUniform.slang), which implements `common` and declares the shared `gFrame` uniform at Vulkan set 3 binding 0, [../../shader/include/sceneTextures.slang](../../shader/include/sceneTextures.slang), which declares the global `gSceneTextures[]` combined sampler table at set 1 binding 2, [../../shader/include/sceneLights.slang](../../shader/include/sceneLights.slang), which declares the global scene light header/list/alias ABI at set 5 bindings 0, 1, and 2, [../../shader/include/materialTextureIds.slang](../../shader/include/materialTextureIds.slang), which owns the 16-bit pair unpack helpers for the shared material texture slot enum, and [../../shader/include/pathTracing/roulette.slang](../../shader/include/pathTracing/roulette.slang), which declares the resource-free link-time Russian roulette policy for PathTracing variants. Renderer uploads `Renderer.GlobalFrameUniforms` once per frame with current render camera matrices, previous view data used by PathTracing to generate current-to-previous 2D motion vectors, camera world position, and frame state whose `xy` lanes carry a monotonic 64-bit sample-frame ordinal while `z` preserves the resource frame slot. PT graphs enable Halton(2,3) subpixel jitter; `previousViewProjection` combines the current projection with the previous view so the guide vectors exclude the inter-frame jitter delta, and the Halton phase is indexed by the same monotonic sample-frame ordinal rather than camera stability. `NodeFrameParameters::renderCameraConstants` remains unjittered. There is currently no independent camera-mutation reset source, but renderer-final `FrameResolutionPlan::resetHistory` directly propagates enable, quality, display-extent, render-extent, renderer-global environment, and successfully committed reset-marked option transitions to temporal consumers; the reset request does not restart the monotonic sample-frame ordinal, and camera frame state remains limited to jitter. C++ render-pass code binds `gFrame` from `NodeBuildContext::globalResources` only for shaders that use it; passes that use or may use `gSceneTextures[]` share [../../src/renderPasses/nrSceneTextureTableBinding.ixx](../../src/renderPasses/nrSceneTextureTableBinding.ixx) as a thin adapter into the renderer-owned bindless table cache, with ID 0 reserved for the neutral white fallback texture and a nearest immutable sampler clamped to LOD0 installed during PSO layout creation.
- `PresentNode` is the final compute conversion, optional UI composition, readback, screenshot, and swapchain-copy path. It converts the scene color buffer into swapchain-ready SDR, HDR10 PQ, or scRGB output from the selected swapchain format/color-space pair. Tone mapping and UI opacity are graph options read from the immutable frame snapshot; no node-local UI or writable draft remains. It then applies final output encoding, alpha-composites the standalone UI buffer as SDR reference-white content when `frameResource::uiColor` is published, and uses a transparent graph-local UI fallback for non-UI graph variants. It optionally copies the converted image into a caller-provided readback buffer on the compute queue. After all screenshot/readback work, `Present.AcquireSwapchainImage` remains the binding and synchronization boundary immediately before the isolated `Present.CopyToSwapchain` transfer batch: resolver-free graphs perform WSI acquire there, while resolver graphs validate and reuse the image acquired before graph build. The actual swapchain image remains absent from graph-build inputs, and the image-available semaphore is still waited only by the final present-signaling submit. Copy uses, readback host barriers, and present-destination intent stay in renderer transfer ops. The optional readback pass uses `PresentReadbackTarget` from [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx). A `render.present.capture_exr` frame effect copies `frameResource::presentSourceColor` before present encoding or UI composition into a node-owned host-visible buffer. The effect claims that exact copy pass; only submission of its compiled batch arms the continuation. Renderer harvests the continuation after the owning frame slot is reclaimed, writes the linear EXR under `screenshots/<session_id>/`, and emits the terminal machine record. `PresentNode` does not expose a source-flip control because scene raster passes already apply Vulkan Y-axis adaptation through viewport state.
- `LightPrepareNode` is a non-shader-visible upload node. It prepends a warm default directional sun, consumes active light instance packets from `NodeFrameParameters::scenePackets`, packs the set 5 scene light ABI into CPU-visible per-frame buffers, builds a Rec.709-luminance-times-photometric-intensity alias table, and publishes `frameResource::sceneLightHeader`, `frameResource::sceneLights`, and `frameResource::sceneLightAliasTable`.
- `PathTracingNode` consumes `frameResource::sceneTlas`, the RT metadata/material buffers, scene vertex/index atlas resources, published scene light buffers, and `frameData::sceneRtHitSbtPlan`, then publishes `frameResource::presentSourceColor` as a fixed 1spp single-frame result. It derives `PathTracingVariantKey` once per frame from the immutable option snapshot and retains no UI draft or pending setting. Its node-local runtime cache remains split by responsibility: `PathTracingPipelineKey` combines the active path-tracing policy with the hit-group permutation-set hash and owns RT PSO creation, while `PathTracingSbtKey` adds the hit-record-plan hash and owns SBT creation. On a pipeline miss, the node batches raygen, material/shadow miss, fixed shadow any-hit, optional material any-hit, and required closest-hit single-entry files before creating one RT PSO; SBT misses rebuild only the shader-binding table. Only raygen receives `kMaxSurfaceBounces` and `RussianRoulettePolicy`; each closest-hit request receives `kEnableFilterAfterShading` plus its `CHS = MaterialCHS<RtMaterialLayerFlag(N)>` assignment; miss and any-hit files are non-variant. FAS therefore changes closest-hit codegen without becoming a second CHS generic or material/SBT key dimension. The material permutation plan retains 17 BSDF layer-flag combinations and a binary any-hit policy for at most 34 material hit groups. Alpha-mask geometry and single-sided geometry exposed by an instance-wide cull disable select `ahMaterialPolicy`; ordinary opaque geometry keeps the hardware fast path. `RtRayType::{material=0, shadow=1, count=2}` defines the shared routing ABI: material traces use offset/miss `material`, shadow traces use offset/miss `shadow`, and both use geometry multiplier `count`. Actual Slang entry-point names are discovered from each single-entry program for Vulkan, while stable logical names drive group lookup. The pipeline has four fixed groups—raygen, two miss groups, and one any-hit-only shadow group—followed by material permutations. SBT miss records are `[material, shadow]`; every logical geometry hit record expands to `[material permutation, shared shadow group]`, while the byte stride remains RHI-derived. Focused helper modules still own scheduling, path state, hit reconstruction, material resolution, direct lighting, environment evaluation, roulette, and output. Both material and shadow any-hit entries call the same payload-independent single-sided/alpha policy; if the frame lacks a complete TLAS/RT sideband plan, the node clears its outputs instead of running RT consumers.
- The graph option `render.path_tracing.filter_after_shading_enabled` defaults to false and participates in the PathTracing structural branch and pipeline key. A successful transition requests one renderer-wide temporal-history reset without resetting `sampleFrameOrdinal_`. Every material segment first consumes the existing scatter packet, copies the following `RandomSequence` into the transient ray payload, and then unconditionally advances the path sequence by three complete `rand4` packets whether the ray misses, hits unlit material, takes optional layer branches, or runs with FAS disabled. The CHS consumes those copied packets by value as eleven fixed semantic lanes—base color, metallic-roughness, emissive, base normal, anisotropy, three clearcoat slots, two sheen slots, and transmission—with the twelfth lane reserved as padding. This preserves later random-packet ordinals without adding filter RNG state to persistent `PathState`.
- PathTracing material traversal uses `MaterialRayPayload`, whose packed `ResolvedMaterialRayPayload` remains exactly 32 32-bit lanes (128 bytes). Closest hit is its sole result encoder, while raygen immediately decodes it into full-precision material/scatter working records. Visibility traversal instead uses `ShadowRayPayload`, exactly one 32-bit `occluded` lane: `msShadow` clears it on escape, and the fixed `ahShadow` group accepts the first policy-approved hit without a closest-hit shader. No ray-kind or visibility state remains in the material payload.
- RT material texture sampling selects `TEXCOORD_0` or `TEXCOORD_1` per dense slot, applies that slot's affine transform once, and fetches exactly one nearest texel at explicit LOD0. With FAS disabled, it samples the transformed UV directly; with FAS enabled, Filter Reservoir Sampling uses one fixed scalar per texture semantic, remaps it across the bilinear X/Y decisions, and moves the UV to the selected texel center before the same nearest `SampleLevel(..., 0)` fetch. This first stage has no mip chain, stochastic mip choice, derivatives, ray cones, or hardware anisotropic filtering. Any-hit alpha coverage deliberately remains deterministic nearest sampling and never receives the FAS sequence. ID 0 remains the generic neutral-white fallback; authored-but-unavailable anisotropy is the explicit semantic exception and decodes `(1, 0.5, 1)` while RT/TLAS wait behavior remains strict for all other slots. Raster readiness remains strict for anisotropy as well. The occlusion slot and its transform remain in the shared transport ABI for fidelity, but current shading deliberately does not sample or multiply AO.
- Lit CHS variants expose one `BaseSurfaceBsdfLobe<LayerFlags>` entry for diffuse reflection, dielectric/metal reflection, and transmission. The isotropic physical-layer masks `1/3/5/7/9/11/13/15` and their anisotropic forms `17/19/21/23/25/27/29/31` remain distinct linked programs; transmission record reads and BTDF code sit behind the existing static layer-bit branch and are removed from the base-only family at link time. The transmission-enabled family selects thin folded-GGX or volume rough-dielectric behavior at runtime from the shared layer record. IOR zero preserves the `KHR_materials_ior` positive-infinity compatibility sentinel and forces the final interface Fresnel to one. Volume transmission uses the raw triangle front/back face, Snell refraction, TIR, and a bounded four-entry strict-LIFO IOR boundary stack keyed by the collision-free dense `InstanceID()` metadata index plus `GeometryIndex()` (with material index as a consistency component), with RR-only eta compensation; nested disconnected shells packed into one BLAS geometry are outside this boundary identity and must be split into separate geometries. It deliberately stores no traveled distance or absorption/scattering state. Thin transmission does not modify that stack.
- `KHR_materials_anisotropy` is carried by the RT header as the `anisotropicBaseLobe` specialization bit plus strength, counter-clockwise rotation, and explicit usable-texture presence. The dense anisotropy texture ref retains UV selection and affine transform; its nearest LOD0 read participates in the same deterministic/FAS CHS policy as the other shading textures, then decodes RG direction and B strength and builds its frame from the final normal-mapped base normal. The anisotropic flag changes only the base lobe's GGX distribution, joint correlated Smith masking-shadowing, visible-normal PDF, and VNDF sampler. Reflection, thin folded transmission, and volume transmission share those helpers; clearcoat remains isotropic. Opaque base reflection and clearcoat use the resource-free UE analytic directional-albedo fit to construct `Spec.W` multiple-scattering compensation and `Spec.E` layer preservation/lobe-selection terms; active thin and volume transmission retain their existing energy model because UE glass requires distinct joint reflection/transmission or eta-dependent compensation. No derivative-footprint or hardware anisotropic texture filtering participates in this material feature.
- PathTracing pipeline identity also includes `ShaderService::sessionGeneration()`. The nested SBT key therefore invalidates concrete shader-group handles after a shader-session reload while pipeline and SBT caches remain separate.
- PathTracing material rays separate traversal and shading through `HitObject::TraceRay`, `ReorderThread`, and `HitObject::Invoke`. HitObject shader identity remains the dominant grouping key; a three-bit project hint adds only raygen information that identity does not encode, with the high bit marking the fixed bounce-limit path and the low two bits grouping primary, second, and later-bounce branches. NEE visibility/shadow rays deliberately retain direct `TraceRay` because they skip closest-hit shading and end on first visibility result.
- PathTracing first resolves a private typed scene-input bundle. Its output boundary is seven frame-render-resolution images: noisy HDR color, hardware depth, diffuse albedo, specular albedo, world-space normal with packed linear roughness, jitter-decoupled 2D motion vectors, and specular hit distance. Allocation is lazy in build; each frame slot owns independent allocation extent/format metadata and only the current slot already waited by `Device::beginFrame()` may grow or reformat, so resources referenced by other in-flight frames are not replaced. Current logical RDG extents plus ray dispatch use `NodeFrameParameters::resolutionPlan.renderExtent` even when a grow-only backing image is larger. Each image has one renderer-persistent frame slot with retained image state; PathTracing publishes the color as `frameResource::presentSourceColor` and all seven resources as `dlss.rr.input.*`. Missing TLAS, incomplete RT sideband, and invalid SBT plans clear the complete set with reason-labeled transfer passes. Optional experimental ray-direction, 3D-motion-vector, high-resolution-depth, disocclusion, and responsivity guides are not generated. Its RT assembly names the raygen, miss, and each full hit permutation group; SBT creation resolves those names from the created pipeline instead of depending on fixed group positions.
- PathTracing binds the renderer-global environment through its dedicated reflected `gEnvironmentMap` combined sampler (linear, repeat longitude, clamp latitude) and `gEnvironment` push constants. Primary camera rays and later-bounce material rays use `msMaterial`, which samples the environment by `WorldRayDirection()` for every material-ray escape; the primary alpha-blend background uses the same directional sampler directly. Visibility rays use `msShadow`, skip the radiance sample, and the environment is deliberately absent from scene-light alias tables, direct-light sampling, and NEE. The missing-TLAS/sideband branch still executes the existing seven-image clear path before environment binding, so a frame with no model preserves current clear behavior.
- `AccumulateNode` is the selectable non-RR post-processing mode for `rtobject`; the graph installs exactly one Accumulate or DLSS RR node in its post-processing slot. Accumulate owns its unjittered view/projection snapshot and history sample count, and discards its ping-pong history when that camera transform changes, its history images are recreated, or the renderer supplies a one-shot temporal reset such as environment replacement or a committed FAS A/B transition. It does not consume DLSS node-local reset state.
- `DlssRayReconstructionNode` preserves every DLSS 310.7.0 RR image slot, SDK-defined subrect base, create flag, temporal scalar, matrix, and research-guide toggle for non-option programmatic configuration. Enabled state, quality, bypass, motion-vector visualization, and reset-history are graph options. Presets remain only in the node input's five-quality programmatic create configuration and default to the SDK-selected `Default`; no preset ID is published in the option catalog. The node's structural, build, and patch paths resolve option-owned values from `NodeFrameParameters::optionSnapshot`; reset-history claims the exact NGX evaluation pass as a frame effect. The default `rtobject` RR graph owns a bounded five-quality controller and installs the generic renderer resolver. That resolver derives Enable/Quality/Bypass from the same immutable snapshot, caches exact NGX optimal settings per quality for one display extent, clears that cache on display resize, returns identity when disabled, and contributes enable/quality/display/render transitions for renderer temporal-reset finalization. Preflight requires each resolver-consumed key to exist in the graph catalog. PathTracing produces at the resolved render extent. Coordinated RR forbids local size overrides, validates the controller-owned request and extents against the same resolved input and final renderer plan, targets display extent, and reuses the early optimal settings without a duplicate query; its controller snapshot never copies the renderer-final reset. Standalone RR retains its non-option programmatic overrides and late query path. The catalog rejects non-DLAA bypass rather than implicitly changing quality or bypass. Bypass still evaluates NGX and may publish the input color only in DLAA, so disabled RR cannot send a low-resolution PathTracing image directly to Present. Automatic NGX matrices use the renderer-selected unjittered `NodeFrameParameters::renderCameraConstants`, including app/viewer overrides, while subpixel jitter is supplied separately and normalized by render extent. Evaluation reset combines the snapshot effect, renderer-final resolution-plan flag, and feature-lifecycle reset directly. The node calls the typed `nr.rhi:dlss` RAII feature from its RDG record callback. When requested, [../../shader/renderer/dlssRayReconstructionDebug.slang](../../shader/renderer/dlssRayReconstructionDebug.slang) subsequently visualizes the same scaled pixel-space MV input into RR's color output without skipping NGX or resetting history; outside this explicit graph wiring, its default output remains `dlss.rr.output.*`.
- `NormalBufferNode` consumes `NodeFrameParameters::sceneBridgeFrameHandle`, resolves `SceneBridgeFrame` through the RDG pass context during record, binds the scene geometry atlas vertex/index buffers once at the start of each parallel chunk, and records atlas-adjusted scene mesh draw calls (indexed/non-indexed) with world-space normal visualization while honoring the per-material culling state already resolved by the scene bridge. Both vertex UV sets are available; a focused 96-byte push record carries three model rows plus only the normal texture ID, UV set, affine transform, and scale. The raster pass opts into `RasterPassBuilder::recordParallel(...)`; each executor-planned chunk records an independent contiguous range of `SceneBridgeFrame::rasterDraws`.
- `AccelerationStructureBuildNode` is a non-shader-visible graph node. It consumes `NodeFrameParameters::sceneTlasBuildInputs` and `sceneRevisions` plus scene AS semantic/mesh/material queries. Structural preflight is the sole AS planning path; both Skeleton hits and cold materialization consume its prepared packet, and missing preparation is a fail-fast contract violation rather than a request to rescan the scene. Its runtime owns the BLAS atlas/cache and an owned structural plan keyed by scene identity, the canonical scene RT structural revision projection, ordered packet topology, and one exact semantic entry per unique mesh from the current BLAS scan. The plan contains RT instance/geometry/material metadata, the material table, a shared immutable logical hit-SBT plan, and per-instance static templates; transforms, masks, current BLAS addresses, and frame slots remain dynamic, while object-space winding policy comes from the mesh semantic key. Material/texture revisions invalidate the compiled-material subcache, and mesh content/layout revisions retire and rebuild the BLAS atlas/cache descriptors without applying capacity growth unless an actual overflow occurs. Each frame slot uploads static sideband buffers only on first use or plan-generation change, while the current instance buffer and TLAS are still written/rebuilt every frame. The logical plan keeps one record per geometry; TLAS emission multiplies each logical base by the ray-type count with 64-bit intermediates, then validates uint32 record capacity and the 24-bit Vulkan instance offset. Scene identity changes retire the prior BLAS atlas/resources and clear BLAS descriptors, material values, and the structural plan before handle reuse can match.
- `UiNode` is the Dear ImGui overlay build node. It drains any remaining app-queued sections, appends CPU Performance and then GPU Performance as the invariant final two sections, finalizes the app-owned UI frame, and consumes the resulting draw data. Viewer is the leading catalog group, and the pipeline-queued Frame Status section—including swapchain format and color space—is rendered between Viewer and all ordinary catalog groups when the presenter is active; benchmark mode drains Frame Status immediately before the same performance tail. `UiNode` does not collect actionable node callbacks or own option values. It honors Dear ImGui 1.92.6 `ImTextureData` requests from the vcpkg dependency, manages UI textures through a descriptor-indexed runtime sampled-image array, keeps texture content/lifetime revision tracking locally, uploads texture pixels through the RHI upload ring before importing shader-readable texture images into the graph, selects the sampled texture per draw through push constants, and renders the overlay into its own transparent `uiBuffer` for later composition in `PresentNode`.
- Built-in GPU-only intermediate targets are node-owned imported images: `NormalBuffer.Color` / `NormalBuffer.Depth` and `Ui.Buffer` are selected from per-frame image slots, while every PathTracing RR input has its own `maxFrameInFlight` image set plus retained layout/access/ownership state. The RR output is graph-transient, and the final present converted-color image is a single retained imported image whose format follows the selected SDR/HDR swapchain output path. `Accumulate.History` remains a two-image retained ping-pong allocation for graphs that explicitly install the standalone node, with its temporal validity and sample count owned by that node. Renderer global frame uniforms are suballocated from the renderer-owned CPU-to-GPU uniform arena before node build; the renderer-owned neutral white scene texture fallback, scene-resident material texture images, and UI texture images are referenced by bindless descriptor tables whose per-frame applied-version state lives in `RendererCacheSuite`; scene texture descriptor writes carry image views/layouts while the shared nearest LOD0 sampler is immutable in each consuming PSO layout; PathTracing's environment map retains its separate linear immutable sampler; UI vertex/index data remain direct mapped frame resources; UI texture pixels use the device-level RHI upload ring.
- node record callbacks should route command recording through `PassRecordContext::commandBuffer` as a RAII `vk::raii::CommandBuffer` reference when calling `nr.rhi` command helpers.
- common pass resource declarations should prefer `NodeBuildContext` resource phrases and `nr::renderer::use::*` intent factories over hand-written graph descriptor fields where the phrase matches the pass semantics.
- shader-visible node bindings are expressed through renderer-side pass builders. The builders capture reflection-backed descriptor and push-constant snapshots, update descriptors during pass prepare, and bind prepared descriptor sets plus push constants during pass record.
- Direct descriptor update/bind helper calls are reserved for renderer/RHI helper implementations, and the detailed policy remains in [../../AGENTS.md](../../AGENTS.md) and [../../src/renderPasses/README.md](../../src/renderPasses/README.md).
- Current binding status: built-in project-shader passes use `RasterPassBuilder`, `ComputePassBuilder`, or `RayTracingPassBuilder`. Built-in transfer clear/copy passes, including PathTracing fallback clears and Present readback/screenshot/swapchain copies, use `nr::renderer::ops`; `AccelerationStructureBuildNode` and `LightPrepare.Upload` remain direct `context.addPass(...)` paths because they are non-shader-visible AS-build/upload work, and `DlssRayReconstructionNode` uses direct `addPass(...)` only to record the typed external NGX evaluation after declaring its RDG image uses. UI texture uploads occur through `UploadReadbackContext` before the overlay graph pass imports the texture images.
- cross-node built-in resource handoff is not modeled as node input/output ports; producers publish `frameResource::presentSourceColor`, `frameResource::normalDepth`, `frameResource::uiColor`, `frameResource::sceneTlas`, `frameResource::sceneLightHeader`, `frameResource::sceneLights`, `frameResource::sceneLightAliasTable`, the seven `dlss.rr.input.*` resources, and the RT material/geometry atlas sideband keys during build, while consumers require the relevant keys before declaring their passes. `PresentNode` treats `uiColor` as optional; the installed RR node consumes the guide keys and explicitly replaces `presentSourceColor` with its reconstructed output.

Entry points:

- Node type aliases: [../../src/renderPasses/nrNodeType.ixx](../../src/renderPasses/nrNodeType.ixx)
- AccelerationStructureBuild node: [../../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.ixx](../../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.ixx)
- LightPrepare node: [../../src/renderPasses/LightPrepare/nrLightPrepareNode.ixx](../../src/renderPasses/LightPrepare/nrLightPrepareNode.ixx)
- NormalBuffer node: [../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx](../../src/renderPasses/NormalBuffer/nrNormalBufferNode.ixx)
- Ui node: [../../src/renderPasses/Ui/nrUiNode.ixx](../../src/renderPasses/Ui/nrUiNode.ixx)
- Present node: [../../src/renderPasses/Present/nrPresentNode.ixx](../../src/renderPasses/Present/nrPresentNode.ixx)
- PathTracing node: [../../src/renderPasses/PathTracing/nrPathTracingNode.ixx](../../src/renderPasses/PathTracing/nrPathTracingNode.ixx)
- PathTracing ray-type and logical/physical SBT boundary: [../../shader/include/share/rtRayType.slang](../../shader/include/share/rtRayType.slang), [../../shader/renderer/pathTracing/visibility.slang](../../shader/renderer/pathTracing/visibility.slang), [../../shader/renderer/pathTracing/shadowMiss.slang](../../shader/renderer/pathTracing/shadowMiss.slang), [../../shader/renderer/pathTracing/shadowAnyHit.slang](../../shader/renderer/pathTracing/shadowAnyHit.slang), [../../src/renderPasses/nrRtHitSbtPlan.ixx](../../src/renderPasses/nrRtHitSbtPlan.ixx), [../../src/renderPasses/PathTracing/nrPathTracingNode.cpp](../../src/renderPasses/PathTracing/nrPathTracingNode.cpp), [../../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp](../../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp)
- PathTracing RR guide generation and frame-slot publication: [../../shader/renderer/pathTracing/guides.slang](../../shader/renderer/pathTracing/guides.slang), [../../src/renderPasses/PathTracing/nrPathTracingNode.cpp](../../src/renderPasses/PathTracing/nrPathTracingNode.cpp)
- Accumulate node: [../../src/renderPasses/Accumulate/nrAccumulateNode.ixx](../../src/renderPasses/Accumulate/nrAccumulateNode.ixx)
- DLSS Ray Reconstruction node: [../../src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx](../../src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx)
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

The current interaction/control chain is:

```text
Dear ImGui, one authenticated WebSocket controller, or one offline-Lua coroutine
-> read one atomically published OptionFrameSnapshot and call the same trySchedule(...)
-> AppSession/frame coordinator irreversibly attempts at most one mutation at the start of
   each renderable frame
-> publish one immutable snapshot used by UI, camera derivation, resolution planning,
   renderer, and all nodes
-> route diagnostics and machine records through nr.utils:errorHandle
-> persist generic records in build/app/logs/engine.ndjson and compact option/endpoint
   records in build/app/logs/options.ndjson
```

Reads do not consume the mutation slot. Mutations require the current binding epoch or
snapshot token; there is no retry, FIFO, task/result store, or transport-to-renderer
mutation bypass. Model success is the one documented derived session update: it commits
the selected source together with pose, FOV, and clip planes derived from the new scene
camera. The `viewer.exit` session frame effect is shared by Dear ImGui, WebSocket, and
offline Lua; the frame coordinator records its terminal success, completes the current
renderable frame, then stops the interaction hosts and shuts down `AppSession`.

Physical mouse-wheel input is presentation-only UI navigation rather than an option
producer. `PresentationContext` retains only the finite vertical delta from the latest
event poll, and `UiSystem` consumes it once before `ImGui::NewFrame()` in every authority
mode. Horizontal wheel input is discarded, window scaling remains disabled, and neither
the camera adapter nor the ImGui, WebSocket, or Lua option-admission paths receive a
wheel-derived mutation.

`nr.utils:errorHandle` remains the sole project logging facility. The viewer owns one
asynchronous rotating NDJSON session for the fixed `build/app/logs` paths: each active
`engine.ndjson` / `options.ndjson` segment starts with `NR_LOG_SESSION_V1`, rotates at
32 MiB, and retains `.1` through `.4` history segments. Generic informational diagnostics
are file-only, warnings/errors/assertions remain visible in the command window as well as
`engine.ndjson`, and all `nrCompactRecord` option and endpoint records go to
`options.ndjson`. Agents and humans read the active files directly and must reopen and
rescan the replacement active segment after rotation. A Windows kernel-backed lease owns
the canonical log directory, and `.active-viewer` is its visible marker. Another current
viewer that owns the same directory fails before touching the fixed files. After a crash
or reboot releases kernel ownership, the next viewer automatically reclaims an empty
stale marker; unexpected or non-empty markers fail closed. Process-pipe draining is not
part of the interaction boundary. The ownership and recovery implementation is in
[`src/utils/errorHandle.cpp`](../../src/utils/errorHandle.cpp), with the Win32 boundary
isolated by [`dependency.processLease`](../../src/extern/dependencyProcessLease.ixx).

The app-global environment follows a parallel, scene-independent path: the viewer loads `kloofendal_48d_partly_cloudy_puresky_8k.exr` by default and may replace it through the `viewer.environment.source` session option. `nr.pipeline` scans the fixed `assets/envMap` prefix once at startup, publishes the sorted extension-free EXR stems as the closed option enum, and resolves a selected stem back under that prefix before routing it through `nr.load:exr` -> `nr.resource:environment` scaled RGBA16F -> renderer upload ring and retained image -> PathTracing primary and later-bounce material-ray miss sampling. The environment is not registered in `nr.scene` and is not an environment light/NEE input.

The default `rtobject` main chain uses DLSS RR: PathTracing produces the fixed seven-resource guide boundary on graphics, the selected compute post-processing slot runs RR (or the user-selected standalone Accumulate mode), and Present performs compute display conversion, UI composition, late swapchain acquisition, and copy.

Application-facing lifetime wrapper:

- `nr::app::AppSession` is the preferred application boundary when one owner needs both `nr.renderer::Renderer` and an exclusively owned, optionally present `nr.scene::Scene`.
- It owns `AppCamera` as the application-side interactive viewer camera and can initialize it from the scene primary camera or a default fallback.
- It is also responsible for resetting renderer scene bindings before replacing the active scene, so model reloads create fresh renderer extract profiles for the new scene.
- It also owns `UiSystem`, begins the Dear ImGui frame before rendering, and exports it to render passes through `makeFrameServices()`.
- `UiSystem` is the sole consumer of `PresentationContext` vertical wheel deltas and forwards them only to Dear ImGui window scrolling.
- It is a safety wrapper, not a new rendering layer: it makes scene-before-renderer ownership and scene-binding reset explicit while `Scene` and `Renderer` independently close their own GPU-safe destructor paths.
- `nr.pipeline` is the viewer orchestration layer above `nr.app`: it owns the registered viewer pipeline list, builds the selected `RendererGraphSpec`, routes model-path loads through `nr.load` and `nr.scene`, stores model history under `build/app`, and queues Frame Status after publishing the current viewer camera. The catalog presenter renders Viewer as the leading section, then the queued Frame Status, then the remaining alphabetically ordered option groups; `UiNode` reserves CPU Performance and GPU Performance as the final two sections in every mode. Switching viewer pipelines builds the replacement graph first, then uninstalls the active renderer graph, rebuilds the Slang session so `loadModule` rechecks shader freshness, and installs the new graph without reloading the active scene/model.
- Viewer startup scans [../../src/pipeline/nrPipelineEnvironment.cpp](../../src/pipeline/nrPipelineEnvironment.cpp)'s fixed `assets/envMap` directory, loads the default Kloofendal selection after renderer initialization and before model loading, and seeds `viewer.environment.source` with the sorted EXR stems. The catalog presenter therefore renders a closed combo without extensions or editable paths. The frame executor resolves the selected leaf name beneath the same fixed prefix and revalidates the direct regular file before loading, so a file removed or replaced after discovery produces a terminal failure while the previously resident environment remains active.

The active Scene uses `unique_ptr<Scene>` so model
replacement can decode, register, and instantiate a detached candidate before the frame-
boundary commit. Candidate failure preserves the active Scene and camera. Successful
commit resets the renderer scene binding, swaps ownership, and commits model source plus
the derived camera values atomically. `AppSession` also becomes the sole OptionSystem
owner. `viewer.window.fullscreen` replaces the direct UI/presentation mutation path.

Useful reality checks:

- [../../src/app/exportModule.ixx](../../src/app/exportModule.ixx), [../../src/app/nrAppSession.ixx](../../src/app/nrAppSession.ixx), and [../../src/app/nrAppCamera.ixx](../../src/app/nrAppCamera.ixx) provide the application-facing lifetime wrapper plus camera/input encapsulation.
- [../../src/app/nrAppUi.ixx](../../src/app/nrAppUi.ixx) is the app-owned Dear ImGui system wrapper used by render-pass-facing UI.
- [../../src/pipeline/exportModule.ixx](../../src/pipeline/exportModule.ixx) exposes the viewer-facing contract, [../../src/pipeline/nrPipeline.cpp](../../src/pipeline/nrPipeline.cpp) owns runtime orchestration, and [../../src/pipeline/nrPipelineCommon.cpp](../../src/pipeline/nrPipelineCommon.cpp) owns the shared registry/path utilities. Each registered graph is isolated in its own implementation: [../../src/pipeline/nrNormalViewPipeline.cpp](../../src/pipeline/nrNormalViewPipeline.cpp) builds `NormalBuffer -> Ui -> Present`, while [../../src/pipeline/nrRtObjectPipeline.cpp](../../src/pipeline/nrRtObjectPipeline.cpp) builds graphics `AccelerationStructureBuild -> LightPrepare -> PathTracing -> Ui` -> submit -> pre-acquire compute `DlssRayReconstruction -> Present.Convert` -> acquire -> compute `Present.CopyToSwapchain`. Model loading/history lives in [../../src/pipeline/nrPipelineModel.cpp](../../src/pipeline/nrPipelineModel.cpp).
- [../../src/main.cpp](../../src/main.cpp) is the only app executable entry point; it parses command-line arguments through `nr.pipeline`, enters `rtobject` by default, and can select `normalview` or `rtobject` with `--pipeline`.
- [../../src/extern/CMakeLists.txt](../../src/extern/CMakeLists.txt), the `dependency*.ixx` modules under [../../src/extern](../../src/extern), [../../src/extern/exportDependency.ixx](../../src/extern/exportDependency.ixx), and [../../src/extern/nsightGraphicsSdkBridge.h](../../src/extern/nsightGraphicsSdkBridge.h) are the current source-of-truth for the centralized third-party module boundary used by the LLVM/Ninja build path.
- [../../test/smoke/app/embeddedTriangle.cpp](../../test/smoke/app/embeddedTriangle.cpp) is the renderer-only window loop where `EmbeddedTriangle` publishes `frameResource::presentSourceColor` and `Ui` publishes `frameResource::uiColor` for `PresentNode`, using the same `nr::app::AppSession` camera and UI wrapper with the default camera path.
- [../../test/smoke/app/normalBufferUiSmoke.cpp](../../test/smoke/app/normalBufferUiSmoke.cpp) is the current smoke path that validates the `NormalBuffer + Ui -> Present` integration and non-empty ImGui draw data.
