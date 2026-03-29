# `NormalView` 与相机子模块三阶段执行计划

这份文档不是“想法清单”，而是给三个独立 Session 使用的可执行 todo。每个 phase 都包含：

- 当前上下文
- 明确的实施边界
- 需要落地的任务
- 严格的验收标准
- 交接到下一阶段时必须成立的状态

## 0. 实现进度（2026-03-26）

- [x] Phase A 完成
   - `NormalView` 已改为真实 scene geometry draw path，移除固定 `draw(3, 1, 0, 0)` 占位路径。
   - `SceneBridgeFrame` 已承载 draw-ready geometry contract（VB/IB + indexed/non-indexed 参数 + front face）。
   - `shader/renderer/normalView.slang` 已改为基于真实法线输出。
   - 自动化覆盖：`nr_renderer_stage4_scene_bridge_contract_test`、`nr_renderer_stage6_builtin_nodes_contract_test`、`rasterNormalViewer`。

- [x] Phase B 完成
   - viewer/app 侧透视相机子模块已接入：`src/renderer/nrViewerCamera.ixx`。
   - renderer 已支持 camera override 输入边界，并在 scene extraction 侧应用 custom frustum + frame constants override。
   - `rasterNormalViewer` 默认 smoke 路径首帧使用 viewer camera override，`Triangle` 与 `Box` 均可见。
   - 自动化覆盖：`nr_renderer_camera_override_contract_test`、`rasterNormalViewer`。

- [x] Phase C 完成
   - 已支持交互控制：`W/S/A/D/Q/E` 位移，按住左键拖拽旋转，且更新由 `deltaSeconds` 驱动。
   - 已加 pitch clamp，避免视角翻转。
   - `rasterNormalViewer` 新增显式 `--interactive` 入口，默认 smoke 路径保持不变。
   - 交互模式已处理每帧 poll events、camera override 提交、viewport resize 同步。
   - 自动化覆盖：`nr_renderer_viewer_camera_controller_contract_test`、`rasterNormalViewerInteractive`、`rasterNormalViewer`。

## 1. 当前代码现实

本节保留的是三阶段启动时的基线判断，用于回顾实施前状态；当前完成状态以“实现进度”小节为准。

在开始三个 phase 之前，需要先统一对当时状态的判断：

1. `NormalViewNode` 已经能基于 `SceneBridgeFrame.rasterDraws` 做 draw planning，但 record 阶段仍是占位实现。
2. 当前 shader `shader/renderer/normalView.slang` 不是“真实模型法线显示”，而是基于 push constants 生成的占位画面。
3. `src/renderPasses/NormalView/nrNormalViewNode.ixx` 当前仍使用固定的 `draw(3, 1, 0, 0)`。
4. `scene` 已经有 mesh GPU upload 与 residency，但 `SceneBridgeFrame` 还没有扩成 render-pass 可直接消费的完整 draw-ready geometry contract。
5. `scene` 当前只负责 imported / fallback primary camera；它没有 viewer 自由相机控制器。
6. `renderer.renderFrame(...)` 当前只会从 `scene` 解析主相机，不支持 app/viewer 侧 camera override。
7. `test/app/rasterNormalViewer.cpp` 当前是单帧 smoke executable，会被 `ctest` 调用，不是长期交互循环。

## 2. 最终方案约束

后续三个 phase 都必须遵守下面这组最终方案，不要在中途偏航：

1. `NormalView` 必须改成真实 scene geometry 的 normal-view raster pass，不能继续依赖占位三角形 shader。
2. render pass 不应直接回钻 `Scene` 内部 registry；scene -> renderer / renderPasses 的稳定边界应继续收敛在 bridge contract 上。
3. 新增的自由相机是 viewer/app 侧运行时能力，不是 `SceneAsset` 或 `nr.resource::CameraAsset` 的扩展。
4. 当前 viewer 只支持透视投影；不要在这轮计划里顺手加正交自由相机。
5. `rasterNormalViewer` 必须保留默认 smoke 路径，避免把 `ctest` 变成会卡住的交互程序。
6. 交互模式必须通过显式入口开启，例如命令行参数或单独模式；不能覆盖默认测试行为。

## 3. 建议的边界落点

为了减少返工，建议按下面的边界落地：

### 3.1 `NormalView` 的真实绘制边界

`SceneBridgeFrame` 需要补齐 render-pass 可直接消费的 draw geometry 信息，而不是让 `NormalViewNode` 重新向 `Scene` 查询 mesh record。

建议目标是为每个 raster draw 提供至少这些信息：

- 顶点缓冲引用
- 可选索引缓冲引用
- `firstIndex`
- `indexCount`
- `vertexOffset`
- front-face / raster state 所需的最小几何元信息

### 3.2 相机模块落点

相机控制器放在 viewer/app 侧，不放进 `scene`。

建议拆成两层：

1. 一个小的 viewer camera 子模块，负责：
   - 透视投影参数
   - 相机 pose
   - 由 pose 生成 `world/view/projection`
   - phase C 再接入键鼠输入
2. 一个 renderer 侧 camera override 输入契约，负责：
   - 用 app camera 替代 scene primary camera
   - 为 scene extraction 提供 custom frustum
   - 为 `SceneBridgeFrame` 提供 frame constants

### 3.3 `rasterNormalViewer` 的运行模式

建议保留两条路径：

- 默认路径：单帧 smoke，继续用于 `ctest`
- 显式 interactive 路径：窗口循环 + camera controller + 键鼠交互

## 4. Phase A

## A. 修改 `NormalView`，使其能正确渲染任意加载模型的法线视图

### A.1 当前上下文

这一阶段开始时，默认应假设：

1. `scene` 已能给出 `ScenePacketSet` / `SceneBridgeFrame`。
2. `scene` 的 mesh 已能上传到 GPU。
3. `NormalView` 还没有真实消费 mesh GPU payload。
4. `rasterNormalViewer` 仍然只需要走 smoke 路径，不要求交互。

### A.2 本阶段必须完成的任务

1. 重新定义 `scene -> renderer / renderPasses` 的 raster draw bridge，使其包含真实 draw 所需的 geometry contract。
2. 在 `SceneRenderBridge` 里构建这个 geometry contract，避免 render pass 直接读 scene 内部 record。
3. 改写 `NormalViewNode` 的 pass record 路径，使每个 `SceneBridgeDrawPacket` 都发出真实 draw call。
4. 改写 `shader/renderer/normalView.slang`，让输出来自真实模型法线，而不是占位生成逻辑。
5. 保持 `NormalView.output.plannedDrawCount` 与 scene bridge draw 数一致。
6. 让 `Triangle.gltf` 继续可见，同时让至少一个非三角形样例模型也可见。

建议优先使用：

- `assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf`
- `assets/glTF-Sample-Assets/Models/Box/glTF/Box.gltf`

### A.3 明确不做的事情

1. 不在本阶段引入自由相机。
2. 不在本阶段改动输入系统。
3. 不把 render pass 改成直接依赖 `Scene` registry。

### A.4 严格验收标准

以下条件全部满足，Phase A 才算完成：

1. `src/renderPasses/NormalView/nrNormalViewNode.ixx` 不再保留固定 `draw(3, 1, 0, 0)` 的占位绘制路径。
2. `shader/renderer/normalView.slang` 不再输出基于 draw index / bindless id 拼出来的占位彩条，而是基于真实几何法线输出。
3. `rasterNormalViewer` 的 smoke 路径加载 `Triangle.gltf` 时，渲染成功且能看到真实三角形法线视图。
4. `rasterNormalViewer` 的 smoke 路径切到 `Box.gltf` 时，渲染成功且画面不是单个占位三角形。
5. `NormalView.output.plannedDrawCount` 与 `SceneBridgeFrame.rasterDraws.size()` 保持一致。
6. 新的 draw geometry 信息来自 bridge contract，而不是 render pass 内部回查 `Scene`。
7. 至少补一项自动化覆盖，验证“真实 geometry draw path 已接通”。优先选项：
   - 扩展现有 `nr_renderer_stage6_builtin_nodes_contract_test`
   - 或新增一个针对 `NormalView` 的 scene-driven integration test

### A.5 交接到 Phase B 时必须成立的状态

1. `NormalView` 已经是“真实模型法线视图 pass”，而不是占位演示。
2. `rasterNormalViewer` 仍然保持默认 smoke 行为。
3. 默认相机仍可先沿用 scene 当前主相机 / fallback 机制。

## 5. Phase B

## B. 将相机模块集成进项目，先不考虑交互，仅在默认视角下显示正确即可

### B.1 当前上下文

这一阶段开始时，默认应假设：

1. Phase A 已完成，`NormalView` 已能真实渲染模型。
2. renderer 仍然只能从 scene 解析相机。
3. `rasterNormalViewer` 仍然是默认单帧 smoke。

### B.2 本阶段必须完成的任务

1. 新增一个 viewer/app 侧相机子模块，只支持透视投影。
2. 该子模块至少要有：
   - 相机位置与朝向
   - 透视投影参数
   - 根据 viewport 生成 `world/view/projection`
3. 给 renderer 增加一个可选的 camera override 输入边界，用于在 scene path 下覆盖默认主相机。
4. 当 camera override 存在时：
   - scene extraction 走 `customFrustum`
   - `SceneBridgeFrame` 使用 override camera 的 frame constants
   - 不依赖 imported camera 或 fallback camera 的最终矩阵结果
5. 更新 `rasterNormalViewer`，让默认 smoke 路径使用新的默认相机视角，并保证一打开就能看到目标模型。

建议默认相机要求：

- `Triangle.gltf` 可见
- `Box.gltf` 可见
- viewport resize 后投影仍正确

### B.3 明确不做的事情

1. 不加入任何键盘或鼠标交互。
2. 不把自由相机状态写回 scene camera registry。
3. 不取消 `rasterNormalViewer` 的 smoke 默认行为。

### B.4 严格验收标准

以下条件全部满足，Phase B 才算完成：

1. 新相机子模块已经存在，且职责仅限于 viewer/runtime 相机状态与透视矩阵生成。
2. renderer 存在明确的 camera override 输入边界；没有 override 时，旧的 scene primary-camera 路径仍可工作。
3. `rasterNormalViewer` 默认运行时使用新相机默认视角，`Triangle.gltf` 首帧可见。
4. `rasterNormalViewer` 切到 `Box.gltf` 时，首帧同样可见，不需要依赖导入相机。
5. viewport 改变后，相机投影与 frustum 仍和新的 viewport 保持一致。
6. 默认调用 `rasterNormalViewer` 仍会在有限时间内退出，能够继续作为 `ctest` smoke。
7. 至少补一项自动化覆盖，验证“camera override 生效且不破坏旧 scene camera 路径”。优先选项：
   - 扩展 renderer scene-bridge contract test
   - 或新增一个 renderer camera-override contract test

### B.5 交接到 Phase C 时必须成立的状态

1. viewer/app 侧相机模块已经能稳定给出默认视角。
2. renderer 已经能吃 app 侧 camera override。
3. `rasterNormalViewer` 仍保留 smoke 路径，没有被交互循环替换掉。

## 6. Phase C

## C. 添加交互功能

### C.1 当前上下文

这一阶段开始时，默认应假设：

1. Phase A 和 Phase B 都已完成。
2. `NormalView` 已经是真实模型绘制。
3. viewer 相机已经能生成默认视角，但还没有输入控制。
4. `rasterNormalViewer` 仍默认走 smoke 模式。

### C.2 本阶段必须完成的任务

1. 为 viewer 相机子模块补齐交互控制：
   - `W/S`：前后移动
   - `A/D`：左右平移
   - `Q/E`：上下移动
   - 按住左键并拖动：旋转视角
2. 交互更新必须基于 `deltaSeconds`，不能写成依赖帧率的固定步长。
3. pitch 需要做合理夹角限制，避免翻转。
4. `rasterNormalViewer` 增加显式 interactive 入口，例如：
   - `--interactive`
   - 或等价的显式模式开关
5. interactive 模式中需要：
   - 持续 `pollEvents()`
   - 每帧更新相机
   - 每帧把 override camera 送入 renderer
   - resize 后更新投影与 frustum

如果需要从窗口系统读取输入，优先增加小而明确的输入查询边界；不要把 GLFW 细节散落到多个核心模块里。

### C.3 明确不做的事情

1. 不在本阶段增加正交自由相机。
2. 不在本阶段做编辑器式 gizmo、轨道相机或多相机切换。
3. 不让默认 smoke 路径进入无限循环。

### C.4 严格验收标准

以下条件全部满足，Phase C 才算完成：

1. 默认运行 `rasterNormalViewer` 时，仍然保持 smoke 行为并能通过自动测试。
2. 显式 interactive 模式下，窗口循环能持续运行直到用户关闭窗口。
3. `W/S/A/D/Q/E` 的移动方向与相机局部坐标一致，且运动量受 `deltaSeconds` 驱动。
4. 只有在按住左键时，鼠标移动才会改变 yaw/pitch；松开左键后停止旋转。
5. resize 后，相机投影与 scene extraction frustum 继续正确匹配新 viewport。
6. 交互过程中模型仍保持可见，法线视图持续正确更新。
7. 至少补一项自动化覆盖，验证相机控制器的纯逻辑部分。优先选项：
   - 相机更新数学单测
   - yaw/pitch clamp 与移动方向单测

### C.5 手动验收清单

Phase C 除自动化外，还必须通过下面的手动清单：

1. 启动 interactive 模式后，默认视角能直接看到模型。
2. 不按左键移动鼠标时，视角不旋转。
3. 按住左键左右拖动时，yaw 变化符合预期。
4. 按住左键上下拖动时，pitch 变化符合预期，且不会翻转。
5. `W/S/A/D/Q/E` 六个方向都能稳定工作。
6. 交互后窗口 resize，画面不崩溃，视角与投影继续正常。

## 7. 三个 Session 的最小交接信息

为避免每个 Session 重新做一遍背景调查，建议每次开始时先确认下面这些事实是否仍成立：

1. `NormalViewNode` 的 draw path 是否已经摆脱占位实现。
2. `SceneBridgeFrame` 是否已经承载 draw-ready geometry contract。
3. renderer 是否已经具备 camera override 输入。
4. `rasterNormalViewer` 是否仍保留默认 smoke 路径。
5. 当前正在处理的是哪个 phase，不要跨 phase 预支功能。
