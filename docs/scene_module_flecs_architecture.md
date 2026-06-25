# Scene 模块架构说明（`nr.scene` + Flecs）

这份文档只保留 `nr.scene` 当前已经成立的架构边界、主数据流和稳定契约，不再记录旧阶段历史、性能对比实验或实施流水账。

## 1. 模块职责

`nr.scene` 是当前工程的运行时世界层与桥接层，位于 `load`、`resource`、`rhi` 与 `renderer` 之间。

它负责：

- 把 `nr::load::SceneAsset` 转成项目内部可稳定引用的 canonical key 与 registry 记录
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
- material grouping
- frame 常量（view / projection / viewProjection / cameraWorld / drawCount）
- 每个 draw 的 `SceneBridgeDrawGeometry`，通过 `SceneRenderBridgeBuildInput::resolveRasterDrawGeometry` 解析成 render-pass 可直接消费的 vertex/index buffer binding、draw count、offset 与 front-face state

这意味着：

- renderer / renderPasses 不需要读取 scene 内部 registry 或 Flecs query 来绘制 mesh
- `NormalBufferNode` 已经消费 bridge geometry contract，并记录 indexed / non-indexed 的真实 scene mesh draw call
- renderer 在 graph build 边界把 `SceneBridgeFrame` 导入为 graph-owned frame data handle；renderPasses 通过 pass context 解析该 handle，而不是持有 scene/build 阶段的借用引用

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
