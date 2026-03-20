# Resource Module System Architecture Diagram

## 1. Module Vertical Stack

```
┌─────────────────────────────────────────────────────────────────┐
│                    APPLICATION / RENDERER                        │
│                   (Draw Calls, Upload Plans)                     │
└────────────────────────┬────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│                 nr.scene (PLANNED, NOT YET)                      │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ • Flecs ECS: Entity/Component management                 │   │
│  │ • Resource Registry: Handle→Data lookup                  │   │
│  │ • Life-cycle: Batch upload, deferred deletion            │   │
│  │ • Conversion: load::Asset → resource::Type              │   │
│  └──────────────────────────────────────────────────────────┘   │
└────────────┬─────────────────────────────────┬───────────────────┘
             │                                 │
    ┌────────▼─────────┐           ┌──────────▼────────────────┐
    │  nr.resource     │           │      nr.load             │
    │ (DATA DEFINITION)│           │  (FILE I/O + DECODE)     │
    │                  │           │                          │
    │ • Mesh           │◄──────────│ MeshAsset                │
    │ • Texture        │           │ TextureAsset             │
    │ • Material       │           │ MaterialAsset            │
    │ • Skeleton       │           │ SceneAsset               │
    │ • Animation      │           │                          │
    │ • Handle<Tag>    │           └──────────────────────────┘
    │ • Triangle,Aabb  │
    │ • validate()     │           ┌──────────────────────────┐
    │ • rebuild*()     │           │       nr.rhi             │
    └────────┬─────────┘◄──────────│  (GPU MANAGEMENT)        │
             │ (types only)        │                          │
             │                     │ • Buffer/Image RAII      │
             │                     │ • Pipeline management    │
             │                     │ • Descriptor sets        │
             │                     │ • Upload operations      │
             │                     └──────────────────────────┘
             │
             └──────────────────────► [GPU Vulkan]
```

---

## 2. Data Flow: Asset → GPU

```
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 1: File Load (nr.load)                                     │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  disk: model.glTF ──[Assimp]──> load::SceneAsset                 │
│                                  ├─ MeshAsset[]                  │
│                                  ├─ TextureAsset[]               │
│                                  ├─ MaterialAsset[]              │
│                                  └─ NodeHierarchy{}              │
│                                                                   │
└──────────────────────┬───────────────────────────────────────────┘
                       │
                       │ [BRIDGE: NOT YET IMPLEMENTED IN nr.scene]
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 2: Normalization & Validation (nr.resource)                │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  load::MeshAsset ──[conversion]──> resource::Mesh                │
│  ├─ rawVertexArray                 ├─ Vertex[] ✓                 │
│  ├─ rawIndexArray                  ├─ uint32_t[] ✓               │
│  └─ rawSubmeshes                   └─ Submesh[] ✓                │
│                                                                   │
│  [Normalization Pass]                                            │
│  ├─ mesh.rebuildLocalBounds() ──────► localAabb ✓               │
│  ├─ mesh.rebuildVertexNormals() ────► smooth normals ✓          │
│  └─ mesh.normalizeSkinWeights() ────► sum(w) = 1 ✓              │
│                                                                   │
│  load::TextureAsset ──[conversion]──> resource::Texture          │
│  ├─ decodedImage                     ├─ levels[0] ✓              │
│  ├─ width/height                     ├─ format ✓                 │
│  └─ colorSpace                       └─ srgb flag ✓              │
│                                                                   │
│  [Validation]                                                    │
│  └─ texture.valid() ──────────► format check, mip chain ✓        │
│                                                                   │
└──────────────────────┬───────────────────────────────────────────┘
                       │
                       │ [ORCHESTRATION: nr.scene manages]
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 3: Resource Registration (nr.scene ECS)                    │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  resource::Mesh ──[registry.register]──> MeshHandle(slot, gen)   │
│  resource::Material ───────────────────> MaterialHandle          │
│  resource::Texture ────────────────────> TextureHandle           │
│                                                                   │
│  [Flecs Components]                                              │
│  └─ entities[i] = {position, rotation, MeshHandle, ...}          │
│                                                                   │
└──────────────────────┬───────────────────────────────────────────┘
                       │
                       │ [UPLOAD: Batch GPU transfer]
                       ▼
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 4: GPU Upload (nr.rhi)                                     │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  Mesh Data:                                                      │
│  ├─ vertices[] ──[createBuffer]──> GPU Buffer (vk::Buffer)       │
│  ├─ indices[] ───[createBuffer]──> GPU Buffer                    │
│  └─ localBounds ─[descriptors]──> push constants                │
│                                                                   │
│  Texture Data:                                                   │
│  ├─ levels[0] ───[createImage]──> GPU Image (vk::Image)          │
│  ├─ mipchain ────[mipmap]──────> GPU MIP levels                  │
│  └─ sampler ─────[sampler]─────> vk::Sampler (from SamplerDesc)  │
│                                                                   │
│  Material:                                                       │
│  ├─ factors ──[push/constants]──> GPU constant buffer            │
│  └─ textureSlots ─[bindings]───> Descriptor Set Layout           │
│                                                                   │
└──────────────────────┬───────────────────────────────────────────┘
                       │
                       ▼
            [Ready for Rendering]
```

---

## 3. Object Ownership Model

```
Lifetime Hierarchy:
┌─────────────────────────────┐
│   Application / Scene       │  Owns unique_ptr<Scene>
│   (root ownership)          │
└────────────────┬────────────┘
                 │
        ┌────────▼─────────────────────┐
        │   nr.scene (Planned)         │
        │   SceneInstance              │  Owns:
        │   ├─ Registry {}             │  • Flecs world
        │   ├─ resources[] {}          │  • Entity instances
        │   └─ lifecycleManager {}     │  • Upload/dealloc queue
        └────────┬──────────────────────┘
                 │
        ┌────────▼─────────────────────┐
        │   nr.resource                │
        │   • Mesh (value / in vec[]) │  Owned only by:
        │   • Texture (value / in vec)│  • Scene Registry
        │   • Material (value / in vec)│  • Never GPU handles
        │   • Skeleton (value)        │
        │   • Handle<Tag> (16 bytes)  │
        └────────┬──────────────────────┘
                 │
        ┌────────▼─────────────────────┐
        │   nr.rhi                      │
        │   • GPU Buffer/Image RAII    │  Owned by Scene or
        │   • via ResourceFactory       │  explicitly managed
        │   • via ResourcePool (frame)  │
        └───────────────────────────────┘

Reference Model:
├─ Submesh → MaterialHandle (non-owning typed ref)
├─ Material → TextureHandle[] (non-owning typed refs)
├─ Scene → EntityID (Flecs)
└─ GPU Buffer → resource::Mesh via upload plan (transient mapping)
```

---

## 4. Current Gap Analysis

### System Integration Status

```
Component State Matrix:

                    IMPLEMENTED   IN-USE   TESTED   GAPS
─────────────────────────────────────────────────────────
nr.load            ✅ 100%        ✅       ✅       None
  (File I/O)

nr.resource        ✅ 95%         ❌       ⚠️       
  (CPU Data)                              Partial  • Validate implemented, scene bridge missing
                                                   • No in-use test
                                                   • No conversion test

nr.scene           ❌ 0%          ❌       ❌       
  (Bridge)                                         • MISSING!
                                                   • Registry architecture
                                                   • Asset→Resource conversion
                                                   • GPU upload orchestration

nr.rhi             ✅ 90%         ✅       ✅       
  (GPU)                                            • Resource upload path unclear
                                                   • Lifecycle integration

Renderer           ⚠️ Basic       ✅       ✅       
  (Draw Loop)                                      • Waiting for scene
```

---

## 5. Critical Interaction Points

### Coupling Topology

```
Dependency Graph (→ means "imports"):

nr.load
  ├─ dependency (GLM, std)
  └─ (no other project modules)

nr.resource ← [loose coupling type definitions]
  ├─ dependency
  └─ std

nr.scene [NOT IMPLEMENTED] would import:
  ├─ nr.load (SceneAsset consumption)
  ├─ nr.resource (type definitions + instances)
  ├─ nr.rhi (GPU upload operations)
  ├─ dependency (Flecs)
  └─ std

nr.rhi [partial integration]
  ├─ exports nr.resource types but doesn't consume data
  ├─ dependency
  └─ VMA, Vulkan-Hpp

Renderer depends on:
  ├─ nr.scene (not available yet)
  └─ nr.rhi (submit commands)
```

### "No Direct Usage" Finding

```
Codebase Search Results:
────────────────────────

✅ resource::Handle exported and visible
✅ resource::Mesh, Texture, Material type definitions included
✅ Submesh's MaterialHandle used in struct definition
❌ NO CODE ACTUALLY USES resource::Mesh instances
❌ NO CODE ACTUALLY CALLS mesh.rebuildVertexNormals()
❌ NO CODE ACTUALLY CONSUMES mesh.triangle(i)

Why?
→ Scene module (the only consumer) not implemented yet
→ RHI imports types but doesn't use data payload
→ No integration layer to convert load→resource→gpu
```

---

## 6. Type Flow During Asset Load

```
Example: Load Stanford Bunny

nr.load Phase:
────────────────
bunny.glTF
  ├─[Assimp parse]→ load::MeshAsset
  │  ├─ rawVertices (float[])       [3 * N bytes]
  │  ├─ rawIndices (uint32_t[])     [4 * M bytes]
  │  ├─ rawSubmeshCount             [metadata]
  │  └─ rawNormals (auto-computed)  [3 * N bytes]
  │
  └─[PNG decode]→ load::TextureAsset[texture.png]
     ├─ decodedImage                [4 * W * H bytes RGBA8]
     ├─ imageDimensions             [W, H metadata]
     └─ colorSpace (sRGB)           [colorspace flag]


[BRIDGE POINT - NOT YET IMPLEMENTED]


nr.resource Phase (if bridge existed):
───────────────────────────────────────
load::MeshAsset ──[conversion]──> resource::Mesh
  • rawVertices → vertices[]: Vertex[]
  • rawIndices → indices[]: uint32_t[]
  • rawSubmeshes → submeshes[]: Submesh[]
  • [rebuildLocalBounds()] → localBounds: Aabb
  ├─ min = componentwise min of all vertex.position
  ├─ max = componentwise max of all vertex.position
  └─ [rebuildLocalSphere()] → localSphere: BoundingSphere
       └─ center = localBounds.center()
       └─ radius = max distance from center to any vertex

load::TextureAsset ──[conversion]──> resource::Texture
  • decodedImage → levels[0]: ImageLevel
  │  ├─ bytes[] = image pixel data
  │  ├─ width, height
  │  └─ depth = 1
  • imageDimensions → width/height metadata
  • colorSpace → srgb flag
  • [infer format] → PixelFormat::rgba8Srgb


[If-scene-existed it would then]


nr.scene Phase (would orchestrate upload):
──────────────────────────────────────────
resource::Mesh ──[registry]──> MeshHandle(slot=5, gen=0)
resource::Texture ──[registry]──> TextureHandle(slot=2, gen=1)
resource::Material ──[registry]──> MaterialHandle(slot=1, gen=0)

Flecs entity created:
  entity[bunny] = {
    Position(0, 0, 0),
    Rotation(quat identity),
    MeshHandle(5, 0),          ← Strong ref
    MaterialHandle(1, 0),      ← Strong ref
    SkeletonHandle(invalid),   ← Not skinned
    ...
  }

Upload Plan generated:
  • schedule GPU buffer creation for bunny mesh vertices+indices
  • schedule GPU image creation for bunny texture
  • schedule descriptor set creation for material


[GPU Upload]


nr.rhi Phase:
─────────────
Device::uploadMesh(MeshHandle, ...):
  ├─ auto& mesh = scene_registry.get<Mesh>(handle)
  ├─ createBuffer(mesh.vertices.data(), mesh.vertices.size())
  ├─ createBuffer(mesh.indices.data(), mesh.indices.size())
  └─ createBuffer([localBounds] as GPU data)

Device::uploadTexture(TextureHandle, ...):
  ├─ auto& texture = scene_registry.get<Texture>(handle)
  ├─ createImage(texture.width, texture.height, texture.format)
  ├─ uploadImageData(texture.levels[0].bytes)
  └─ createImageView(...)

Result: GPU resources created, mapped to handles
```

---

## 7. Call Chain Example: Mesh Normalization

```
Scenario: Scene::importAsset(load::SceneAsset asset)

Call Stack (IF scene implemented):
──────────────────────────────────────

main()
  → Scene::loadScene("bunny.glTF")
      → load::loadFile(...) [returns load::SceneAsset]  ✅ exists
          → [Assimp loads mesh vertices/indices]
          ← returns load::SceneAsset
      → scene::import(asset)  ❌ NOT IMPLEMENTED
          → for each assetMesh in asset.meshes:
              → convert [pseudo-code, doesn't exist]
                  resource::Mesh mesh;
                  mesh.vertices = convertVertices(assetMesh.rawVertices);
                  mesh.indices = assetMesh.indices;
                  
              → mesh.rebuildLocalBounds()  ✅ method exists
                  └─ iterate all vertices, compute min/max
              
              → mesh.rebuildVertexNormals()  ✅ method exists
                  ├─ zero all normals
                  ├─ for each triangle:
                  │    ├─ compute face normal
                  │    └─ accumulate to 3 vertices
                  └─ normalize all
              
                → nrAssert(mesh.validate(), ...)  ✅ implemented
                  └─ now checks: attributes/index/submesh/bounds consistency
              
              → registry.registerMesh(std::move(mesh))  ❌ no registry
                  └─ returns MeshHandle(0, 0)

CONCLUSION: All individual functions exist but 
orchestration layer (Scene) missing!
```

---

## 8. Ready-to-Use vs. Blocked Status

```
CAN USE TODAY:
  ✅ nr::resource::Triangle - complete geometric primitives
  ✅ nr::resource::Aabb - bounding box queries
  ✅ nr::resource::BoundingSphere - sphere geometry
  ✅ nr::resource::Mesh - data structure loaded
  ✅ nr::resource::Vertex - with all fields
  ✅ nr::resource::Material - PBR complete
  ✅ nr::resource::Skeleton - with validateHierarchy()
  ✅ nr::resource::Handle<Tag> - type-safe handles
  ✅ All rebuild*() methods - normalization ready

BLOCKED - NEEDS SCENE:
  ❌ Converting load::MeshAsset → resource::Mesh
  ❌ Creating resource registry with handles
  ❌ GPU upload orchestration
  ❌ Instance management (ECS)
  ❌ Lifecycle/dealloc tracking
  ❌ End-to-end rendering pipeline

BLOCKED - PARTIAL IMPL:
  ✅ Mesh::validate() - implemented (attribute/index/submesh/bounds checks)
  ✅ AnimationClip::valid() - implemented (sorted timeline + range checks)
  ✅ Animation split - cameraLight / skeletalAnimation / particle
  ⚠️ Texture format specialization - basic only
```

---

## 9. Next Steps & Roadmap

### Immediate (This Week)

1. ✒️ **Add tests for Mesh::validate()**
   ```cpp
   bool Mesh::validate() const noexcept {
     // implemented: vertex attribute finiteness
     // implemented: normal validity
     // implemented: index topology/range checks
     // implemented: submesh range + vertexOffset checks
     // implemented: localBounds/localSphere checks
   }
   ```

2. ✒️ **Write Tests**
   - Test load→resource data conversion (prototype in test utilities)
   - Test Mesh normalization (normals, bounds, sphere)
   - Test Skeleton::validateHierarchy()

### Short-term (1-2 weeks)

3. **Create nr.scene Module**
   - Define SceneInstance class
   - Implement asset→resource conversion
   - Add resource registry with Handle lookup

4. **GPU Upload Integration**
   - Define upload plan format
   - Connect to Device::beginFrame/resetFrame
   - Test memory safety

### Long-term

5. **Optimization & Polish**
   - Serialization (save/load resource bundles)
   - Memory profiling
   - Performance tuning

---

**End of Architecture Document**
