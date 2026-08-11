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
- `:environment`
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

`Mesh`, `Material`, `Texture`, `EnvironmentMap`, `CameraAsset`, `LightAsset`, `Skeleton`, `AnimationClip`, and `FluidParticleSet` are CPU-side data records.

Important consequence:

- these types may own vectors, strings, and paths
- they must not own RHI handles or Vulkan lifetime

Material-specific consequence:

- `Material` is the canonical metallic-roughness CPU record: `MaterialCorePbr` owns core PBR factors, optional extension structs hold clearcoat/sheen/transmission/IOR/volume-boundary/anisotropy data, and texture bindings live in enum-indexed `textureSlots`. Every slot records UV set 0 or 1 plus an identity-default row-major 2x2 UV transform and offset; this preserves per-texture `KHR_texture_transform` state instead of baking it into shared mesh UVs. Raster readiness remains strict for every authored texture. RT/TLAS readiness alone permits an unavailable anisotropy slot so its defined semantic fallback remains reachable; all other texture slots remain strict, and texture-residency revisions promote the normal binding path when data becomes resident. The RT compiler consumes the separate IOR block only when a transmission layer is active; IOR-only base-reflection changes remain unsupported. The volume-boundary block currently carries only the scalar thickness-mode signal; attenuation, Beer absorption, thickness textures, and volume scattering are outside the runtime contract.
- The canonical vertex record stores two UV sets but one tangent frame. The glTF load path therefore generates missing MikkTSpace tangents from the effective base-normal mapping, falling back to the clearcoat-normal mapping only when base normal is absent; distinct base and clearcoat mappings are diagnosed because they cannot both be represented exactly by this vertex ABI.
- Specular-glossiness authoring inputs are converted by `scene` before they enter `nr.resource`; this layer does not store specular/glossiness fields.
- `MaterialFeatureFlag` and `MaterialTextureSlotSemantic` form the stable material ABI consumed by `scene`; source importer strings stay in `load` diagnostics.

Environment-specific consequence:

- `EnvironmentMap` owns one mipless RGBA16F `Texture` plus explicit latitude-longitude projection, linear-sRGB color-space, radiance decode scale, intensity, and yaw metadata.
- The HALF payload is a storage encoding rather than a clamped lighting result: `load` chooses `radianceDecodeScale`, and the shader restores it before applying intensity.
- This record does not imply scene-light registration or importance sampling; the current renderer hands it directly to PathTracing for material-ray miss contribution only.

Mesh-specific consequence:

- `Mesh` owns shared vertex/index arrays plus `MeshGeometry` records.
- Each `MeshGeometry` represents one source primitive / future BLAS geometry range and must carry a valid material handle.
- A mesh is the future BLAS unit; geometry is the material-mapping and draw/build-range unit.

### 3.3 Math storage and transform convention are explicit

`nr.resource` obtains DirectXMath only through `dependency.math`. Resource records keep
standard-layout `DirectX::XMFLOAT*` and `XMUINT*` storage values; `XMVECTOR` and
`XMMATRIX` are local SIMD computation values and must not be retained in records or
containers. Quaternions use `XMFLOAT4` in `x, y, z, w` order.

Persistent transforms use row-major `XMFLOAT4X4` and the project convention is a row
vector multiplied on the left (`v * M`). Importers convert external column-vector matrix
conventions at their boundary so scene/resource consumers do not mix conventions.

### 3.4 Validation stays close to the data

Normalization and validation helpers such as mesh-bound rebuilding, skin-weight normalization, texture validity checks, and hierarchy validation belong here because they are properties of the data itself.

Important consequence:

- scene bridge code should reuse these helpers instead of re-implementing ad-hoc validation logic

### 3.5 `CameraAsset` is authored lens data, not runtime camera control

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

Scene resources follow:

`load` authoring data -> `scene` bridge -> `nr.resource` canonical CPU records -> `scene` runtime registries -> `renderer` / `renderPasses` bridged runtime data -> `rhi`

The app-global environment is the explicit scene-independent branch:

`load:exr` -> `nr.resource:environment` -> renderer-global GPU residency -> PathTracing miss sampling

Important consequence:

- `nr.resource` should stay independent from Flecs and renderer graph concerns
- scene remains the integration layer that turns imported authoring data into runtime-owned registries

## 5. What Belongs in Neighbor Layers Instead

Belongs in `load`:

- Assimp-specific import logic
- texture decode backends
- OpenEXR environment decode, validation, and HALF scaling policy
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
- Environment data model: [../src/resource/nrResourceEnvironment.ixx](../src/resource/nrResourceEnvironment.ixx)
- Camera data model: [../src/resource/nrResourceCamera.ixx](../src/resource/nrResourceCamera.ixx)
- Geometry helpers: [../src/resource/nrResourceGeometry.ixx](../src/resource/nrResourceGeometry.ixx)
