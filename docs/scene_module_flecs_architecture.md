# Scene 模块架构说明（`nr.scene` + Flecs）

这份文档只保留 `nr.scene` 当前已经成立的架构边界、主数据流和稳定契约，不再记录旧阶段历史、性能对比实验或实施流水账。

## 1. 模块职责

`nr.scene` 是当前工程的运行时世界层与桥接层，位于 `load`、`resource`、`rhi` 与 `renderer` 之间。

它负责：

- 把 `nr::load::SceneAsset` 转成项目内部可稳定引用的 canonical key 与 registry 记录
- 把 load 层的 enum 材质 texture semantic 映射到 `nr.resource` 的 enum texture slot，并按 factor 或 texture 存在性创建可选 PBR extension block
- 将 specular-glossiness factor 近似转换为 metallic-roughness canonical material，并对可能丢失的输入发出 warning
- 维护 mesh / material / texture / camera / light 的 CPU registry
- 管理 template / instance 生命周期
- 承载 Flecs world、层级关系、活动实例过滤和 world transform 更新
- 管理 GPU 上传、acquire、驻留与延迟回收
- 根据 selector/profile 抽取运行时 packet
- 解析 imported primary camera，并在缺失时提供 fallback runtime camera
- 把 `ScenePacketSet` 进一步桥接成 `SceneBridgeFrame`

它不负责：

- 文件导入与解码
- renderer graph 生命周期
- render pass 业务实现
- viewer 侧键鼠输入和自由相机控制

## 2. 当前主数据流

当前稳定主路径可以概括为：

```text
nr::load::SceneAsset
  -> SceneBridge::buildPlan(...)
  -> Scene::registerTemplate(...)
  -> Scene::instantiate(...)
  -> beginFrame / uploadPending / updateSimulation
  -> extractPackets(profile, input)
  -> ScenePacketSet
  -> SceneRenderBridge::buildFrame(...)
  -> SceneBridgeFrame
```

其中：

- `SceneBridgePlan` 负责 canonical key 规划与 source-index 对齐
- `Scene` 负责 registry、template/instance、Flecs world 与 GPU 生命周期
- `SceneRenderBridge` 负责把 scene packet 变成 renderer 更容易消费的 frame 结构

## 3. 当前稳定输出边界

### 3.1 抽取边界

`SceneExtractProfileCreateInfo` + `SceneExtractInput` 是当前 packet 抽取的稳定入口。

当前语义重点：

- packet 抽取是 profile-first，不是 multi-view render-list-first
- `SceneVisibilityMode::primaryCameraFrustum` 会走 scene 内部主相机解析
- `SceneVisibilityMode::customFrustum` 已经允许上层提供自定义 frustum
- `viewportExtent` 会影响投影矩阵与 frustum 计算

### 3.2 相机边界

当前公开的相机运行时结果是 `SceneResolvedCamera`。

它表达的是：

- scene 中已解析出的主相机
- 或 scene 自动提供的 fallback camera

它不表达：

- app/viewer 侧自由相机控制器
- 键鼠输入状态

### 3.3 renderer 边界

当前 scene 对 renderer 的首选边界是 `SceneBridgeFrame`，而不是 renderer 直接读取 scene 内部 registry 或 Flecs query。

`SceneBridgeFrame` 当前已经稳定承载：

- raster draw 列表
- material grouping 与材质 raster state
- frame 级 scene geometry atlas vertex/index buffer binding
- frame 常量（view / projection / viewProjection / cameraWorld / drawCount）
- 每个 draw 的 `SceneBridgeDrawGeometry`，通过 `SceneRenderBridgeBuildInput::resolveRasterDrawGeometry` 解析成 render-pass 可直接消费的 atlas-adjusted draw count、offset 与 front-face state
- 每个 draw 的 `SceneBridgeMaterialRasterState`，通过 `SceneRenderBridgeBuildInput::resolveMaterialRasterState` 解析材质双面/剔除策略；缺失时默认单面 back-face culling

这意味着：

- renderer / renderPasses 不需要读取 scene 内部 registry 或 Flecs query 来绘制 mesh
- raster packet 按 mesh geometry fan-out；每个 draw 的材质来自 `MeshGeometry::material`
- ray tracing / TLAS packet 按 node mesh instance fan-out；mesh 是未来 BLAS 单元，`MeshGeometry` 是未来 BLAS geometry 与材质表映射单元；RT/TLAS 抽取不能使用 camera/frustum culling，必须覆盖整个 active RT scene
- scene geometry atlas buffer 是 scene-owned GPU-only 资源；它们包含 raster vertex/index、AS build-input 和 shader-device-address usage，并在 transfer / graphics / compute queue family 不同时使用 concurrent sharing，使 graphics raster 与 compute AS build 都能消费同一份 resident geometry
- `Scene::tryGetAccelerationStructureMesh(...)` 是 renderer AS node 的最小查询边界；它暴露 resident mesh 的 atlas binding、geometry ranges、GPU version、opaque classification，以及由 mesh winding / material double-sided 状态得到的 RT instance flags，但不把 RT build frame 放进 scene 层
- `NormalBufferNode` 已经消费 bridge geometry atlas binding、draw geometry 与 material raster contract，并记录 indexed / non-indexed 的真实 scene mesh draw call
- renderer 在 graph build 边界把 `SceneBridgeFrame` 导入为 graph-owned frame data handle；renderPasses 通过 pass context 解析该 handle，而不是持有 scene/build 阶段的借用引用

### 3.4 RT/TLAS 可见性边界

Ray tracing 的场景输入不是 raster visibility 的派生结果。RT/TLAS 抽取必须表达整个 active RT scene，不能继承 camera、frustum、viewport、raster visibility 或 viewer override culling。

当前 primary ray 的三角形 facing cull 由 ray-tracing shader 的 back-face cull flag 和 TLAS instance flags 共同决定：单面 mesh 按导入的 mesh winding 剔除背面，`Mesh::clockwiseFrontFace=false` 会在 TLAS instance 上启用 Vulkan RT triangle-facing flip 以匹配 CCW front-face，clockwise mesh 保持 Vulkan RT 默认 facing；任一 geometry 使用 double-sided material 的 mesh 会在整 instance 上禁用 facing cull。TLAS 写入 instance 时还会按 world transform handedness 对 flip bit 做一次 XOR，以保持镜像实例的正反面一致性。精确到 geometry 的混合单面/双面策略需要 TLAS 按 geometry fan-out，是单独的架构变更。

当前 renderer 边界要求：

- raster extraction 可以使用 `SceneVisibilityMode::primaryCameraFrustum` 或 `SceneVisibilityMode::customFrustum`
- `ScenePacketDomain::rayTracingInstance` 和 `ScenePacketDomain::tlasBuildInput` 必须使用 RT 专用 `SceneExtractInput`
- RT 专用输入的 `visibility` 必须是 `SceneVisibilityMode::none`，且不能携带 `customFrustum`

未来如果需要 RT culling、streaming、TLAS partition 或 per-light/path-specific acceleration structure，必须作为显式架构变更设计；不能通过复用 raster extraction setting 隐式引入。

## 4. Flecs 在当前架构中的位置

Flecs 目前用于：

- template prefab 与 runtime instance 组织
- 层级关系存储
- 活动实例过滤
- transform / bounds 更新
- renderable 与 camera/light binding 查询

在架构上，Flecs 是 `scene` 的内部组织工具，而不是跨模块公共 ABI。

这意味着：

- 上层模块不应依赖 scene 内部 query 细节
- renderer / renderPasses 应继续消费桥接后的 runtime 契约

## 5. 与相机相关的当前现实

当前 scene 已经具备的能力：

- imported camera / light 全链路接入
- deterministic primary camera 解析
- fallback runtime camera
- viewport-aware projection contract
- primary-camera frustum visibility

当前仍明确留在 scene 外部的能力：

- viewer 侧自由相机模块（在 `nr.renderer` / `nr.app`）
- 键盘与鼠标交互驱动的运行时相机（在 `nr.app::AppCamera`）
- renderer camera override 接入（通过 `RendererCameraOverride`、`SceneVisibilityMode::customFrustum` 和 bridge frame-constants override）

因此，“自由相机 + `NormalBuffer` 交互”不应通过向 `SceneAsset` 或 `CameraAsset` 塞入输入状态来实现，而应保持为 scene 之外的运行时层能力。

## 6. 当前明确不在本模块内解决的问题

以下内容不应继续塞进 `nr.scene`：

- GLFW 键鼠输入轮询
- QWEASD 移动控制
- 按住左键旋转视角的控制器状态机
- render pass 直接向 scene 请求内部 mesh record 的捷径接口

这些能力当前通过 `nr.app::AppCamera`、`nr.renderer::RendererCameraOverride`、scene 的 custom-frustum 入口和 bridge frame-constants override 接入，而不是把 app 输入逻辑下沉到 `scene`。

## 7. 当前代码入口

- 主实现：[../src/scene/nrScene.ixx](../src/scene/nrScene.ixx)
- 类型与 runtime 契约：[../src/scene/nrSceneType.ixx](../src/scene/nrSceneType.ixx)
- bridge 规划与 frame bridge：[../src/scene/nrSceneBridge.ixx](../src/scene/nrSceneBridge.ixx)
- scene 工具函数：[../src/scene/nrSceneUtils.ixx](../src/scene/nrSceneUtils.ixx)

## 8. 当前建议优先参考的测试

如果要验证 scene 当前真实边界，优先看这些测试：

- `test/integration/scene/nr_scene_bridge_plan_contract_test.cpp`
- `test/integration/scene/nr_scene_runtime_extraction_contract_test.cpp`
- `test/integration/scene/nr_scene_render_bridge_contract_test.cpp`
- `test/integration/renderer/nr_renderer_camera_override_contract_test.cpp`
- `test/smoke/app/normalBufferUiSmoke.cpp`

这些测试覆盖了：

- bridge plan 与 canonical key
- runtime extraction 与 scene 抽取主链路
- `SceneBridgeFrame` 的 frame constants override 与 draw geometry 解析契约
- renderer camera override 下的 scene -> renderer 桥接契约
- `NormalBuffer + Ui -> Present` 主运行路径的端到端 smoke 覆盖
