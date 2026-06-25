# Resource Module Architecture (`nr.resource`)

This note keeps `nr.resource` aligned with the current codebase without turning into a field-by-field API manual.

## 1. Role

`nr.resource` is the canonical CPU-side resource data layer of the project.

It is the place for:

- value-type resource records
- typed handle vocabulary shared across runtime layers
- small geometry and math helpers used by those records
- local validation and normalization helpers that belong with the data

It is not the place for:

- file import or decode
- ECS or scene orchestration
- GPU lifetime or Vulkan objects
- app-side camera controllers or input handling

## 2. Export Surface

The current exported partitions are:

- `:type`
- `:handle`
- `:math`
- `:geometry`
- `:mesh`
- `:material`
- `:camera`
- `:light`
- `:skeletalAnimation`
- `:particle`

Entry point:

- [../src/resource/exportModule.ixx](../src/resource/exportModule.ixx)

## 3. Stable Architectural Contracts

### 3.1 Handles are the cross-module reference vocabulary

`Handle<Tag>` in `nr.resource:handle` is the stable slot-plus-generation identity scheme used by scene registries and downstream runtime contracts.

Important consequence:

- `scene` registry families should continue to mirror the handle families defined here
- renderer and render passes should prefer bridged runtime packets or handles instead of raw canonical CPU objects

### 3.2 Resource records stay value-oriented

`Mesh`, `Material`, `Texture`, `CameraAsset`, `LightAsset`, `Skeleton`, `AnimationClip`, and `FluidParticleSet` are CPU-side data records.

Important consequence:

- these types may own vectors, strings, and paths
- they must not own RHI handles or Vulkan lifetime

### 3.3 Validation stays close to the data

Normalization and validation helpers such as mesh-bound rebuilding, skin-weight normalization, texture validity checks, and hierarchy validation belong here because they are properties of the data itself.

Important consequence:

- scene bridge code should reuse these helpers instead of re-implementing ad-hoc validation logic

### 3.4 `CameraAsset` is authored lens data, not runtime camera control

The current `CameraAsset` in [../src/resource/nrResourceCamera.ixx](../src/resource/nrResourceCamera.ixx) stores authored projection parameters:

- projection mode
- authored aspect ratio
- vertical FOV or orthographic height
- near and far plane

It does not store:

- world transform
- view matrix
- runtime input/controller state

Those belong to `scene` or the app/viewer runtime layer, not `nr.resource`.

## 4. Dependency Direction

The current direction is:

`load` authoring data -> `scene` bridge -> `nr.resource` canonical CPU records -> `scene` runtime registries -> `renderer` / `renderPasses` bridged runtime data -> `rhi`

Important consequence:

- `nr.resource` should stay independent from Flecs and renderer graph concerns
- scene remains the integration layer that turns imported authoring data into runtime-owned registries

## 5. What Belongs in Neighbor Layers Instead

Belongs in `load`:

- Assimp-specific import logic
- texture decode backends
- source-file-specific authoring quirks

Belongs in `scene`:

- canonical key planning
- registry lifetime
- GPU upload state and residency
- imported camera resolution and fallback camera policy

Belongs in `renderer` / `renderPasses`:

- per-frame draw planning
- frame constants and pass execution

Belongs in app-side runtime code:

- free-camera motion
- keyboard and mouse interaction

## 6. Code References

- Base types: [../src/resource/nrResourceType.ixx](../src/resource/nrResourceType.ixx)
- Handle family: [../src/resource/nrResourceHandle.ixx](../src/resource/nrResourceHandle.ixx)
- Mesh data model: [../src/resource/nrResourceMesh.ixx](../src/resource/nrResourceMesh.ixx)
- Material and texture data model: [../src/resource/nrResourceMaterial.ixx](../src/resource/nrResourceMaterial.ixx)
- Camera data model: [../src/resource/nrResourceCamera.ixx](../src/resource/nrResourceCamera.ixx)
- Geometry helpers: [../src/resource/nrResourceGeometry.ixx](../src/resource/nrResourceGeometry.ixx)
