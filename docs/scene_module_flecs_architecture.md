# Scene 模块架构设计（nr.scene + Flecs）

## 0. 文档目的

这份文档不是“泛泛而谈的 ECS 方案草图”，而是基于当前仓库真实状态、`src/extern/flecs/docs/` 本地文档、以及 Flecs 官方文档整理出来的一份可落地设计规范。

唯一目标：

1. 让 `nr.scene` 真正成为 `nr.load -> nr.resource -> nr.rhi` 之间的桥接中枢。
2. 让资源生命周期、模板实例化、GPU 驻留、逐帧回收、多视图渲染提取都落在一个一致的模型里。
3. 让 Flecs 的使用方式符合其性能模型，而不是“能跑就行”。

---

## 1. 当前工程现状与对接上下文（基于仓库事实）

先确认真实上下文，再设计 Scene。

### 1.1 `nr.load` 当前已经具备的能力

当前 `src/load` 已经能够导出 `nr::load::SceneAsset`，并且数据覆盖面是明确的：

1. `NodeAsset`
2. `MeshAsset`
3. `MaterialAsset`
4. `TextureAsset`
5. `SceneImportStatistics`

当前 loader 路径是：

```text
Assimp import
  -> embedded/external texture discovery
  -> multi-thread texture decode
  -> SceneAsset
```

关键事实：

1. `SceneAsset` 目前只覆盖静态节点/网格/材质/纹理。
2. 当前 `load` 还没有输出相机、灯光、骨骼、动画、粒子。
3. `NodeAsset.localTransform` 是 `std::array<float, 16>`，Scene 层必须在单一位置明确矩阵约定并转换到 GLM。

这意味着：

1. Scene 的第一阶段必须先做好“静态 mesh scene”闭环。
2. 动画、骨骼、camera、light、particle 必须作为后续阶段，并在必要时先扩展 `nr.load`。

### 1.2 `nr.resource` 当前已经具备的能力

`src/resource` 已经形成了和 Scene 直接相关的稳定导出面：

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
8. `nr.resource:geometry` 与 `nr.resource:math`

关键事实：

1. `nr.resource` 不持有 ECS 状态。
2. `nr.resource` 不持有 RHI/Vulkan 句柄。
3. `Handle(slot, generation)` 设计天然适合 Scene 的 slot-map / arena / registry。
4. 当前源码中的真实导出面是分拆的 `camera/light/skeletalAnimation/particle`，Scene 规划必须以 `src/resource/exportModule.ixx` 为准，而不是继续假设 `cameraLight` 或兼容 `animation` 聚合层存在。
5. `nr.resource` 已经内建了 Scene bridge 应复用的规范化/验证边界，例如：
   - `Mesh::validate()`
   - `Texture::valid()`
   - `Skeleton::validateHierarchy()`
   - `AnimationClip::valid()`
   - `FluidParticleSet::valid()`

这意味着：

1. Scene 不应该把“大对象”塞进 ECS，而应该把 `nr.resource::*` 记录放在 registry，ECS 只引用轻量 handle。
2. Scene bridge 应先把 loader 数据规范化到 `nr.resource::*`，再调用对应验证接口，最后才允许进入 registry。
3. 后续扩展 registry 时必须镜像当前 `nr.resource` 的 handle family，而不是重新发明新的聚合 handle 类型。

### 1.3 `nr.rhi` 当前已经具备的能力

`src/rhi` 当前已经提供了 Scene 真正需要的三类能力：

1. 持久资源创建：`device.resourceFactory`
2. 每帧瞬态资源：`device.resourcePool`
3. 上传/读回基础设施：`nr::rhi::ops::UploadReadbackContext`

并且 `Device::beginFrame()` 的行为非常关键：

1. 等待当前 frame slot 的 fence
2. `resourcePool.resetFrame(frameIndex)`
3. `memoryAllocator.resetFramePool(frameIndex)`
4. 重置 command pool
5. 获取 swapchain image

这带来两个重要设计约束：

1. `beginFrame()` 返回的 `frameIndex` 是 frame slot，不是单调递增 frame serial。
2. Scene 不能把“slot index”误当成“全局完成帧编号”做 GPU 回收判定。

此外还要注意：

1. `UploadReadbackContext` 目前已有 `uploadBuffer()`、`readbackBuffer()`、`readbackImage()`。
2. 它通过 transfer queue + timeline semaphore + ownership acquire/release barrier 协调上传。
3. 它当前没有完整的 `uploadImage()` 高层 helper。

这意味着纹理上传设计不能假设 RHI 里已经有现成的一步式 image upload API。

### 1.4 构建与依赖现状

当前工程存在明确断点：

1. `src/scene` 目录不存在。
2. `src/CMakeLists.txt` 中 `add_subdirectory(scene)` 被注释，但 `main` 仍然链接 `nrscene`。
3. `src/extern/CMakeLists.txt` 当前没有把 `flecs` 子模块接入构建。
4. `dependency` 模块当前没有导出 Flecs 头，也没有链接 Flecs target。

这意味着 `nr.scene` 当前不是“未优化”，而是“还没有从 0 到 1”。

---

## 2. Flecs 调研结论（只保留对 Scene 设计真正有影响的部分）

本节是对 `src/extern/flecs/docs/` 与官方文档的归纳，不复述手册，而是直接提炼成设计规则。

### 2.1 查询缓存：热路径必须缓存，临时查询不要缓存

Flecs 的核心性能前提是 archetype/table cache。

结论：

1. 每帧都会跑的查询必须在 Scene 初始化阶段创建并复用。
2. 不能在 system lambda、query loop、editor filter 中反复创建 `flecs::query`。
3. 只有 ad-hoc 条件未知、生命周期很短的查询才允许 uncached。

对 Scene 的约束：

1. 变换传播、bounds、render candidate、upload dirty scan 这些查询都必须缓存。
2. “查某个 root 的后代”“查某个 cell 的实体”这类动态条件，优先考虑 grouped query 或者显式索引，不要每次新建 cached query。

### 2.2 遍历（traversal）会触发 rematching，必须监控

Flecs 文档明确指出：

1. `up` / `cascade` 这类 traversal query 可能触发 rematching。
2. rematching 在 archetype/table 很多时会产生明显 spike。
3. 如果 rematching 明显，应该把相关 traversal query 从 cached 改为 uncached，或者拆成两条查询。

对 Scene 的约束：

1. 不能把“世界变换计算”“父材质继承”“visibility 继承”一股脑写成大量 cached traversal query。
2. 对 `Parent` 层级的热路径，不要依赖 `Transform(up)` 这类自动向上遍历。
3. 必要时拆为：
   - 一条 `ChildOf` 路径查询
   - 一条 `Parent` 路径查询
   - 手动 fetch parent data

### 2.3 `ChildOf` 与 `Parent` 不是二选一，而是要按场景混用

Flecs 文档给出的结论很明确：

1. `ChildOf` 适合大、动态、非结构化层级。
2. `Parent` 适合小、结构化、可复用、常和 prefab 一起出现的层级。
3. `Parent` 最大优点是减少 table fragmentation，尤其适合 prefab 树。
4. `ChildOf` 最大优点是针对“某个 parent 的很多 children”查询非常快，且删除 child 是 O(1) archetype 级行为。

对 Scene 的约束：

1. 不能把所有层级都强制做成 `ChildOf`。
2. 也不能把整个 runtime world 全部改成 `Parent`。
3. 要使用 Flecs 内建的 `ChildOf` / `Parent`，不要再自定义一套 Parent 关系并试图自己补 traits。

推荐策略：

1. 运行时世界根节点、streaming cell、动态附着物、拥有大量 children 的层级：`ChildOf`
2. prefab/template 内部结构、小型可复用节点树、重复实例化的模型层级：`Parent`

### 2.4 Observer 不是热路径工具

Flecs 手册给出的边界也很明确：

1. observer 适合低频、结构事件、工具链事件。
2. observer 不适合高频逻辑。
3. hooks 比 observer 更高效，但 hooks 也不适合承载复杂系统级副作用。

对 Scene 的约束：

1. 资源 retain/release、上传队列、GC 主路径不能依赖 observer。
2. 高频状态变化不能靠 add/remove tag 触发 observer。
3. observer 只适合：
   - debug warning
   - editor inspector bridge
   - import completion notification
   - metrics sampling

### 2.5 系统默认 readonly/staging，sync point 必须显式注解

Flecs pipeline 在 `progress()` 中默认进入 readonly/staging 模式。

结论：

1. 系统里的 `add/remove/set` 默认是 deferred command。
2. 如果系统内部会对某类组件做 ECS 操作，必须用 `write<T>()` / `read<T>()` 给 pipeline 提供 sync point 信息。
3. 只有确实需要“立即可见”结果时，才用 `immediate()`。

对 Scene 的约束：

1. 大部分 Scene 系统都应保持默认 readonly。
2. 只有极少数“结构性修复”场景才考虑 `immediate()`。
3. 任何使用 `get()` 读取非查询组件、或使用 `set/add/remove` 写入别的组件的系统，都必须做 `read<T>()` / `write<T>()` 注解。

### 2.6 排序与 grouping：用在合适的地方

Flecs 文档的建议也很有用：

1. query sorting 依赖 change detection，排序频繁变化时代价不低。
2. sorted query 里排序键应尽量是 readonly。
3. grouping 是低粒度但非常高效的组织方式。
4. group iterators 特别适合“世界 cell / region / coarse partition”这类筛选。

对 Scene 的约束：

1. 主渲染排序不要指望 Flecs query sorting 完成全部工作。
2. pass/pipeline/material/mesh/submesh 的 draw ordering 仍然应该落到 Scene 的 CPU sort key。
3. grouping 可以用于 coarse partition，例如：
   - world cell
   - streaming chunk
   - scene partition
4. 不要把 material、mesh 这种高基数资源 pair 直接拿去做主 query group key。

### 2.7 组件要小、原子化；高频显隐不要靠结构修改

Flecs 官方设计文档强调：

1. 组件保持原子化。
2. 大组件会恶化缓存局部性。
3. 高频 add/remove 会造成 archetype churn。

对 Scene 的约束：

1. `LocalTransform`、`WorldTransform`、`WorldBounds`、`MeshRef`、`MaterialOverride` 必须拆开。
2. `UploadPending`、`GpuDirty`、`DestroyPending` 这类高频状态不应作为 ECS tag 主路径。
3. 每相机可见性结果不应通过给实体 add/remove `Visible` tag 来表达。
4. per-view culling、upload dirty queue、GC queue 应该是 Scene 私有 scratch / queue / bitset。

### 2.8 命名与作用域必须纳入设计

Flecs 名称在 `ChildOf` scope 内必须唯一。

对 Scene 的约束：

1. 直接把导入节点名原样塞进实体名是危险的。
2. Scene import 必须有确定性命名规则：
   - 优先使用源名字
   - 同级冲突自动 suffix
   - 原始显示名存放到独立 debug/doc 字段

---

## 3. Scene 的职责、非职责与当前阶段边界

### 3.1 Scene 负责什么

`nr.scene` 负责四件事：

1. 把 `nr.load::SceneAsset` 规范化为可复用 CPU 资源与模板。
2. 管理 runtime ECS instance、层级、变换、bounds、可渲染引用关系。
3. 管理 GPU 驻留、上传计划、版本并存、逐帧安全回收。
4. 按 view 提取渲染消费数据，而不是录制具体 Vulkan draw。

### 3.2 Scene 不负责什么

1. 不做磁盘 I/O 或格式解码。
2. 不直接成为 renderer / framegraph / render pass scheduler。
3. 不在 `nr.resource` 中塞 ECS 或 RHI 句柄。
4. 不把 Flecs observer 当主逻辑框架。

### 3.3 当前阶段边界

必须把“已有数据能做什么”与“未来要做什么”拆开。

| 范围 | 当前是否可做 | 说明 |
|---|---:|---|
| 静态节点树 | 是 | `load::SceneAsset.nodes` 已有 |
| 静态 mesh/material/texture import | 是 | loader + resource 已就位 |
| 多实例模板复用 | 是 | Scene 设计层应优先支持 |
| `Skeleton/AnimationClip` import | 否（需先扩展 load） | `nr.resource:skeletalAnimation` 已有，但 `load::SceneAsset` 尚未导出 |
| `CameraAsset` import | 否（需先扩展 load） | `nr.resource:camera` 已有，但 `load::SceneAsset` 尚未导出 |
| `LightAsset` import | 否（需先扩展 load） | `nr.resource:light` 已有，但 `load::SceneAsset` 尚未导出 |
| `FluidParticleSet` authoring/seed import | 否 | `nr.resource:particle` 已有，但目前没有 loader/source 数据，且 live simulation 不应等同于 asset import |
| BLAS/TLAS 场景集成 | 部分可做 | RHI 基础已有，Scene 还需单独规划 |

---

## 4. 现有设计的主要 pitfalls

这一节不是“吹毛求疵”，而是直接指出如果照旧设计继续走，后面一定会返工的点。

1. **把“导入资源”和“创建运行时实例”绑死在一个 API 上。**
   当前设计更像 `importLoadedScene(sceneAsset) -> root entity`，这会直接丢掉模板复用能力，也不适合同一 asset 多次实例化。

2. **把 `frameIndex` 当成全局 frame serial。**
   当前 RHI 的 `Device::beginFrame()` 返回的是 ring-buffer slot index；如果直接拿它做 `retireAfterFrame` 判定，GPU 回收语义会错。

3. **把未来资源扩展仍按旧的聚合接口来想象。**
   当前 `nr.resource` 的真实边界是 `camera/light/skeletalAnimation/particle` 分拆模块，而 `nr.load::SceneAsset` 还不能产出这些数据。如果 Scene 继续围绕模糊的 `animation` / `cameraLight` 桶设计，注册表、模板 pin set、验证策略都会失真。

4. **默认把上传脏态和销毁态做成 ECS tag。**
   这类状态高频变化，会造成 archetype churn、query cache 抖动和不必要的结构修改。

5. **把资源 retain/release 逻辑塞进 hooks 或 observer。**
   这样做隐式副作用太强，而且和 staging/merge 的时序很容易打架。主路径应使用显式 registry queue。

6. **层级策略过于单一。**
   旧设计基本倾向“默认 ChildOf”，但 Flecs 官方文档明确指出 prefab/模板树与 runtime 大场景树的最佳存储并不相同。

7. **没有引入“模板/Prefab 层”。**
   直接把导入后的节点树做成 runtime entities，会导致：
   - 多实例重复构建 ECS 树
   - 多实例重复 retain 资源
   - 后续 override/variant 能力很差

8. **没有区分 persistent GPU resource、per-frame transient、upload staging 三类内存。**
   当前 RHI 已经区分 `resourceFactory`、`resourcePool`、`UploadReadbackContext`，Scene 设计必须跟着这三类走，不能抽象成一个“统一 upload”黑盒。

9. **没有考虑 transfer queue upload 的 acquire barrier 语义。**
   `uploadBuffer()` 返回的是需要目标队列 acquire 的 ticket，而不是“上传完就能直接读”。如果 Scene 不建模这个状态，会出现“已 resident 但尚未 acquire”的错误语义。

10. **纹理上传接口假设和当前 RHI 不一致。**
    RHI 目前没有对称的高层 `uploadImage()` helper。纹理上传是 Scene Phase 3/4 之前必须补齐的接口缺口。

11. **将 RenderList 绑定为 Flecs pipeline 最后一阶段过于僵硬。**
    RenderList 是 view-dependent 的，一个 frame 可能有主相机、shadow views、reflection views。把它做成“单次 pipeline 阶段产物”会直接限制多视图。

12. **`framesInFlight` 作为 SceneCreateInfo 独立参数容易与 Device 漂移。**
    这一参数应以 `device.frameManager.frameCount()` 为准，Scene 不应再自己持有第二份真相。

13. **命名规则没有纳入设计。**
    节点名直接映射 Flecs name 会与 scope uniqueness 冲突，尤其是导入 glTF/FBX 时大量重名节点是常态。

14. **没有定义跨文件资源去重层级。**
    纹理、材质、网格到底按 source index 去重、按路径去重，还是按内容 hash 去重，旧文档没有给明确策略。

15. **没有定义 CPU 副本何时可以丢弃。**
    资源上传成功后是否保留像素/顶点数据，会明显影响内存占用。旧设计没有这层策略。

16. **没有把 `nr.resource` 的验证 API 纳入桥接合同。**
    如果 Scene 直接把 loader 数据塞进 registry，而不经过 `Mesh::validate()`、`Texture::valid()` 等边界检查，后续调试只会越来越难，资源层也会失去“canonical CPU shape”意义。

---

## 5. 修订后的目标架构

核心改动只有一句话：

**Scene 不直接把 `SceneAsset` 变成一棵“最终运行时树”，而是先构建“资产注册表 + 模板 + 运行时实例 + GPU 驻留”四层模型。**

### 5.1 四层模型

```text
nr.load::SceneAsset
    -> Normalize / Bridge
        -> Asset Registry (CPU canonical resources)
        -> Scene Template Registry (prefab/tree template)
            -> Runtime ECS Instances
                -> View Extraction
                -> Upload Planner / GPU Residency
                    -> nr.rhi
```

四层职责：

1. **Asset Registry**
   - 管 canonical CPU 资源
   - 做去重
   - 管版本号
   - 管 GPU 驻留与延迟回收

2. **Scene Template Registry**
   - 管导入场景的模板树
   - 管 prefab root / template metadata
   - 管“这个模板引用了哪些资源”

3. **Runtime ECS Instances**
   - 管实例层级
   - 管本帧更新所需的实例态
   - 管 transform / bounds / visibility input

4. **View Extraction**
   - 基于 view/camera 提取 render packets
   - 不能把多视图限制为 Flecs 单 pipeline 输出

### 5.2 推荐模块布局

```text
src/scene/
  CMakeLists.txt
  exportModule.ixx
  nrSceneType.ixx
  nrSceneAssetRegistry.ixx
  nrSceneBridge.ixx
  nrSceneTemplate.ixx
  nrSceneComponents.ixx
  nrSceneHierarchy.ixx
  nrSceneRuntime.ixx
  nrSceneUpload.ixx
  nrSceneView.ixx
  nrSceneDiagnostics.ixx
  nrScene.ixx
```

推荐职责：

1. `type`
   - 对外句柄
   - create info
   - report / diagnostics
   - frame stamp / view input

2. `assetRegistry`
   - Phase 1 必需：`MeshHandle` / `MaterialHandle` / `TextureHandle`
   - Phase 7 扩展：`SkeletonHandle` / `AnimationClipHandle` / `CameraAssetHandle` / `LightAssetHandle` / `ParticleSetHandle`
   - canonical key
   - versioned residency
   - GC queues

3. `bridge`
   - `load::* -> resource::*` 规范化
   - `rebuild*` / `validate()` / `valid()` 协议
   - import diagnostics / reject policy

4. `template`
   - scene template handle
   - prefab root
   - template-to-resource pin set

5. `components`
   - runtime ECS components
   - tags
   - trait registration

6. `hierarchy`
   - Parent / ChildOf policy
   - transform propagation
   - bounds propagation

7. `runtime`
   - instantiate / destroy
   - root mapping
   - scene frame bookkeeping

8. `upload`
   - upload queue
   - budgeting
   - upload tickets / acquire requirements
   - delayed destroy

9. `view`
   - render candidate query
   - culling input
   - sort key generation
   - render packet output

10. `diagnostics`
   - Flecs stats bridge
   - table/rematch/query counters

### 5.3 关键句柄与时间戳

Scene 应该延续 `nr.resource` 的 handle 模式：

```cpp
export namespace nr::scene
{
using SceneTemplateHandle = nr::resource::Handle<struct SceneTemplateTag>;
using SceneInstanceHandle = nr::resource::Handle<struct SceneInstanceTag>;

struct SceneFrameStamp
{
    std::uint32_t frameSlot = 0;   // Ring-buffer slot from Device::beginFrame()
    std::uint64_t frameSerial = 0; // Monotonic serial owned by Scene
};
}
```

设计要点：

1. `frameSlot` 只用于对接 `resourcePool.resetFrame(frameSlot)` 这一类 ring-buffer 行为。
2. `frameSerial` 只用于资源版本并存、回收、安全 retire。
3. 二者绝不能混用。

### 5.4 对外 API（修订版）

旧接口的根本问题是“导入”和“实例化”没分开。修订后必须拆开。

```cpp
export module nr.scene;

import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import std;

export namespace nr::scene
{
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

struct SceneCreateInfo
{
    nr::rhi::Device& device;
    std::size_t uploadBudgetBytesPerFrame = 128ull * 1024ull * 1024ull;
    CpuRetentionPolicy cpuRetention = CpuRetentionPolicy::keepAll;
    bool enableDiagnostics = false;
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

struct SceneViewInput
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::uvec2 renderExtent{};
    flecs::entity camera{};
    std::uint64_t viewId = 0;
};

class Scene
{
  public:
    explicit Scene(const SceneCreateInfo& createInfo);
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] flecs::world& ecs() noexcept;
    [[nodiscard]] const flecs::world& ecs() const noexcept;

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

    [[nodiscard]] SceneRenderList extractRenderList(const SceneViewInput& view) const;

  private:
    nr::rhi::Device& device_;
};
}
```

设计说明：

1. `beginFrame(frameSlot)` 由外部在 `device.beginFrame()` 之后立即调用。
2. `Scene` 自己维护 `frameSerial_`，不把这个责任丢给调用方。
3. `extractRenderList(view)` 是显式 view-driven API，不强塞进 ECS pipeline。
4. 如果需要“导入即实例化”便利接口，可以在此接口之上再包一层 convenience API，但不能反过来把 convenience 设计成核心接口。

### 5.5 资源注册表：按“资产”而不是“实体”做引用计数

旧设计里最危险的方向，是想让每个 ECS 实体通过 hooks retain/release 自己引用的 mesh/material/texture。

修订后改成：

1. **资源 pinning 以 template/instance 为单位，而不是以每个 node 为单位。**
2. ECS 节点上仍然保存 `MeshHandle/MaterialHandle/...`，但它们只是查询引用，不驱动 retain/release。
3. 资源真正的“活跃引用”来自：
   - template 注册
   - runtime instance 存在
   - optional explicit pin

推荐结构：

```cpp
template <typename HandleT, typename CpuT, typename GpuT>
struct AssetRecord
{
    struct ResidentVersion
    {
        GpuT gpu{};
        std::uint64_t gpuVersion = 0;
        std::uint64_t readyAfterSerial = 0;
        std::uint64_t retireAfterSerial = 0;
    };

    HandleT handle{};
    std::string stableKey{};
    CpuT cpu{};

    std::uint64_t cpuVersion = 1;
    std::uint32_t liveTemplatePins = 0;
    std::uint32_t liveExplicitPins = 0;

    std::deque<ResidentVersion> residentVersions{};
    bool uploadQueued = false;
};
```

设计要点：

1. `cpuVersion` 变化表示 CPU 数据已变更。
2. `residentVersions.back().gpuVersion == cpuVersion` 才表示最新版本 resident。
3. `residentVersions` 允许短时间多版本并存，避免 in-flight 资源悬挂。
4. 回收条件看 `retireAfterSerial`，不是 `lastUsedFrameSlot`。

### 5.5.1 Registry family 必须镜像 `nr.resource`

第一版强制落地：

1. `MeshRegistry -> nr::resource::MeshHandle`
2. `MaterialRegistry -> nr::resource::MaterialHandle`
3. `TextureRegistry -> nr::resource::TextureHandle`

后续扩展也必须保持分拆：

4. `SkeletonRegistry -> nr::resource::SkeletonHandle`
5. `AnimationClipRegistry -> nr::resource::AnimationClipHandle`
6. `CameraRegistry -> nr::resource::CameraAssetHandle`
7. `LightRegistry -> nr::resource::LightAssetHandle`
8. `ParticleAuthoringRegistry -> nr::resource::ParticleSetHandle`

明确禁止：

1. 新造一个模糊的 `AnimationHandle`
2. 新造一个模糊的 `CameraLightHandle`
3. 把 camera/light/particle/skeletalAnimation 混成一个“扩展资源桶”

### 5.5.2 `nr.resource` 是 Scene bridge 的 canonical validation boundary

每条 bridge 路径都应遵守同一份合同：

1. 先构造 `nr.resource` 值对象
2. 运行必要的规范化步骤
3. 调用对应 `validate()` / `valid()` / `validateHierarchy()` 接口
4. 失败则记录 import diagnostics，并拒绝注册到 AssetRegistry

对当前静态路径，最低要求是：

1. `resource::Mesh`
   - 必要时修复法线/切线
   - `rebuildLocalBounds()`
   - `rebuildLocalSphere()`
   - `validate()`
2. `resource::Texture`
   - `valid()`
3. `resource::Material`
   - Scene 侧补做句柄存在性与槽位引用合法性检查

### 5.6 模板层：SceneAsset 先变模板，再变实例

这是本次修订最重要的结构性变化。

```cpp
struct SceneTemplateRecord
{
    SceneTemplateHandle handle{};
    std::string stableKey{};
    flecs::entity prefabRoot{};

    std::vector<nr::resource::MeshHandle> meshes{};
    std::vector<nr::resource::MaterialHandle> materials{};
    std::vector<nr::resource::TextureHandle> textures{};

    std::uint32_t liveInstanceCount = 0;
    TemplateHierarchyPolicy hierarchyPolicy = TemplateHierarchyPolicy::autoSelect;
};
```

当前阶段只强制填充 `meshes/materials/textures`，但模板接口与统计结构应从一开始就预留未来 family：

1. `nr::resource::SkeletonHandle`
2. `nr::resource::AnimationClipHandle`
3. `nr::resource::CameraAssetHandle`
4. `nr::resource::LightAssetHandle`
5. `nr::resource::ParticleSetHandle`

模板层带来的收益：

1. 多实例不重复 build ECS 子树。
2. 多实例不重复 retain 单个 mesh/material/texture。
3. prefab override、variant、LOD、instance local override 都有明确落点。
4. 后续 camera/light/animation 导入可以继续挂在 template 上，不污染 runtime 层。

### 5.7 层级存储策略：明确混用，不自创关系

#### 5.7.1 强制规则

1. 只使用 Flecs 内建 `flecs::ChildOf` 和 `flecs::Parent`。
2. 不再自定义一套 Parent relationship 并手动补 `Traversable/Acyclic/OnDeleteTarget`。
3. 同一个 entity 不能同时拥有 `ChildOf` 与 `Parent` 层级语义。

#### 5.7.2 推荐默认策略

| 场景 | 默认存储 | 原因 |
|---|---|---|
| runtime world root / streaming cell / 大型动态挂接 | `ChildOf` | parent child 数量大，增删频繁 |
| 导入模型模板 / prefab 内部层级 / 小型重复实例树 | `Parent` | 降碎片、利于 prefab 实例化 |
| 单 parent 下有成千上万 child 的动态集合 | `ChildOf` | `Parent` 删除 child 为 O(n) |
| 热路径依赖 `(ChildOf, root), Component` 过滤 | `ChildOf` | 针对特定 parent 的查询更强 |

#### 5.7.3 `autoSelect` 的建议规则

`TemplateHierarchyPolicy::autoSelect` 推荐这样落地：

1. 对“导入模板”默认 `preferParent`
2. 对“runtime attach 到世界根”的关系默认 `ChildOf`
3. 如果模板 root 的直接子节点数非常大，允许模板创建阶段强制切回 `preferChildOf`

这让 Scene 同时吃到：

1. prefab/template 的低碎片优势
2. runtime world root 的明确分组与可管理性

### 5.8 ECS 组件策略：稳定实例态进 ECS，高频状态留在 Scene 私有队列

#### 5.8.1 适合常驻 ECS 的组件

建议保留以下类型：

1. `LocalTransform`
2. `WorldTransform`
3. `LocalBounds`
4. `WorldBounds`
5. `SceneInstanceRef`
6. `NodeTemplateRef`
7. `MeshRef`
8. `MaterialOverride`
9. `StaticObject`
10. `DynamicObject`
11. `MainCamera`

#### 5.8.2 不应作为主路径 ECS tag 的状态

以下状态不应通过 add/remove tag 驱动：

1. `UploadPending`
2. `GpuDirty`
3. `DestroyPending`
4. `VisibleThisView`
5. `PendingRetain`
6. `PendingRelease`

这些状态应落在：

1. registry dirty bitset
2. upload queue
3. GC queue
4. per-view scratch list
5. Scene diagnostics buffer

#### 5.8.3 可见性语义

推荐拆成两层：

1. **长寿命启用态**
   - 例如 `Disabled` / authoring hidden
   - 改动频率低，可以保留为 ECS 状态

2. **每 view 的 culling 结果**
   - 不能通过 ECS 结构修改表达
   - 只能写入 `RenderList` scratch / bitset / packet array

### 5.9 导入桥接规则：从当前 `SceneAsset` 过渡到未来统一资源模型

当前 `load` 与 `resource` 之间并非一一对应，必须明确桥接规则。

#### 5.9.0 Bridge 总合同

Scene 不是把 loader payload 原样“搬运”到 registry，而是负责把它们变成 `nr.resource` 的 canonical 值对象。

这一步必须同时完成：

1. provenance 到 canonical key 的映射
2. 资源值对象构造
3. 必要 normalize
4. `nr.resource` 自带验证函数
5. Scene 自身补充的跨资源引用检查

#### 5.9.1 Mesh 桥接

当前 `load::MeshAsset` 只有：

1. 顶点数组
2. 索引数组
3. 单一 `materialIndex`

而 `nr.resource::Mesh` 已支持 `submeshes`。

因此第一阶段强制规定：

1. 每个 `load::MeshAsset` 规范化为一个 `nr::resource::Mesh`
2. 该 `Mesh` 恰好创建一个 `Submesh`
3. `Submesh.material` 由 `mesh.materialIndex` 映射

这样做的好处是：

1. 不阻断当前实现
2. 后续 loader 扩展为 multi-primitive / multi-submesh 时，runtime 与 render list API 不必大改
3. 可以在注册前执行 `rebuildLocalBounds()`、`rebuildLocalSphere()` 与 `validate()`，把 `Mesh` 作为真正的 canonical CPU mesh

#### 5.9.2 Texture 桥接

纹理 key 直接复用当前 loader 的结果：

1. embedded 纹理：`*N`
2. external 纹理：loader 已归一化后的 path key

并且在进入 registry 前必须满足：

1. `resource::Texture::valid() == true`
2. `sourcePath` / key / `srgb` / `format` 的语义不互相冲突

#### 5.9.3 Material 桥接

材质的 canonical key 推荐使用：

```text
<scene source path>::material[<index>]
```

原因：

1. 足够稳定
2. 不要求一开始就做昂贵内容 hash
3. 和当前 Assimp material index 天然对齐
4. 便于 Scene 在 bridge 阶段补做“引用的 `TextureHandle` 是否存在”这类关系检查

#### 5.9.4 Mesh 桥接 key

同理：

```text
<scene source path>::mesh[<index>]
```

#### 5.9.5 去重策略分层

第一版不建议上来就做跨文件内容哈希去重，建议分两层：

1. **Identity dedupe**
   - 按 source path + index / normalized texture key 去重
   - 便宜、稳定、可调试

2. **Content dedupe（后续优化）**
   - 对纹理/mesh 做内容 hash
   - 只作为可选后台优化

#### 5.9.6 后续扩展桥接必须按 `resource` family 分拆

后续扩展不应再使用笼统的“animation resource”说法，而应显式拆为：

1. `Skeleton`
   - 骨骼层级与 bind pose
   - 注册前必须过 `validateHierarchy()`
2. `AnimationClip`
   - 单独的 clip 注册表与版本管理
   - 注册前必须过 `valid()`
3. `CameraAsset`
   - 主要属于 template/authoring 输入
   - runtime ECS 持有的是解析后的实例态与 override
4. `LightAsset`
   - 主要属于 template/authoring 输入
   - runtime ECS 持有的是解析后的实例态与 override
5. `FluidParticleSet`
   - 只适合作为 authoring/seed 数据
   - live particle buffer、spawn/update scratch 不进入 persistent AssetRegistry

### 5.10 帧生命周期：Scene 自己管理 frame serial

这是资源回收正确性的关键。

#### 5.10.1 运行时约束

每帧推荐顺序：

1. `auto begin = device.beginFrame();`
2. `scene.beginFrame(begin.frameIndex);`
3. `scene.updateSimulation({deltaSeconds});`
4. `scene.extractRenderList(view)`，可调用多次
5. `scene.uploadPending();`
6. renderer 录制与提交
7. `device.endFrame(batch);`

#### 5.10.2 Scene 内部需要维护的状态

```cpp
struct FrameSlotState
{
    std::uint64_t lastSubmittedSerial = 0;
};
```

`Scene::beginFrame(frameSlot)` 做三件事：

1. `++frameSerial_`
2. 由于 `Device::beginFrame()` 已等待 `frameSlot` 对应 fence，说明该 slot 上一次提交的 serial 已完成
3. 用 `slotStates_[frameSlot].lastSubmittedSerial` 推进 GC

这意味着 Scene 不需要外部传入 `completedFrameIndex`。

#### 5.10.3 `collectGarbage()` 的推荐模型

GC 变成 Scene 内部细节：

1. `beginFrame(frameSlot)` 时推进 “某个 slot 上次提交 serial 已完成”
2. 把所有 `retireAfterSerial <= completedSerial` 的旧版本释放
3. 对于 upload ring/timeline 相关 staging 资源，则继续以 upload timeline value 回收

### 5.11 GPU 驻留策略：三类资源、三条路径

必须尊重当前 RHI 的真实分层。

#### 5.11.1 Persistent asset resources

这些资源使用 `device.resourceFactory` 创建：

1. vertex/index buffers
2. static material buffers
3. textures
4. BLAS/TLAS（后续阶段）

它们生命周期跨 frame。

#### 5.11.2 Per-frame transient resources

这些资源使用 `device.resourcePool.allocateTransientBuffer/allocateTransientImage(frameSlot)`：

1. instance data
2. skin palette
3. per-view culling result
4. temporary indirect args
5. frame-local material patch buffer

它们绝不进入长期 registry。

#### 5.11.3 Upload staging / readback

这些资源依赖 `nr::rhi::ops::UploadReadbackContext`：

1. 大 buffer 上传
2. GPU readback
3. queue-family ownership transfer
4. upload timeline 驱动的 staging ring reclaim

### 5.12 上传语义：`resident` 之前要经过 acquire

当前 RHI 的 upload helper 语义决定了 Scene 必须建模一个中间态。

推荐状态机：

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

解释：

1. `uploadQueued`
   - transfer submit 已经排队，但还不能被消费

2. `waitingAcquire`
   - transfer 复制已完成
   - 还需要目标队列记录 acquire barrier，或已经发放 upload ticket 但尚未被消费侧接入

3. `resident`
   - upload + acquire 都已完成
   - 可以被 graphics/compute 正常读取

#### 5.12.1 当前阶段对 buffer 的做法

1. mesh/material buffer upload 直接走 `UploadReadbackContext::uploadBuffer()`
2. Scene 为新版本保存 upload ticket
3. renderer 或 Scene upload phase 在首次消费命令缓冲里记录 acquire barrier

#### 5.12.2 当前阶段对 image 的要求

由于 RHI 当前还没有对称 `uploadImage()` helper：

1. Scene 实现纹理上传前，必须先为 `nr.rhi::ops` 增加薄封装的 image upload 接口
2. 不建议把 image transfer 状态机私自复制一份到 Scene

这条是硬约束，否则 Scene 会绕开已有 RHI 的上传模型。

### 5.13 查询与系统策略：Simulation 用 systems，Render extraction 用 manual cached queries

这也是本次修订的重要变化。

#### 5.13.1 哪些适合 Flecs systems / pipeline

这些工作适合每帧一次、世界态驱动：

1. 动画采样
2. 局部变换到世界变换传播
3. bounds 更新
4. 校验/修正
5. 长寿命结构同步

#### 5.13.2 哪些不适合塞进 pipeline

这些工作是 view-dependent 或 device-driven：

1. 主相机 RenderList
2. shadow views RenderList
3. reflection / probe views
4. upload budgeting
5. upload acquire barrier 接入

它们应该是：

1. 显式 API
2. 手动运行的 cached queries
3. 输出到 view scratch / render packet array

#### 5.13.3 sync point 注解规则

只要系统做了 ECS 读写旁路，就必须注解：

1. 调用 `get<T>()` 读取非 query 字段：`read<T>()`
2. 调用 `set/add/remove` 改别的组件：`write<T>()`
3. 只有确实要立即可见时，才考虑 `immediate()`

### 5.14 RenderList：不要让 Flecs 负责最终 draw ordering

Flecs query sorting 很强，但不应该替代 renderer 所需的主排序。

推荐做法：

1. Flecs 只负责快速找出 render candidates
2. Scene 生成线性 `DrawPacket`
3. Scene 用 64-bit 或 128-bit sort key 排序

推荐排序维度：

1. pass
2. pipeline family
3. material
4. mesh
5. submesh
6. instance

不建议：

1. 用高基数 pair 当 query 主 group key
2. 用 query sorting 直接按 material/mesh 每帧全局排序

### 5.15 诊断与性能观测：必须设计进去，而不是出问题再补

需要预留以下指标：

1. Flecs table count
2. cached query count
3. rematching count / time
4. hierarchy type usage（ChildOf vs Parent）
5. render candidate count
6. upload bytes per frame
7. resident asset count
8. retired asset count
9. empty table cleanup count（如果后续启用）

开发版建议：

1. dev-only 接 Flecs `stats`/explorer
2. 场景压测时重点观察 rematching 与 table fragmentation

---

## 6. 分阶段实施计划（附 Prompt）

下面的阶段不是“想到哪做到哪”，而是围绕当前仓库状态和上述设计依赖有序推进。

### Phase 0：构建链与 Flecs 依赖接入

**目标**

让 `nr.scene` 真正进入构建图，并能在 C++23 module 环境里使用 Flecs。

**必须完成**

1. 新建 `src/scene` 目录与基础 module/CMake 骨架
2. `src/extern/CMakeLists.txt` 接入 `flecs`
3. `dependency` 模块导出 Flecs C++ API
4. 修复 `src/CMakeLists.txt` 中 `nrscene` 链接断点

**Prompt**

```text
请在 Newbie-Renderer 中完成 Scene 模块的最小可编译接入：
1) 新建 src/scene 与基础 CMake/module 骨架，生成 nrscene 静态库。
2) 在 src/extern/CMakeLists.txt 中接入 flecs 子模块，并让 dependency 模块导出 Flecs C++ API。
3) 修复 src/CMakeLists.txt，使 nrscene 真正参与构建并被 main 链接。
4) 保持 C++23 modules 风格，不引入新的原始 owning pointer。
要求：
- Scene 对 Device 使用引用语义。 
- 不引入 raw Vulkan C API。
- 你可以根据需要修改现有 nr.load / nr.resource 公共接口,但在修改之前你需要给出详细的修改原因并向我确认. 
```

### Phase 1：资产注册表与模板层

**目标**

先把“导入资源”和“实例化”拆开，建立正确的数据模型。

**必须完成**

1. `SceneTemplateHandle` / `SceneInstanceHandle`
2. Asset registry（mesh/material/texture）
3. canonical key 方案
4. `registerTemplate()` 与 `instantiate()` 双阶段 API
5. 每个 template 的 pin set / live instance count
6. `nrSceneBridge` 的最小接口与 import diagnostics 结构

**Prompt**

```text
请实现 nr.scene 的资产注册表与模板层：
1) 引入 SceneTemplateHandle / SceneInstanceHandle，并把“注册模板”和“实例化”拆成两个 API。
2) 为 mesh/material/texture 建立 AssetRegistry，使用 slot+generation 句柄管理。
3) 定义第一版 canonical key：
   - texture 复用 load::TextureAsset.key
   - material 使用 `<scene source path>::material[index]`
   - mesh 使用 `<scene source path>::mesh[index]`
4) 新建最小 `nrSceneBridge` 接口与 import diagnostics 结构，明确后续 `load::* -> resource::*` 转换边界。
5) 让 template 持有 prefab root 与资源 pin set，实例化只增加 template 实例引用，不对每个节点单独 retain/release。
要求：
- ECS 实体上只保留轻量 handle，不存放大块 CPU 资源。
- retain/release 主路径不要依赖 observer 或 hook。
- registry family 命名必须与 `nr.resource` 的 handle aliases 对齐，不要自造 `AnimationHandle` / `CameraLightHandle`。
```

### Phase 2：静态 SceneAsset -> resource/template/runtime 闭环

**目标**

先闭合当前仓库已经真实支持的静态场景路径。

**必须完成**

1. `load::SceneAsset` 到 `resource::Mesh/Material/Texture` 的 bridge
2. 节点树模板化
3. runtime 实例根创建
4. node 命名冲突处理
5. `load::MeshAsset -> resource::Mesh(one submesh)` 规则
6. bridge 后的 normalize + validate 合同

**Prompt**

```text
请实现当前静态 SceneAsset 的导入闭环：
1) 把 load::SceneAsset 转换为 resource::Mesh / Material / Texture，并注册到 Scene AssetRegistry。
2) 每个 load::MeshAsset 规范化为一个 resource::Mesh，且恰好包含一个 Submesh。
3) 在注册前运行 bridge 规范化与验证：
   - mesh 至少执行 `rebuildLocalBounds()`、`rebuildLocalSphere()`、`validate()`
   - texture 至少执行 `valid()`
   - material 补做引用句柄合法性检查
4) 基于导入的 nodes 构建 Scene template tree，并支持 runtime instantiate。
5) 为导入节点实现确定性命名规则，解决同级重名冲突。
6) 返回模板统计、实例统计与导入诊断信息。
要求：
- 只覆盖当前 load 已支持的数据范围：nodes / meshes / materials / textures。
- 不提前承诺 `skeletalAnimation` / `camera` / `light` / `particle` 路径。
```

### Phase 3：层级、变换与 bounds 系统

**目标**

建立稳定的 runtime world 更新主路径。

**必须完成**

1. `LocalTransform` / `WorldTransform`
2. `LocalBounds` / `WorldBounds`
3. `ChildOf` / `Parent` 混合策略
4. 变换传播与 bounds 更新查询缓存
5. 非 traversal fallback 策略

**Prompt**

```text
请实现 Scene 的层级、变换与 bounds 系统：
1) 注册 LocalTransform / WorldTransform / LocalBounds / WorldBounds 等原子组件。
2) 使用 Flecs cached queries 或 systems 更新世界矩阵与世界包围体。
3) 模板内部优先使用 flecs::Parent，runtime world root / attach 关系使用 flecs::ChildOf。
4) 不要引入自定义 Parent relationship；直接使用 Flecs 内建 ChildOf / Parent。
5) 对潜在 traversal 热路径预留 split-query 策略，避免 rematching 成为后续瓶颈。
要求：
- 热路径 query 只创建一次并复用。
- 组件保持小而原子。
- 不把 per-view 可见性写回 ECS tag。
```

### Phase 4：GPU 上传、版本并存与安全回收

**目标**

让 Scene 真正具备“资源管理系统”的核心能力。

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
5) 为纹理上传补齐薄 RHI helper（优先扩展 nr.rhi::ops，而不是在 Scene 重写一套 transfer 状态机）。
6) 使用 retireAfterSerial 执行多版本并存与延迟销毁。
要求：
- 释放逻辑不能依赖 observer。
- 不要把 UploadPending / GpuDirty / DestroyPending 做成 ECS 主路径 tag。
```

### Phase 5：多视图 RenderList 提取

**目标**

让 Scene 成为 renderer 可直接消费的数据提供者，而不是单视图 demo。

**必须完成**

1. 显式 `extractRenderList(view)` API
2. 多 view 可重复调用
3. render candidate 查询缓存
4. `DrawPacket` / sort key
5. coarse grouping 预留

**Prompt**

```text
请实现 Scene 的多视图 RenderList 提取：
1) 提供显式的 `extractRenderList(const SceneViewInput&)` API，而不是把 RenderList 固定成 Flecs pipeline 的最终阶段。
2) 使用 cached query 提取 render candidates，支持同一帧多次按不同 view 调用。
3) 生成线性 DrawPacket / DrawItem，并按 pass / pipeline / material / mesh / submesh 生成 sort key。
4) 把每 view 可见性结果保留在 scratch / render packet 中，不写回 ECS 结构状态。
5) 为后续 group_by(world cell / scene partition) 优化预留接口，但不要用 material 这类高基数资源当 query 分组键。
要求：
- 主排序不要依赖 Flecs sorted query 完成。
- 允许后续扩展 shadow / reflection / probe views。
```

### Phase 6：诊断、压测与 Flecs 性能策略固化

**目标**

把“设计判断”变成“可量化结论”。

**必须完成**

1. rematching / table fragmentation 观测
2. ChildOf vs Parent 的实际压测
3. query cache / table count / upload bytes 统计
4. dev-only Flecs stats / explorer 接入

**Prompt**

```text
请为 Scene 模块加入 Flecs 与资源管理诊断能力：
1) 记录 table count、cached query count、rematching 次数或耗时、upload bytes、resident asset count。
2) 设计 3 档规模压测场景，对比 ChildOf / Parent 在模板层与 runtime attach 层的表现。
3) 在开发构建中接入 Flecs stats / explorer 所需模块或桥接代码，便于观察 rematching 与 archetype fragmentation。
4) 输出默认策略建议，并把压测结论回写设计文档。
要求：
- 只在 dev build 启用重型诊断。
- 将“何时切换 cached traversal query -> uncached / split query”的阈值写清楚。
```

### Phase 7：扩展 load 覆盖面（`skeletalAnimation`、`camera`、`light`、`particle`、RT）

**目标**

在静态路径稳定后，再把未来能力接进来。

**必须完成**

1. 扩展 `nr.load` 输出 skeleton / animation clip / camera / light 所需数据
2. Scene 分别接入 `SkeletonHandle` / `AnimationClipHandle` / `CameraAssetHandle` / `LightAssetHandle` / `ParticleSetHandle`
3. 区分 authoring asset 与 live runtime data
4. 高频 skin palette / particle buffer 改走 per-frame transient
5. 规划 BLAS/TLAS 挂接点

**Prompt**

```text
请扩展 Scene 模块到 `skeletalAnimation` / `camera` / `light` / `particle` / RT 资源管理路径：
1) 先扩展 nr.load，使 SceneAsset 能分别输出 skeleton、animation clip、camera、light 所需数据。
2) 在 Scene 中分别建立 `SkeletonHandle`、`AnimationClipHandle`、`CameraAssetHandle`、`LightAssetHandle`、`ParticleSetHandle` 对应的桥接与注册路径。
3) CameraAsset / LightAsset 进入 template 与 runtime authoring 路径；FluidParticleSet 只作为 seed/authoring 数据，live particle 数据仍走 transient scratch/buffer。
4) skin palette、particle 数据统一走 per-frame transient buffer，不进入 persistent asset registry。
5) 为 mesh 资源补充可选 BLAS，为场景视图提取补充 TLAS build input。
要求：
- 不要破坏前面静态 mesh scene 的路径。
- 高频数据继续遵守“ECS 保存引用与状态，buffer 数据走 frame-local scratch”的原则。
```

---

## 7. 验收标准

### 7.1 工程与接口

1. `nrscene` 真正进入构建图并可被 `main` 链接。
2. `dependency` 可导出 Flecs C++ API。
3. Scene 对 `nr::rhi::Device` 使用必需非拥有引用语义。
4. Scene 不再暴露“把 `Device*` 传进来允许为空”的接口。

### 7.2 数据模型

1. `SceneAsset` 导入路径先生成 template，再生成 instance。
2. 资源 registry 与 ECS 实体边界清晰。
3. 当前静态 mesh/material/texture/node 路径能够闭环。
4. 后续扩展严格按 `SkeletonHandle` / `AnimationClipHandle` / `CameraAssetHandle` / `LightAssetHandle` / `ParticleSetHandle` 分拆接入。
5. `particle` 被明确区分为 authoring/seed 数据与 live runtime data 两类路径，而不是伪装成普通静态资产。

### 7.3 GPU 生命周期

1. `frameSlot` 与 `frameSerial` 语义明确分离。
2. persistent / transient / staging 三类路径不混用。
3. 上传后的 acquire barrier 语义被建模。
4. 旧版本资源按 `retireAfterSerial` 回收，不再按模糊 frame index 回收。

### 7.4 Flecs 使用方式

1. 热路径 query 只创建一次并复用。
2. observer 不承担主资源生命周期逻辑。
3. 高频状态变化不通过 ECS tag 结构修改表达。
4. `ChildOf` / `Parent` 使用策略有明文规则并有压测支持。

### 7.5 渲染提取

1. `extractRenderList(view)` 支持多视图。
2. RenderList 不耦合为单次 `world.progress()` 产物。
3. 主排序使用 CPU sort key，而不是完全依赖 Flecs sorted query。

---

## 8. 参考资料

### 8.1 仓库内资料

说明：

1. `docs/RESOURCE_*` 提供了有价值的分析与术语整理。
2. 如果文档中的模块名与 `src/resource/exportModule.ixx` 不一致，应以源码导出面为准。

1. `src/extern/flecs/docs/Queries.md`
2. `src/extern/flecs/docs/HierarchiesManual.md`
3. `src/extern/flecs/docs/ObserversManual.md`
4. `src/extern/flecs/docs/Systems.md`
5. `src/extern/flecs/docs/DesignWithFlecs.md`
6. `docs/RESOURCE_MODULE_ASSESSMENT.md`
7. `docs/RESOURCE_MODULE_EXECUTIVE_SUMMARY.md`
8. `docs/RESOURCE_SYSTEM_ARCHITECTURE.md`
9. `src/load/nrLoadType.ixx`
10. `src/load/nrLoadAssimp.ixx`
11. `src/load/nrLoadDecode.ixx`
12. `src/resource/exportModule.ixx`
13. `src/resource/nrResourceHandle.ixx`
14. `src/resource/nrResourceMesh.ixx`
15. `src/resource/nrResourceMaterial.ixx`
16. `src/resource/nrResourceCamera.ixx`
17. `src/resource/nrResourceLight.ixx`
18. `src/resource/nrResourceSkeletalAnimation.ixx`
19. `src/resource/nrResourceParticle.ixx`
20. `src/rhi/nrDevice.ixx`
21. `src/rhi/nrFrameContext.ixx`
22. `src/rhi/nrResourcePool.ixx`
23. `src/rhi/nrMemoryAllocator.ixx`
24. `src/rhi/nrResourceOps.ixx`
25. `src/extern/CMakeLists.txt`
26. `src/CMakeLists.txt`

### 8.2 Flecs 官方文档

1. Queries: https://www.flecs.dev/flecs/md_docs_2Queries.html
2. Hierarchies: https://www.flecs.dev/flecs/md_docs_2HierarchiesManual.html
3. Observers: https://www.flecs.dev/flecs/md_docs_2ObserversManual.html
4. Systems: https://www.flecs.dev/flecs/md_docs_2Systems.html
5. Designing with Flecs: https://www.flecs.dev/flecs/md_docs_2DesignWithFlecs.html

---

这版修订后的核心变化可以概括为三句话：

1. Scene 先做“模板 + 实例”，再做“渲染提取”。
2. GPU 生命周期以 `frameSerial` 为准，不再混淆 `frameSlot`。
3. Flecs 只负责它擅长的表格化状态与查询，不替代资源队列、上传状态机和多视图渲染调度。
