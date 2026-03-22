# Scene 模块架构设计（nr.scene + Flecs）

## 0. 文档目的

这份文档是 `nr.scene` 的现行设计基线，基于三类事实重新整理：

1. 当前仓库真实代码状态，尤其是 `src/scene`、`test/scene`、`src/load`、`src/resource`、`src/rhi`。
2. Flecs 官方文档对 query cache、hierarchy storage、observer、stats/explorer 的性能建议。
3. 当前渲染器的真实目标：**单运行时相机**、**mesh shader GBuffer pass**、**多条实时 RT pipeline**。

这份文档明确替换旧的“多视图 RenderList 优先”方案。若旧设计中的 `SceneViewInput` / `extractRenderList(view)` 与本文冲突，以本文为准。

本文的唯一目标是：

1. 让 `nr.scene` 成为 `nr.load -> nr.resource -> nr.rhi` 之间的桥接中枢。
2. 让 Scene 的数据模型贴合当前 renderer，而不是围绕一个并不存在的多相机场景抽象。
3. 把 Flecs 的使用方式固定到符合其性能模型的路径上，避免后续返工。

## 0.1 当前状态更新（2026-03-22）

为与当前代码状态保持一致，补充如下标注：

1. 代码已回退到 Phase5 之后的功能基线，Phase6 的性能对比辅助代码已移除。
2. Scene 当前采取单策略实现：固定 dedicated cached query 路径，不再保留 broad/dedicated 双策略对比分支。
3. 现行功能回归测试集合为：
   - `nr_scene_phase1_test`
   - `nr_scene_phase2_test`
   - `nr_scene_phase25_test`
   - `nr_scene_phase3_test`
   - `nr_scene_phase4_test`
   - `nr_scene_phase5_test`
   - `nr_scene_upload_readback_test`
4. 上述测试已执行并全部通过，当前结论聚焦“功能正确性与上传链路稳定性”，不再输出 Phase6 策略对比结论。

说明：本文中涉及 Phase6 性能对比/遥测规划的段落仍可作为后续演进参考，但不代表当前代码已启用该能力。

---

## 1. 当前工程现状与目标前提

### 1.1 `nr.load` 当前已经具备的能力

当前 `src/load` 已能输出 `nr::load::SceneAsset`，覆盖：

1. `NodeAsset`
2. `MeshAsset`
3. `MaterialAsset`
4. `TextureAsset`
5. `CameraAsset`
6. `LightAsset`
7. `SceneImportStatistics`

当前 loader 主路径是：

```text
Assimp import
  -> embedded/external texture discovery
  -> multi-thread texture decode
  -> SceneAsset
```

关键事实：

1. `SceneAsset` 已包含静态节点树、mesh、material、texture，以及 camera/light authoring 数据。
2. camera/light 已通过 node name 回绑到 `nodeIndex`。
3. `NodeAsset.localTransform` 仍然是 `std::array<float, 16>`，Scene 必须在单一位置完成矩阵约定与 GLM 转换。
4. loader 还没有输出 skeleton、animation clip、particle seed。

设计含义：

1. Scene 的静态导入闭环已经有了稳定输入。
2. camera/light 可以保留在 Scene registry 中，但不意味着运行时抽取接口必须围绕 camera/view 建模。
3. skeleton/animation/particle 仍属于后续阶段。

### 1.2 `nr.resource` 当前已经具备的能力

`src/resource` 已形成稳定导出面：

1. `nr.resource:handle`
   - `MeshHandle`
   - `TextureHandle`
   - `MaterialHandle`
   - `SkeletonHandle`
   - `AnimationClipHandle`
   - `ParticleSetHandle`
   - `CameraAssetHandle`
   - `LightAssetHandle`
2. `nr.resource:mesh`
3. `nr.resource:material`
4. `nr.resource:camera`
5. `nr.resource:light`
6. `nr.resource:skeletalAnimation`
7. `nr.resource:particle`
8. `nr.resource:geometry`
9. `nr.resource:math`

关键事实：

1. `nr.resource` 不持有 ECS 状态。
2. `nr.resource` 不持有 RHI/Vulkan 句柄。
3. `Handle(slot, generation)` 已经为 Scene 的 registry / slot-map 提供了统一句柄风格。
4. `nr.resource` 还提供了 Scene bridge 应复用的验证边界，例如：
   - `Mesh::validate()`
   - `Texture::valid()`
   - `Skeleton::validateHierarchy()`
   - `AnimationClip::valid()`
   - `FluidParticleSet::valid()`

设计含义：

1. ECS 实体只能持有轻量引用，不能持有大块 CPU canonical 资源。
2. Scene 的 registry family 必须镜像 `nr.resource` 的 handle family。
3. bridge 必须把 loader 数据规范化成 `nr.resource::*` 再进入 Scene。

### 1.3 `nr.rhi` 当前已经具备的能力

`src/rhi` 已提供 Scene 真正需要的三类路径：

1. persistent 资源创建：`device.resourceFactory`
2. per-frame transient 资源：`device.resourcePool`
3. 上传/读回基础设施：`nr::rhi::ops::UploadReadbackContext`

`Device::beginFrame()` 的行为非常关键：

1. 等待当前 frame slot fence
2. `resourcePool.resetFrame(frameIndex)`
3. `memoryAllocator.resetFramePool(frameIndex)`
4. 重置 command pool
5. 获取 swapchain image

关键事实：

1. `beginFrame()` 返回的 `frameIndex` 是 **frame slot**，不是单调递增 frame serial。
2. `UploadReadbackContext` 当前已经有 buffer upload/readback，但没有对称的高层 `uploadImage()` helper。

设计含义：

1. Scene 必须维护自己的 `frameSerial`，不能把 `frameSlot` 当回收序号。
2. 纹理上传仍应优先扩展 `nr.rhi::ops`，而不是在 Scene 私自复制一套 transfer 状态机。

### 1.4 当前 `scene` 模块代码状态

当前 `src/scene` 目录已有：

1. `exportModule.ixx`
2. `nrSceneType.ixx`
3. `nrSceneBridge.ixx`
4. `nrSceneUtils.ixx`
5. `nrScene.ixx`

当前 `Scene` 已落地的能力：

1. `registerTemplate()`
2. `instantiate()`
3. `destroyInstance()`
4. `updateSimulation()`
5. 统计与 registry 查询接口
6. template/runtime 层级构建
7. `ChildOf` / `Parent` 混合使用的初步策略
8. 层级变换与 world bounds 更新

当前测试已覆盖：

1. phase1/phase2：静态导入、mesh/material/texture bridge、template/instance
2. phase2.5：camera/light bridge、registry、template/runtime 绑定
3. phase3：层级、transform、bounds

当前缺失：

1. `beginFrame(frameSlot)`
2. `uploadPending()`
3. `frameSerial` 驱动的回收闭环
4. 面向 renderer 的 packet extraction
5. Flecs telemetry / explorer bridge

设计含义：

1. 现在真正该设计和实现的是 **Phase 4 / Phase 5 / Phase 6**。
2. 旧文档里“多视图 RenderList”是架构目标，但当前代码并未朝那个方向落地。
3. 这给我们留出了机会，把接口改成更适合当前 renderer 的形式。

### 1.5 当前 renderer 的真实目标前提

这部分是本次重写的关键前提。

当前 renderer 目标并不是：

1. 一个 scene 中长期并存多个运行时 camera
2. 每帧按不同 view 输出不同 RenderList
3. 以 view 为 Scene 对 renderer 的主抽象

当前 renderer 更接近：

1. 单运行时相机
2. mesh shader GBuffer pass
3. 多条实时 RT pipeline
4. 同一场景里按 **consumer / mask / bucket** 生成不同子集

典型例子：

1. raster path：
   - 只提取主相机视锥内的 instance
   - 线性化成 mesh shader/GBuffer 可消费的 draw packet
2. RT path：
   - 一份主 TLAS：所有 RT 物体
   - 一份 secondary TLAS：透明物体
   - 未来还可能出现 alpha-test、emissive-only、decal-ignored 等不同 bucket

设计结论：

1. **不同 list 的主键不是 view，而是“选择器 + 可选可见性过滤”**。
2. `Scene` 应提供 selector-driven 的 packet extraction，而不是多视图 API。
3. frustum 只是一个可选过滤条件，不应该成为核心 API 的身份参数。

---

## 2. Flecs 调研结论（只保留对 Scene 设计真正有影响的部分）

下面的结论来自 Flecs 官方文档，重点参考：

1. Queries manual
2. Hierarchies manual
3. Observers manual
4. Designing with Flecs
5. Flecs Remote API / Explorer / Stats

### 2.1 查询缓存：热路径必须缓存，临时查询不要缓存

Flecs 的 archetype/table cache 决定了 query 的主性能模型。

结论：

1. 每帧都会执行的查询应该缓存。
2. ad-hoc、只执行一次或条件完全动态的查询，优先 uncached。
3. 不要反复创建/销毁 cached query。
4. 使用 `group_by()` / `order_by()` / `cascade()` 的查询天然更偏向长期存在的 cached query。

对 Scene 的约束：

1. 变换更新、bounds 更新、render candidate 扫描、上传脏态扫描都必须复用长期 query。
2. 不要因为“用户想按某个 mask 拿一份 list”就给每个 mask 生成一个新的 Flecs query。
3. 对当前项目，**应固定少量 broad candidate query，再用 profile/mask 在提取阶段过滤**。

### 2.2 遍历（traversal）会触发 rematching，必须监控

Flecs 官方文档明确指出：

1. `up` / `cascade` 这样的 relationship traversal 可能触发 rematching。
2. rematching 在 archetype/table 较多时可能形成 spike。
3. 如果 rematching 明显，应把相关查询改为 uncached，或者拆成两条查询。

对 Scene 的约束：

1. 热路径不要默认依赖 `Transform(up)` 之类的写法。
2. `Parent` 路径的热更新应优先采用 split query 或手动父组件读取。
3. 需要把 rematching 纳入 telemetry，而不是等卡顿了再猜。

### 2.3 `ChildOf` 与 `Parent` 不是二选一，而是要按场景混用

Flecs 官方 hierarchy storage 建议可以直接映射到 Scene：

1. `ChildOf`
   - 适合大、动态、非结构化层级
   - 父节点 children 很多
   - 删除 child 是 O(1) archetype 级行为
2. `Parent`
   - 适合小、结构化、深层 prefab 树
   - 显著减少 table fragmentation
   - 对 prefab instantiation 更友好

对 Scene 的约束：

1. template/prefab 内部优先 `Parent`
2. runtime scene root、streaming cell、动态附着优先 `ChildOf`
3. 同一个 parent 可以混用两种 storage
4. 一个 child 不能同时拥有 `ChildOf` 和 `Parent`

### 2.4 grouping 与排序：只用在低基数的地方

Flecs 的 grouping 很强，但适合的是 **低基数 coarse partition**，不是高基数 draw ordering。

结论：

1. `group_by()` 很适合：
   - scene partition
   - world cell
   - `ParentDepth`
   - TLAS bucket
2. `order_by()` / query sorting 不能代替 renderer 的最终主排序。
3. material / mesh / pipeline 这类高基数资源不应作为 query 的主 group key。

对 Scene 的约束：

1. raster 主排序仍然是 Scene/renderer 侧的 sort key。
2. TLAS bucket、scene partition 可以成为 coarse grouping key。
3. 透明/不透明/RT 主集合这类分类，更适合放到 mask 或低基数 bucket，而不是 material 分组。

### 2.5 Observer 不是热路径工具

Flecs 官方文档对 observer 的定位非常清楚：

1. observer 适合低频结构事件和工具链事件。
2. 复杂 observer 在事件到来时仍需先做 query 匹配，因此不是“免费”的。
3. 如果某件事能用 system 解决，通常应该优先用 system。

对 Scene 的约束：

1. retain/release 主路径不能依赖 observer。
2. 上传队列和延迟销毁不能依赖 observer。
3. 每帧的 visibility/culling/extraction 不能靠 add/remove tag 触发 observer。
4. observer 只适合：
   - dev warning
   - editor bridge
   - stats sampling
   - import 完成通知

### 2.6 组件要小、原子化；高频状态不要靠结构修改

Flecs 官方设计建议是组件尽量小、原子化。

结论：

1. 小组件更利于 cache locality。
2. 查询多个小组件的额外开销通常不大。
3. 高频 add/remove 会导致 archetype churn。

对 Scene 的约束：

1. `LocalTransform`、`WorldTransform`、`LocalBounds`、`WorldBounds`、`RenderableBinding` 应拆开。
2. 高频 `Visible` / `GpuDirty` / `DestroyPending` 不能成为主 ECS tag。
3. 每次提取的可见性结果必须放在 scratch / packet array 中，不写回 ECS。

### 2.7 dev build 应接 Flecs stats / explorer

Flecs 官方提供了 stats addon、REST/explorer 路径。

结论：

1. 开发构建可以导入 `flecs::stats`
2. 开发构建可以通过 `flecs::Rest` 接 explorer
3. production build 不应默认启用重型 telemetry

对 Scene 的约束：

1. phase6 必须接入 dev-only telemetry
2. 需要能观察：
   - table count
   - rematch time
   - query count
   - fragmentation
   - extraction candidate / packet 数量

### 2.8 命名与 prefab 语义必须纳入 Scene 设计

Flecs 名称在 scope 内必须唯一。

结论：

1. 导入节点名不能无脑直接映射实体名。
2. prefab / template 树的名字要 deterministic。
3. 原始显示名和 ECS 内部 name 应分离。

对 Scene 的约束：

1. 同级重名必须自动 suffix。
2. 原始名字保留在 template metadata/debug 字段。
3. future extraction/profile debug 信息也要有稳定名字。

---

## 3. Scene 的职责、非职责与当前阶段边界

### 3.1 Scene 负责什么

`nr.scene` 负责五件事：

1. 把 `nr.load::SceneAsset` 规范化成 canonical CPU 资源与 template。
2. 管理 runtime ECS instances、层级、变换、bounds、可渲染引用关系。
3. 管理 GPU 驻留、上传计划、版本并存、延迟回收。
4. 按 **selector/profile** 提取 renderer 可直接消费的 packet 集合。
5. 暴露 dev-only telemetry，帮助固化 Flecs 使用策略。

### 3.2 Scene 不负责什么

1. 不做磁盘 I/O 或格式解码。
2. 不直接替代 renderer / framegraph / pass scheduler。
3. 不把 ECS 状态塞进 `nr.resource`。
4. 不把 Flecs observer 当作主运行时逻辑系统。
5. 不直接录制最终 Vulkan draw / trace ray 命令。

### 3.3 运行时单相机规则

当前 renderer 目标下，Scene 采用下面的规则：

1. 运行时只存在一个主 camera 参与当前 frame 的 raster 可见性。
2. Scene 仍可导入多个 `CameraAsset`，用于 authoring、调试、未来切换。
3. Scene 的 packet extraction 不以 camera/view 作为主键。
4. frustum 只是 `extractPackets()` 的可选过滤输入。

换句话说：

1. camera 是一个**数据来源**
2. selector/profile 才是一个**提取目标**

### 3.4 当前阶段边界

| 范围 | 当前代码状态 | 目标方向 |
| --- | --- | --- |
| 静态 mesh/material/texture bridge | 已落地 | 继续保持 |
| template / instance 模型 | 已落地 | 继续扩展 |
| camera/light Scene 接入 | 已落地 | 保留，但不做 view-first API |
| hierarchy / transform / bounds | 已落地基线 | 优化查询和 telemetry |
| `frameSerial` / upload / retire | 未完成 | Phase 4 |
| renderer packet extraction | 未完成 | 改为 selector-driven Phase 5 |
| 多视图 RenderList | 不再作为目标 | 删除 |
| telemetry / benchmark / explorer | 未完成 | Phase 6 |
| skeleton / animation / particle | 未完成 | Phase 7 |

---

## 4. 旧设计为何不适合当前 renderer

旧设计里最需要被替换掉的不是某个小 API，而是**问题建模方式**。

1. **把 list 提取建模成“按 view 提取”是错误重心。**
   当前 renderer 的 list 差异更多来自 consumer 类型与 mask/bucket，而不是来自多个 camera。

2. **`SceneViewInput` 会把 camera 矩阵错误地变成核心身份。**
   对主 TLAS、透明 TLAS 这种场景，camera 根本不是主筛选参数。

3. **多视图 scratch / `viewId` 会引入没有收益的复杂度。**
   现在只有一个运行时 camera，不值得把 API 和内部 scratch 都改造成多 view 中心模型。

4. **单一 `DrawPacket` 产物过窄。**
   当前项目除了 raster draw，还需要 RT instance/TLAS build input 这类 packet。

5. **如果用户每要一个子集就新建一个 Flecs query，会把 query cache 用错。**
   Flecs 的 cached query 应该服务长期稳定的 term 结构，而不是服务每个自定义 mask。

6. **把 material/pipeline 当 query group key 会把高基数资源错误地塞进 Flecs grouping。**
   这会放大缓存管理成本，也不符合 Flecs 的 grouping 使用场景。

7. **把每次可见性结果写回 ECS 是错误做法。**
   对 mesh shader frustum cull、RT subset 提取，这些都应该是一次性 scratch 结果。

8. **把 camera asset 数量等同于提取维度是概念混淆。**
   Scene 导入多个 camera asset 没问题，但这不代表 runtime 提取 API 要围绕 camera 集合设计。

9. **为未来预留“多 view”会掩盖当前真正需要的扩展点。**
   当前真正需要的是 opaque / transparent / RT / TLAS bucket 这样的 selector 扩展。

10. **旧设计没有把 mesh shader 和 RT 的差异放进 packet 模型。**
    raster 关注 sort key，RT 更关注 instance mask、bucket、BLAS/TLAS build input。

因此本次修订的核心结论是：

**Scene 不再围绕 view 生成 RenderList，而是围绕 selector/profile 生成 ScenePacketSet。**

---

## 5. 修订后的目标架构

### 5.1 五层模型

修订后的 Scene 不是“多视图 RenderList 工厂”，而是下面这五层：

```text
nr.load::SceneAsset
    -> Normalize / Bridge
        -> Asset Registry (CPU canonical resources)
        -> Scene Template Registry (prefab/tree template)
            -> Runtime ECS Instances
                -> Selector-driven Packet Extraction
                    -> renderer / RT builders / nr.rhi
```

五层职责：

1. **Asset Registry**
   - canonical CPU 资源
   - 去重
   - validation
   - GPU residency/version tracking

2. **Scene Template Registry**
   - prefab/template 树
   - template metadata
   - template pin set

3. **Runtime ECS Instances**
   - 实例层级
   - transform / bounds
   - renderable binding / selection bits / partition id

4. **Selector-driven Packet Extraction**
   - broad candidate query
   - mask / bucket / partition / optional frustum 过滤
   - raster / RT packet 构建

5. **renderer / RT builders**
   - Scene 的 packet 消费者
   - Vulkan command recording
   - TLAS build / update

### 5.2 推荐模块布局

当前代码仍可暂时保持 `nrScene.ixx` 较厚，但 phase4 之后建议按职责拆开：

```text
src/scene/
  CMakeLists.txt
  exportModule.ixx
  nrSceneType.ixx
  nrSceneBridge.ixx
  nrSceneRegistry.ixx
  nrSceneComponents.ixx
  nrSceneHierarchy.ixx
  nrSceneExtraction.ixx
  nrSceneTelemetry.ixx
  nrScene.ixx
```

推荐职责：

1. `type`
   - 句柄
   - public structs
   - packet/profile types

2. `bridge`
   - `load::* -> resource::*`
   - canonical key
   - validation boundary

3. `registry`
   - asset records
   - template records
   - instance records
   - extract profile records

4. `components`
   - ECS 原子组件
   - selection bits / partition / bucket

5. `hierarchy`
   - Parent / ChildOf policy
   - transform / bounds update

6. `extraction`
   - candidate queries
   - profile filtering
   - raster / RT packet build

7. `telemetry`
   - Flecs stats bridge
   - query / rematch / packet counters

### 5.3 关键句柄与时间戳

修订后需要新增 extraction profile 句柄：

```cpp
export namespace nr::scene
{
using SceneTemplateHandle = nr::resource::Handle<struct SceneTemplateTag>;
using SceneInstanceHandle = nr::resource::Handle<struct SceneInstanceTag>;
using SceneExtractProfileHandle = nr::resource::Handle<struct SceneExtractProfileTag>;

struct SceneFrameStamp
{
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
};
}
```

约束：

1. `frameSlot` 只对接 RHI ring-buffer slot。
2. `frameSerial` 只用于上传与回收生命周期。
3. `SceneExtractProfileHandle` 代表长期存在的 selector/profile 配置，而不是一次性的 query 结果。

### 5.4 对外 API（修订版）

旧接口的根本问题是把 packet extraction 建模成 view-driven。修订后改成 **profile + input**。

```cpp
export module nr.scene;

import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import std;

export namespace nr::scene
{
using SceneTemplateHandle = nr::resource::Handle<struct SceneTemplateTag>;
using SceneInstanceHandle = nr::resource::Handle<struct SceneInstanceTag>;
using SceneExtractProfileHandle = nr::resource::Handle<struct SceneExtractProfileTag>;

enum class CpuRetentionPolicy : std::uint8_t
{
    keepAll,
    discardUploadSourceAfterResident,
};

enum class TemplateHierarchyPolicy : std::uint8_t
{
    autoSelect,
    preferParent,
    preferChildOf,
};

enum class ScenePacketDomain : std::uint8_t
{
    rasterDraw,
    rayTracingInstance,
    tlasBuildInput,
};

enum class SceneVisibilityMode : std::uint8_t
{
    none,
    primaryCameraFrustum,
    customFrustum,
};

struct SceneSelectionMask
{
    std::uint64_t requireAll = 0;
    std::uint64_t requireAny = 0;
    std::uint64_t rejectAny = 0;
};

struct SceneFrustum
{
    std::array<glm::vec4, 6> planes{};
};

struct SceneCreateInfo
{
    nr::rhi::Device& device;
    std::size_t uploadBudgetBytesPerFrame = 128ull * 1024ull * 1024ull;
    CpuRetentionPolicy cpuRetention = CpuRetentionPolicy::keepAll;
};

struct SceneTemplateCreateInfo
{
    std::string debugName{};
    std::string stableKey{};
    TemplateHierarchyPolicy hierarchyPolicy = TemplateHierarchyPolicy::autoSelect;
};

struct SceneInstantiateInfo
{
    std::optional<std::reference_wrapper<const flecs::entity>> runtimeParent{};
    glm::mat4 rootTransform{1.0f};
    bool activate = true;
};

struct SceneUpdateInput
{
    float deltaSeconds = 0.0f;
};

struct SceneExtractProfileCreateInfo
{
    std::string debugName{};
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    SceneSelectionMask selection{};
    bool requireResidentGeometry = true;
    bool requireActiveInstances = true;
    bool enableCoarseGrouping = true;
};

struct SceneExtractInput
{
    SceneVisibilityMode visibility = SceneVisibilityMode::none;
    std::optional<SceneFrustum> customFrustum{};
    std::optional<std::uint32_t> partitionOverride{};
};

struct RasterDrawPacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    nr::resource::Aabb worldBounds{};
    std::uint64_t sortKey = 0;
};

struct RayTracingInstancePacket
{
    flecs::entity renderable{};
    nr::resource::MeshHandle mesh{};
    std::uint32_t submeshIndex = 0;
    glm::mat4 world{1.0f};
    std::uint32_t instanceMask = 0xFF;
    std::uint16_t tlasBucket = 0;
};

struct ScenePacketSet
{
    ScenePacketDomain domain = ScenePacketDomain::rasterDraw;
    std::vector<RasterDrawPacket> rasterDraws{};
    std::vector<RayTracingInstancePacket> rtInstances{};
};

class Scene
{
  public:
    explicit Scene(const SceneCreateInfo& createInfo);
    ~Scene();

    [[nodiscard]] SceneTemplateHandle registerTemplate(
        const nr::load::SceneAsset& sceneAsset,
        const SceneTemplateCreateInfo& createInfo = {});

    [[nodiscard]] SceneInstanceHandle instantiate(
        SceneTemplateHandle templateHandle,
        const SceneInstantiateInfo& createInfo = {});

    void destroyInstance(SceneInstanceHandle instanceHandle);

    void beginFrame(std::uint32_t frameSlot);
    void updateSimulation(const SceneUpdateInput& input);
    void uploadPending();

    [[nodiscard]] SceneExtractProfileHandle registerExtractProfile(
        const SceneExtractProfileCreateInfo& createInfo);

    void destroyExtractProfile(SceneExtractProfileHandle profile);

    [[nodiscard]] ScenePacketSet extractPackets(
        SceneExtractProfileHandle profile,
        const SceneExtractInput& input = {}) const;
};
}
```

设计说明：

1. **删除 `SceneViewInput` 与 `extractRenderList(view)` 核心地位。**
2. **保留可见性过滤，但把它降级为 `SceneExtractInput` 中的可选条件。**
3. **不同 consumer 的差异由 `SceneExtractProfile` 表达。**
4. **profile 是长期配置对象，不等于“每个 profile 一个 Flecs query”。**

### 5.5 资源注册表：按“资产”而不是“实体”做引用计数

这部分继续沿用当前方向，不回退。

规则：

1. pinning 以 template/instance 为单位，而不是每个 ECS 节点。
2. ECS 节点上的 `MeshHandle/MaterialHandle/...` 只是查询引用，不驱动 retain/release。
3. 资源真正的活跃引用来自：
   - template pin set
   - runtime instance 存在
   - optional explicit pin

#### 5.5.1 Registry family 必须镜像 `nr.resource`

不能自造新的聚合 family。

第一阶段必须支持：

1. `MeshHandle`
2. `MaterialHandle`
3. `TextureHandle`
4. `CameraAssetHandle`
5. `LightAssetHandle`

后续扩展：

1. `SkeletonHandle`
2. `AnimationClipHandle`
3. `ParticleSetHandle`

#### 5.5.2 `nr.resource` 是 Scene bridge 的 canonical validation boundary

bridge 之后，注册前必须执行：

1. `Mesh::rebuildLocalBounds()`
2. `Mesh::rebuildLocalSphere()`
3. `Mesh::validate()`
4. `Texture::valid()`
5. material 引用句柄合法性检查

Scene 不应绕过这些边界。

### 5.6 模板层：SceneAsset 先变模板，再变实例

这部分继续保持双阶段导入：

```text
SceneAsset
  -> registerTemplate(...)
      -> prefab/template tree
  -> instantiate(...)
      -> runtime ECS instance tree
```

规则：

1. 导入与实例化必须解耦。
2. template 负责持有 prefab root、pin set、绑定关系。
3. instance 负责 runtime root、活跃态、附着关系。
4. imported camera/light 保留在 template/runtime 绑定中，但不驱动 extraction API。

### 5.7 层级存储策略：明确混用，不自创关系

继续使用 Flecs 内建 `ChildOf` / `Parent`。

推荐策略：

1. template/prefab 内部结构：
   - 默认 `Parent`
   - 原因：小层级、结构稳定、实例化多
2. runtime world root / attach / dynamic cell：
   - 默认 `ChildOf`
   - 原因：更适合大而动态的关系图

禁止：

1. 自定义一套 Parent relation 试图替代 Flecs 内建语义
2. 一个 child 同时拥有两种 storage

热路径注意点：

1. `Parent` 路径不要长期依赖 `up` traversal。
2. 需要父组件时，优先 split query + manual fetch。

### 5.8 ECS 组件策略：稳定实例态进 ECS，高频状态留在 Scene 私有队列

这里是本次修订最重要的差异之一。

建议的 runtime ECS 组件：

```cpp
struct LocalTransform { glm::mat4 value{1.0f}; };
struct WorldTransform { glm::mat4 value{1.0f}; };
struct LocalBounds { nr::resource::Aabb value{}; };
struct WorldBounds { nr::resource::Aabb value{}; };

struct RenderableBinding
{
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    std::uint32_t submeshCount = 0;
};

struct SceneSelectionBits
{
    std::uint64_t value = 0;
};

struct ScenePartitionId
{
    std::uint32_t value = 0;
};

struct TlasBucketId
{
    std::uint16_t value = 0;
};
```

其中：

1. `RenderableBinding`
   - 是 extraction 热路径真正需要的 runtime 组件
   - 当前 `SceneTemplateMeshBindingRef` 更偏 template metadata
2. `SceneSelectionBits`
   - 用于 opaque / transparent / rt-main / rt-transparent 等成员资格
   - 默认比“新建很多 tag / pair / query”更适合当前 renderer
3. `ScenePartitionId`
   - 作为 coarse grouping key
4. `TlasBucketId`
   - 作为低基数 RT bucket key

典型 bit 例子：

1. `rasterOpaque`
2. `rasterTransparent`
3. `rtMain`
4. `rtTransparent`
5. `shadowCaster`
6. `alphaTest`

不应写回 ECS 的状态：

1. 每次 packet extraction 的可见性结果
2. mesh shader coarse cull 结果
3. upload pending / waiting acquire
4. delayed destroy queue

这些都应该放在 Scene 私有 scratch / queue / packet array 中。

### 5.9 导入桥接规则：从当前 `SceneAsset` 过渡到统一资源模型

这部分延续当前思路，但要补一条新规则：

**bridge 不只负责把资源导进来，还要给 runtime extraction 准备稳定的选择属性。**

#### 5.9.1 Mesh 桥接

第一阶段继续规定：

1. 每个 `load::MeshAsset` 规范化成一个 `nr::resource::Mesh`
2. 该 `Mesh` 创建一个 `Submesh`
3. `Submesh.material` 由 `mesh.materialIndex` 映射

#### 5.9.2 Texture 桥接

canonical key 继续复用 loader 的稳定 key：

1. embedded：`*N`
2. external：normalized path key

#### 5.9.3 Material 桥接

canonical key 继续推荐：

```text
<scene source path>::material[<index>]
```

同时在 bridge 阶段补出 extraction 相关的稳定属性，例如：

1. 是否透明
2. 是否 alpha test
3. 是否默认进入 RT 主集合

这些属性应在 runtime 侧下沉为 `SceneSelectionBits` 或低基数 bucket，而不是在提取时每次临时重新解析 material。

#### 5.9.4 Mesh 桥接 key

继续使用：

```text
<scene source path>::mesh[<index>]
```

#### 5.9.5 去重策略分层

第一版仍建议分层：

1. identity dedupe
2. optional content dedupe

不强制一开始就上内容哈希。

#### 5.9.6 camera/light 已接入，但不再作为核心提取维度

当前代码已经接入：

1. `SceneBridgePlan` 的 camera/light 输入与 canonical key
2. Scene registry 的 `CameraAssetRecord` / `LightAssetRecord`
3. template/runtime 的 camera/light 绑定
4. phase2.5 测试

修订后的定位是：

1. camera/light 继续保留为 import/runtime 数据
2. 运行时 renderer 选一个主 camera
3. packet extraction 不按 camera/view 设计

### 5.10 帧生命周期：Scene 自己管理 frame serial

这部分保留，但把提取步骤改成 selector-driven。

推荐顺序：

1. `auto begin = device.beginFrame();`
2. `scene.beginFrame(begin.frameIndex);`
3. `scene.updateSimulation({deltaSeconds});`
4. renderer 生成一次主相机 frustum（如果需要）
5. `scene.extractPackets(mainGbufferProfile, {.visibility = SceneVisibilityMode::primaryCameraFrustum});`
6. `scene.extractPackets(mainTlasProfile);`
7. `scene.extractPackets(transparentTlasProfile);`
8. `scene.uploadPending();`
9. renderer 录制与提交
10. `device.endFrame(batch);`

关键点：

1. packet extraction 现在允许一帧多次调用，但不是按 view，而是按 profile。
2. frustum 只在需要时参与过滤。
3. TLAS 相关提取通常不依赖 camera。

### 5.11 GPU 驻留策略：三类资源、三条路径

继续严格区分：

1. persistent asset resources
2. per-frame transient resources
3. upload staging / readback

并增加一条与 extraction 的联系：

1. `SceneExtractProfileCreateInfo.requireResidentGeometry == true` 时，
2. Scene 只输出已满足 resident 语义的 packet，
3. 未 resident 的资产要么跳过，要么在 telemetry 中计数。

### 5.12 上传语义：`resident` 之前要经过 acquire

上传状态机建议继续保持：

```cpp
enum class GpuResidencyState : std::uint8_t
{
    none,
    uploadQueued,
    waitingAcquire,
    resident,
    evictQueued,
};
```

规则不变：

1. `uploadQueued`：transfer submit 已排队
2. `waitingAcquire`：复制完成但目标队列 acquire 尚未接入
3. `resident`：上传与 acquire 都已完成

额外规则：

1. 不允许把 `waitingAcquire` 当 resident 参与 packet extraction
2. 提取结果必须能表达“当前资产不可输出 packet”的原因，以便 telemetry 统计

### 5.13 查询与系统策略：Simulation 用 systems，Extraction 用少量固定 candidate query

这是本次修订最核心的 Flecs 落地策略。

#### 5.13.1 哪些工作适合 Flecs systems / cached queries

这些工作适合长期 cached query 或 system：

1. 动画采样
2. local -> world 变换传播
3. bounds 更新
4. 长寿命结构同步
5. upload dirty scan

#### 5.13.2 哪些工作不适合塞进 Flecs pipeline

这些工作不适合继续被设计成 pipeline 最后一阶段：

1. GBuffer draw packet 提取
2. RT instance packet 提取
3. TLAS build input 提取
4. consumer-specific mask/bucket 子集提取

它们都应该是：

1. 显式 API
2. 手动运行的 cached candidate query
3. 输出到 Scene 私有 scratch / packet array

#### 5.13.3 固定少量 broad candidate query，而不是 profile-per-query

对当前 renderer，推荐 `Scene` 只维护少量固定 extraction query：

1. `rasterCandidatesQuery_`
2. `rtCandidatesQuery_`
3. 其他只有在 term 结构真的不同的时候才新增

不要做成：

1. 每个 profile 一个独立 Flecs query
2. 每个透明/不透明/RT bucket 一个独立 query
3. 每个用户自定义 mask 一个独立 query

原因：

1. 当前多数差异并不是 term 结构差异，而是同一 renderable 集合上的成员资格差异。
2. 这些差异更适合放在 `SceneSelectionBits` / `TlasBucketId` / `ScenePartitionId` 上，在 packet build 阶段过滤。

#### 5.13.4 什么时候值得拆成单独 query

默认只用 broad candidate query。只有满足下面条件时才考虑新增 dedicated query：

1. 该提取路径是长期热路径，并且每帧执行。
2. 该路径有稳定的结构性 term 差异，而不是单纯 mask 差异。
3. broad query 的 post-filter reject ratio 持续过高。
4. telemetry 证明 dedicated query 能明显减少 candidate 扫描量且不会引入新的 rematching 风险。

### 5.14 选择器驱动的 ScenePacket 提取：按 consumer，而不是按 view

这部分取代旧的“多视图 RenderList 提取”。

#### 5.14.1 核心原则

Scene 的 packet extraction 必须满足：

1. 核心 API 不以 view/camera 作为身份参数。
2. 核心 API 以 **profile + input** 工作。
3. profile 表达“我要哪类 packet、哪类成员资格”。
4. input 表达“这一帧这次提取的临时过滤条件”，例如 frustum 或 partition override。

#### 5.14.2 profile 负责什么

`SceneExtractProfile` 表达长期稳定的“提取意图”，例如：

1. 这是 raster 还是 RT/TLAS 提取
2. 需要哪些 `SceneSelectionBits`
3. 是否要求 geometry resident
4. 是否要求 instance active
5. 是否允许 coarse grouping

profile **不是**：

1. 一个 camera
2. 一个 view id
3. 一个“我想要一份 list 所以随便临时建个 query”

#### 5.14.3 input 负责什么

`SceneExtractInput` 只负责一次提取调用的临时信息：

1. 是否做 frustum 过滤
2. 使用主相机 frustum 还是 custom frustum
3. 是否临时覆盖 partition

因此：

1. 主 GBuffer 提取通常传入 `primaryCameraFrustum`
2. TLAS 构建通常不传 frustum

#### 5.14.4 packet domain 必须能覆盖 raster 与 RT

当前项目不应该再把输出物限定为单一 `DrawPacket`。

最少需要三类 domain：

1. `rasterDraw`
   - 面向 mesh shader / GBuffer draw
   - 需要 raster sort key
2. `rayTracingInstance`
   - 面向 RT instance 集合
   - 需要 instance mask / hit group / material class 等 RT 元数据
3. `tlasBuildInput`
   - 面向 TLAS build/update 输入
   - 可以与 `rayTracingInstance` 共享大部分源数据，但不应强迫复用 raster packet 形状

#### 5.14.5 主过滤路径

一次 `extractPackets(profile, input)` 的推荐步骤：

1. 根据 `profile.domain` 选择 broad candidate query
2. 线性扫描 candidates
3. 按 `SceneSelectionBits` 做 include/exclude 过滤
4. 按 `ScenePartitionId` / `TlasBucketId` 做 coarse 过滤或分桶
5. 如果 `input.visibility != none`，做 instance-level frustum cull
6. 构建对应 packet
7. 对 raster packet 生成 sort key
8. 对需要排序的 packet 做 CPU 排序

注意：

1. 主排序不要依赖 Flecs sorted query。
2. 可见性结果只保留在 scratch / packet array 中。
3. 不向 ECS 写回“这次提取可见/不可见”。

#### 5.14.6 sort key 与 grouping 规则

raster packet 推荐 sort key 维度：

1. pass
2. pipeline family
3. material class / material handle
4. mesh
5. submesh
6. instance

RT packet 推荐分组维度：

1. TLAS bucket
2. instance mask
3. material class
4. mesh / submesh

coarse grouping 推荐只用于低基数键：

1. `ScenePartitionId`
2. `TlasBucketId`
3. future `WorldCellId`

不推荐：

1. material handle 作为 query group key
2. mesh handle 作为 query group key
3. 用高基数资源 pair 来切割 archetype / query group

#### 5.14.7 这套模型如何覆盖当前 renderer

几个典型 profile：

```text
mainGbufferProfile
  domain = rasterDraw
  selection.requireAll = rasterOpaque
  input.visibility = primaryCameraFrustum

mainRtProfile
  domain = rayTracingInstance
  selection.requireAll = rtMain
  input.visibility = none

transparentTlasProfile
  domain = tlasBuildInput
  selection.requireAll = rtTransparent
  input.visibility = none
```

这正好映射当前需求：

1. GBuffer 需要主相机视锥内对象
2. TLAS 主集合不一定依赖 camera
3. 透明对象单独一份 secondary TLAS

#### 5.14.8 为什么这比 multi-view 更适合当前项目

因为当前 list 差异主要来自：

1. opaque vs transparent
2. raster vs RT
3. 主 TLAS vs secondary TLAS

而不是：

1. camera A vs camera B
2. shadow view vs reflection view

如果未来真的引入多个运行时 camera，也可以把不同 camera 产生的 frustum 作为 `SceneExtractInput` 的来源，但不需要再推翻整个 API。

### 5.15 错误与性能观测：必须随架构一起设计

统一错误策略保持不变：

1. 运行时错误/告警统一通过 `nr.utils:errorHandle`
2. 不新增 Scene 私有 diagnostics 系统用于生产路径

同时新增 extraction/telemetry 指标：

1. Flecs table count
2. cached query count
3. rematching count / time
4. hierarchy storage usage（ChildOf vs Parent）
5. raster candidate count
6. RT candidate count
7. packet count per profile
8. cull reject count / ratio
9. upload bytes per frame
10. resident asset count
11. retired asset count

#### 5.15.1 dev-only 重型 telemetry

只在 dev build 启用：

1. `flecs::stats`
2. `flecs::Rest`
3. explorer bridge
4. detailed query / packet histogram

#### 5.15.2 默认阈值建议（Phase 6 中用压测验证）

这些阈值是项目默认 heuristic，不是 Flecs 官方硬性规则：

1. 如果 traversal query 的 rematch time 在 benchmark 中持续进入主要 frame spike 来源，应改成 split query 或 uncached traversal。
2. 如果某个 extraction profile 每帧调用，且 broad candidate query 的 post-filter reject ratio 持续高于 80%，优先考虑：
   - 增加低基数 structural discriminator
   - 或拆 dedicated candidate query
3. 如果某个 parent 常态下有上千 children 且 child 单独删除频繁，优先 `ChildOf`。
4. 如果 prefab/template 树大量实例化导致 table 数随实例数明显膨胀，优先 `Parent`。

### 5.16 编译期变体策略继续沿用

对于只在常量策略上不同的路径，继续优先：

1. 单模板
2. 非类型模板参数
3. `if constexpr`

适用场景：

1. log level
2. packet domain
3. resource kind label
4. small policy tag

避免：

1. `extractRasterPackets()` / `extractRtPackets()` / `extractTlasPackets()` 这种仅常量参数不同的 wrapper 爆炸

---

## 6. 分阶段实施计划（附 Prompt）

状态说明：

1. Phase 0 / Phase 1 / Phase 2 / Phase 2.5 / Phase 3 的基线能力已经落地。
2. 旧文档中的 Phase 5“多视图 RenderList”已废弃。
3. 新计划重点转向：
   - Phase 4：GPU 上传与回收闭环
   - Phase 5：**选择器驱动 ScenePacket 提取**
   - Phase 6：遥测、压测与 Flecs 策略固化

### Phase 0：构建链与 Flecs 依赖接入

**当前状态：已完成（基础接入）**

**目标**

让 `nr.scene` 真正进入构建图，并能在 C++23 modules 环境里使用 Flecs。

**必须完成**

1. 新建 `src/scene` 目录与基础 module/CMake 骨架
2. `src/extern/CMakeLists.txt` 接入 `flecs`
3. `dependency` 模块导出 Flecs API
4. `main` 链接 `nrscene`

### Phase 1：资产注册表与模板层

**当前状态：已完成（mesh/material/texture + template/instance）**

**目标**

把“导入资源”和“实例化”拆开，建立正确的数据模型。

**必须完成**

1. `SceneTemplateHandle` / `SceneInstanceHandle`
2. Asset registry（mesh/material/texture）
3. canonical key
4. `registerTemplate()` / `instantiate()` 双阶段 API
5. template pin set / live instance count

### Phase 2：静态 SceneAsset -> resource/template/runtime 闭环

**当前状态：已完成**

**目标**

闭合当前仓库已经真实支持的静态路径。

**必须完成**

1. `load::SceneAsset -> resource::*` bridge
2. node 树 template 化
3. runtime 实例根创建
4. deterministic naming
5. `load::MeshAsset -> resource::Mesh(one submesh)`
6. bridge 后 normalize + validate 合同

### Phase 2.5：camera/light Scene 接入

**当前状态：已完成**

**目标**

把现有 loader 侧 camera/light 全链路接入 Scene。

**必须完成**

1. `SceneBridgePlan` 的 camera/light 输入与 canonical key
2. Scene registry 的 camera/light records
3. camera/light template/runtime 绑定
4. phase2.5 测试

**额外说明**

camera/light 的导入继续保留，但从本次架构修订开始，**不再把它们当做 packet extraction 的主 API 维度**。

### Phase 3：层级、变换与 bounds 系统

**当前状态：已完成（基线能力）**

**目标**

建立稳定 runtime world 更新路径。

**必须完成**

1. `LocalTransform` / `WorldTransform`
2. `LocalBounds` / `WorldBounds`
3. `ChildOf` / `Parent` 混合策略
4. cached query / system 更新世界矩阵与 bounds
5. 预留 split-query 策略

### Phase 4：GPU 上传、版本并存与安全回收

**目标**

让 Scene 真正具备资源系统的核心能力。

**必须完成**

1. `frameSlot` 与 `frameSerial` 双时间模型
2. persistent / transient / staging 三类资源分工
3. `cpuVersion -> gpuVersion` 上传判定
4. upload budget
5. 旧版本延迟退休
6. image upload helper 缺口补齐

**Prompt**

```text
请实现 Scene 的 GPU 上传与回收系统：
1) 在 Scene 内部引入 frameSerial，并纠正 frameSlot 与 frameSerial 的语义分离。
2) persistent 资源使用 device.resourceFactory，per-frame 数据使用 device.resourcePool，staging/readback 使用 UploadReadbackContext。
3) 基于 cpuVersion / resident gpuVersion 构建上传判定与 upload queue。
4) 引入 waitingAcquire 状态，正确处理 transfer upload 后的目标队列 acquire barrier。
5) 为纹理上传补齐薄 RHI helper（优先扩展 nr.rhi::ops，而不是在 Scene 重写一套 transfer 状态机）。(目前rhi已有uploadTexture工具,请分析其功能的正确性.)
6) 使用 retireAfterSerial 执行多版本并存与延迟销毁。
要求：
- 释放逻辑不能依赖 observer。
- 不要把 UploadPending / GpuDirty / DestroyPending 做成 ECS 主路径 tag。
```

### Phase 5：选择器驱动的 ScenePacket 提取

**目标**

让 Scene 成为当前 renderer 可直接消费的数据提供者，围绕 **selector/profile** 而不是围绕 view。

**必须完成**

1. 显式 `registerExtractProfile()` / `extractPackets(profile, input)` API
2. 固定少量 cached candidate query
3. `SceneSelectionBits` / `ScenePartitionId` / `TlasBucketId`
4. `RasterDrawPacket` / `RayTracingInstancePacket` / `ScenePacketSet`
5. 可选 primary-camera frustum 过滤
6. coarse grouping 预留

**Prompt**

```text
请实现 Scene 的选择器驱动 packet 提取：
1) 提供显式 `registerExtractProfile(const SceneExtractProfileCreateInfo&)` 与 `extractPackets(SceneExtractProfileHandle, const SceneExtractInput&)` API。
2) 不要再使用 `SceneViewInput` / `extractRenderList(view)` 作为核心抽取接口。
3) Scene 内部只维护少量长期 cached candidate query（例如 rasterCandidatesQuery、rtCandidatesQuery），不要为每个 mask/profile 创建独立 Flecs query。
4) 为 runtime renderable 引入稳定组件：`RenderableBinding`、`SceneSelectionBits`、`ScenePartitionId`、`TlasBucketId`。
5) 生成线性 `RasterDrawPacket` 与 `RayTracingInstancePacket`，其中 raster 侧按 pass / pipeline / material / mesh / submesh 生成 sort key。
6) 支持可选的 primary-camera frustum 过滤，但 frustum 只是 filter，不是 list 的身份参数。
7) 每次提取的可见性/筛选结果保留在 Scene scratch / packet array 中，不写回 ECS 结构状态。
8) 为后续 group_by(scene partition / world cell / TLAS bucket) 预留接口，但不要用 material 或 mesh 这类高基数资源当 query group key。
要求：
- 主排序不要依赖 Flecs sorted query 完成。
- 允许 raster、RT、TLAS build input 走同一套 profile/input 架构。
```

### Phase 6：遥测、压测与 Flecs 性能策略固化

**目标**

把“设计判断”变成“可量化结论”。

**必须完成**

1. rematching / table fragmentation 观测
2. ChildOf vs Parent 压测
3. candidate count / packet count / upload bytes 统计
4. dev-only Flecs stats / explorer 接入
5. broad query + post-filter 与 dedicated query 的压测对比

**Prompt**

```text
请为 Scene 模块加入 Flecs 与资源管理遥测能力：
1) 记录 table count、cached query count、rematching 次数或耗时、upload bytes、resident asset count。
2) 额外记录 extraction 相关指标：raster candidate count、rt candidate count、packet count per profile、cull reject count / ratio。
3) 设计 3 档规模压测场景，对比 ChildOf / Parent 在模板层与 runtime attach 层的表现。
4) 对比“broad cached candidate query + mask post-filter”与“dedicated query”两种策略在热点 profile 下的性能差异。
5) 在开发构建中接入 Flecs stats / explorer 所需模块或桥接代码，便于观察 rematching 与 archetype fragmentation。
6) 输出默认策略建议，并把压测结论回写设计文档。
要求：
- 只在 dev build 启用重型遥测。
- 将“何时从 broad query 切换到 dedicated query、何时从 cached traversal query 切换到 uncached / split query”的阈值写清楚。
```

### Phase 7：扩展 Scene 资源覆盖面（`skeletalAnimation`、`particle`、RT 资源细化）

**目标**

在静态路径稳定后，再把未来能力接进来。

**必须完成**

1. 扩展 `nr.load` 输出 skeleton / animation clip 所需数据
2. Scene 接入 `SkeletonHandle` / `AnimationClipHandle` / `ParticleSetHandle`
3. 区分 authoring asset 与 live runtime data
4. skin palette / particle buffer 走 per-frame transient
5. 为 RT 细化 BLAS cache / skinned TLAS input

**Prompt**

```text
请扩展 Scene 模块到 `skeletalAnimation` / `particle` / RT 细化资源路径：
1) 扩展 nr.load，使 SceneAsset 能输出 skeleton、animation clip 所需数据。
2) 在 Scene 中建立 `SkeletonHandle`、`AnimationClipHandle`、`ParticleSetHandle` 对应的 bridge 与 registry 路径。
3) FluidParticleSet 只作为 seed/authoring 数据，live particle 数据继续走 transient scratch/buffer。
4) skin palette、particle 数据统一走 per-frame transient buffer，不进入 persistent asset registry。
5) 在已存在的 selector-driven packet extraction 上补齐 skinned RT instance / BLAS cache / TLAS update input。
要求：
- 不破坏前面静态 mesh scene 的路径。
- 高频数据继续遵守“ECS 保存引用与状态，buffer 数据走 frame-local scratch”的原则。
```

---

## 7. 验收标准

### 7.1 工程与接口

1. `nrscene` 继续稳定参与构建。
2. `Scene` 对 `nr::rhi::Device` 使用必需非拥有引用语义。
3. 对外接口以 `registerTemplate()/instantiate()/registerExtractProfile()/extractPackets()` 为核心。
4. 核心接口中不再把 `SceneViewInput` / `extractRenderList(view)` 作为目标 API。

### 7.2 数据模型

1. registry family 与 `nr.resource` 对齐。
2. runtime ECS 中只有轻量组件与引用：
   - transform
   - bounds
   - renderable binding
   - selection bits
   - low-cardinality partition/bucket
3. 每视图可见性这类旧概念不写回 ECS。

### 7.3 GPU 生命周期

1. `frameSlot` 与 `frameSerial` 语义分离。
2. 资源版本并存与回收以 `retireAfterSerial` 为准。
3. `waitingAcquire` 不会被误判为 `resident`。

### 7.4 Flecs 使用方式

1. 热路径 query 只创建一次并复用。
2. 不为每个 selector/mask 动态制造 cached query。
3. template/prefab 优先 `Parent`，runtime dynamic attach 优先 `ChildOf`。
4. traversal 热路径保留 split-query / uncached fallback。
5. observer 不进入主资源/提取路径。

### 7.5 Packet 提取

1. 能表达 GBuffer raster packet 提取。
2. 能表达 RT instance / TLAS build input 提取。
3. 可以对主相机做 coarse frustum cull，但 frustum 只是 filter，不是 API 身份。
4. raster 主排序由 CPU sort key 完成，不依赖 Flecs sorted query。

### 7.6 遥测与策略固化

1. dev build 可接入 Flecs stats / explorer。
2. 能观测 table count、query count、rematch、candidate/packet 数量、upload bytes。
3. 文档中明确写出 broad query / dedicated query / split query 的切换条件。

---

## 8. 参考资料

### 8.1 仓库内资料

1. `src/scene/nrScene.ixx`
   - 当前 Scene 主实现
2. `src/scene/nrSceneType.ixx`
   - Scene 对外类型与记录结构
3. `src/scene/nrSceneBridge.ixx`
   - canonical key 与 bridge plan
4. `src/scene/nrSceneUtils.ixx`
   - 命名、矩阵转换、bounds 变换辅助
5. `src/extern/exportDependency.ixx`
   - Flecs 导出方式
6. `src/rhi/nrDevice.ixx`
   - `beginFrame()` 语义与 frame slot 行为
7. `test/scene/nr_scene_phase25_test.cpp`
   - camera/light 接入现状
8. `test/scene/nr_scene_phase3_test.cpp`
   - hierarchy / transform / bounds 基线

### 8.2 Flecs 官方文档

1. Queries manual  
   `https://www.flecs.dev/flecs/md_docs_2Queries.html`
2. Hierarchies manual  
   `https://www.flecs.dev/flecs/md_docs_2HierarchiesManual.html`
3. Observers manual  
   `https://www.flecs.dev/flecs/md_docs_2ObserversManual.html`
4. Designing with Flecs  
   `https://www.flecs.dev/flecs/md_docs_2DesignWithFlecs.html`
5. Flecs Remote API / Explorer  
   `https://www.flecs.dev/flecs/md_docs_2FlecsRemoteApi.html`
6. Stats addon reference  
   `https://www.flecs.dev/flecs/group__cpp__addons__stats.html`

这些文档对本设计最重要的几条指导分别是：

1. 查询缓存的创建/复用策略
2. `ChildOf` / `Parent` 的真实性能边界
3. traversal rematching 的监控与 fallback
4. observer 不是高频热路径工具
5. dev build 中 stats / explorer 的接入方式

---

## 9. 故障速查：`invalid component '#0' passed to add()`（Scene/Flecs）

### 9.1 现象

在 Scene 中新增 ECS 组件后，如果启动或测试时出现：

```text
invalid component '#0' passed to add()
```

通常不是 Flecs 本身有问题，而是 Scene 侧组件注册流程出了问题。

### 9.2 在当前 Scene 架构下的高发触发点

1. 新增了组件，但没有在统一注册入口注册。
2. 在 `registerSceneComponents()` 执行之前就开始 `add/set/query` 组件。
3. 新增了 extraction 相关组件，例如：
   - `RenderableBinding`
   - `SceneSelectionBits`
   - `ScenePartitionId`
   - `TlasBucketId`
   但忘了注册。
4. C++ module 导出链断裂，导致类型存在但 Flecs component id 未初始化。

### 9.3 最小可行修复

修复顺序：

1. 把所有 Scene ECS 组件统一放进一个注册入口。
2. `Scene` 构造时第一时间调用该注册入口。
3. 不要在组件注册之前创建依赖这些组件的 entity / prefab / query。
4. 新增组件后同步补测试。

推荐检查点：

```cpp
inline void registerSceneComponents(flecs::world& world)
{
    world.component<LocalTransform>();
    world.component<WorldTransform>();
    world.component<LocalBounds>();
    world.component<WorldBounds>();
    world.component<RenderableBinding>();
    world.component<SceneSelectionBits>();
    world.component<ScenePartitionId>();
    world.component<TlasBucketId>();
}
```

### 9.4 验证方法

1. 先跑 `test/scene` 基础用例，确保 template/instance 与 phase3 层级路径不退化。
2. 再补 extraction 相关 smoke test：
   - 创建一个 renderable
   - 设置 `SceneSelectionBits`
   - 注册 profile
   - `extractPackets()` 成功返回 packet
3. 如接入 telemetry，再验证 stats/explorer build 不影响基础用例。

### 9.5 回归检查清单

每次给 Scene 新增 ECS 组件时，至少检查下面五项：

1. 是否已在统一入口注册
2. 是否已被模块正确导出/import
3. 是否已有最小测试覆盖
4. 是否会出现在 prefab/template 与 runtime instance 两层
5. 是否真的是 ECS 稳定状态，而不是应该留在 Scene 私有 scratch/queue 的高频临时状态
