# Resource Module Memory (nr.resource)

This file is a compact, code-aligned memory note for agents. It focuses on:
- module architecture design
- interface design and usage contracts

It is based on the current implementation under src/resource/*.ixx.

## 1. Scope and Role

nr.resource is the CPU-side canonical resource data model.

In scope:
- value-type resource structures (mesh, texture, material, camera, light, animation, particle)
- lightweight geometry/math utilities used by those structures
- typed handles for stable references
- normalization and validation methods close to resource data

Out of scope:
- file decoding/loading (nr.load)
- ECS/world orchestration (planned nr.scene)
- GPU object ownership and Vulkan execution (nr.rhi)

Design style:
- public data members + small helper methods
- value semantics through std::vector/std::string/std::filesystem::path
- no GPU handles stored inside resource objects

## 2. Module Architecture

Top-level module:
- nr.resource (exportModule.ixx)

Exported submodules:
- :type
- :handle
- :math
- :geometry
- :mesh
- :material
- :camera
- :light
- :skeletalAnimation
- :particle

High-level dependency structure:
- :type is the shared base type layer
- :handle imports :type
- :material imports :type + :handle
- :mesh imports :handle + :geometry + :math
- :camera and :light import :type
- :skeletalAnimation imports :math
- :particle imports :geometry

System position (current):
- nr.load -> (planned scene bridge) -> nr.resource -> (planned scene orchestration) -> nr.rhi -> Vulkan

Important integration fact:
- nr.resource is largely complete as a data and algorithm layer.
- End-to-end runtime integration still depends on scene-layer bridging/orchestration.

## 3. Base Type Layer (:type)

Shared constants and type aliases:
- invalidResourceSlot: uint32 max value
- PixelFormat = vk::Format
- TextureDimension = vk::ImageType
- FilterMode = vk::Filter
- MipFilterMode = vk::SamplerMipmapMode
- AddressMode = vk::SamplerAddressMode

Enums:
- AlphaMode: opaque, mask, blend
- CameraProjection: perspective, orthographic
- LightType: directional, point, spot

Notes:
- This layer directly reuses Vulkan enums instead of custom duplicate enums.

## 4. Handle Layer (:handle)

Core template:
- template <typename Tag> struct Handle

Fields:
- slot (default invalidResourceSlot)
- generation (default 0)

Methods:
- valid() -> slot != invalidResourceSlot
- packed() -> uint64 where high 32 bits are generation and low 32 bits are slot
- default three-way comparison

Concrete handle aliases:
- MeshHandle
- TextureHandle
- MaterialHandle
- SkeletonHandle
- AnimationClipHandle
- ParticleSetHandle
- CameraAssetHandle
- LightAssetHandle

## 5. Math Utilities (:math)

Functions under nr::resource::math:
- finiteFloat(float)
- finiteComponents(...)
- finiteVec(glm::vec<L, T, Q>)
- finiteQuat(glm::quat)

Purpose:
- central finite-value checks reused by validation code.

## 6. Geometry Interfaces (:geometry)

### Aabb
Fields:
- min, max

Methods:
- valid()
- center()
- extent()
- expand(point)
- merge(rhs)

Behavior notes:
- merge handles invalid rhs and invalid current bounds safely.

### BoundingSphere
Fields:
- center
- radius

Method:
- valid(eps)

### Triangle
Fields:
- p0, p1, p2

Methods:
- edge01(), edge02()
- computeFaceNormal() with degenerate guard
- computeArea()
- centroid()
- isDegenerate(eps)

## 7. Mesh Interfaces (:mesh)

### VertexSkinData
Fields:
- joints: uvec4
- weights: vec4

Methods:
- hasInfluence(eps)
- normalizeWeights(eps)

### Vertex
Fields:
- position
- normal
- tangent
- texCoord0, texCoord1
- color0
- skin

Methods:
- hasValidNormal(eps)
- hasValidTangent(eps)
- normalizeFrame(eps)

### Submesh
Fields:
- name
- firstIndex
- indexCount
- vertexOffset
- material (MaterialHandle)
- localBounds

Methods:
- triangleCount()
- indexed()

### Mesh
Fields:
- name
- vertices
- indices
- submeshes
- localBounds
- localSphere
- clockwiseFrontFace
- skinned

Queries:
- vertexCount(), indexCount(), triangleCount(), indexed()
- triangle(index) (throws out_of_range on invalid access)

Rebuild/normalize APIs:
- rebuildLocalBounds()
- rebuildLocalSphere()
- rebuildFlatNormals(eps)
- rebuildVertexNormals(eps)
- normalizeSkinWeights(eps)

Validation:
- validate() checks:
	- non-empty vertices
	- finite and valid vertex attributes
	- skin weight sanity (finite, non-negative, sum > eps)
	- index topology/range consistency when indexed
	- non-indexed meshes require vertex count multiple of 3
	- localBounds validity
	- finite localSphere center and non-negative radius
	- submesh range/bounds/vertex reference consistency

## 8. Material and Texture Interfaces (:material)

### ImageLevel
Fields:
- width, height, depth
- bytes

Method:
- byteSize()

### Texture
Fields:
- name
- sourcePath
- dimension
- format
- width, height, depth
- mipCount
- srgb
- compressed
- levels

Methods:
- valid()
- hasCpuPixels()
- byteSize()
- mipExtent(mip)

Validation contract:
- dimensions and mipCount must be non-zero
- format must not be vk::Format::eUndefined
- levels.size() must be <= mipCount

### SamplerDesc
Fields:
- minFilter, magFilter, mipFilter
- addressU, addressV, addressW
- mipLodBias, minLod, maxLod
- maxAnisotropy

### MaterialTextureSlot
Fields:
- texture (TextureHandle)
- sampler
- uvSet
- scale
- strength

### Material
Fields:
- name
- baseColorFactor
- emissiveFactor
- metallicFactor
- roughnessFactor
- normalScale
- occlusionStrength
- alphaCutoff
- alphaMode
- doubleSided
- fixed slots: baseColor, normal, metallicRoughness, occlusion, emissive

Methods:
- isOpaque()
- isAlphaMasked()
- isAlphaBlended()

## 9. Camera and Light Interfaces

### CameraAsset (:camera)
Fields:
- name
- projection
- verticalFovRadians
- orthoHeight
- nearPlane
- farPlane

Method:
- perspective()

### LightAsset (:light)
Fields:
- name
- type
- color
- intensity
- range
- innerConeRadians
- outerConeRadians
- castShadow

Method:
- finiteRange()

## 10. Skeletal Animation Interfaces (:skeletalAnimation)

### Bone
Fields:
- inverseBindPose (aligned mat4)
- localBindPose (aligned mat4)
- name
- parentIndex

Method:
- isRoot()

### Skeleton
Fields:
- name
- bones

Methods:
- boneCount()
- rootCount()
- validateHierarchy()

Hierarchy validation checks:
- parent index in range or root marker (< 0)
- at least one root if non-empty
- no parent cycle (hop limit check)

### Keyframes and Tracks
Types:
- KeyframeVec3(timeSeconds, value)
- KeyframeQuat(timeSeconds, value)
- BoneAnimationTrack(boneIndex, translations, rotations, scales)

### AnimationClip
Fields:
- name
- durationSeconds
- ticksPerSecond
- looping
- tracks

Method:
- valid()

Validation checks:
- finite duration/ticks
- duration > eps and ticksPerSecond >= 0
- non-empty tracks
- per-track boneIndex >= 0
- at least one key stream present in each track
- keyframe time sorted and inside [0, duration]
- finite values and non-zero quaternion length

## 11. Particle Interfaces (:particle)

### FluidParticleSet
Fields (SoA layout):
- positionRadius
- velocityLifetime
- colorDensity

Methods:
- count()
- reserve(n)
- resize(n)
- computeBounds()
- valid()

Validation contract:
- all three arrays must have equal size.

## 12. Recommended Usage Pattern

Typical pipeline role for callers:
1. Convert decoded assets into nr.resource value objects.
2. Run normalization helpers where needed (bounds, normals, skin weights).
3. Run validate()/valid()/validateHierarchy() checks before registration/upload.
4. Register objects in a higher-level registry that returns typed handles.
5. Use handles to orchestrate GPU upload and runtime binding in scene/rhi layers.

Practical order for mesh data:
1. Fill vertices/indices/submeshes
2. rebuildLocalBounds()
3. rebuildLocalSphere()
4. rebuildVertexNormals() and normalizeSkinWeights() if required by source data
5. validate()

## 13. Quick Integration Gaps (Architecture-Level)

Current gap outside this module:
- A scene bridge/orchestration layer is required to connect load assets, resource objects, ECS lifecycle, and rhi upload execution.

This is not a resource-module defect; it is an integration layer responsibility.
