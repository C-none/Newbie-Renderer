# NRRHI 定向优化分析（Vulkan-only / C++23 / RTX 5070 Ti / Slang Cursor-only）

> 本文档是当前项目的“单目标实施规范”，用于约束后续 nrrhi 演进方向。

---

## 1. 项目约束（最终版本）

1. 图形 API：仅支持 Vulkan。
2. 语言与风格：现代 C++（以 C++23 为基准），坚持 RAII + move-only + 零裸句柄长期持有。
3. 目标设备：单一验证设备为 NVIDIA GeForce RTX 5070 Ti。
4. Slang 使用策略：仅采用 Cursor 风格反射与绑定路径，不引入非 Cursor 的高层绑定方案。

说明：
- 由于是单设备目标，本阶段不以“跨厂商兼容”作为优先级目标。
- 由于是 Vulkan-only，不需要保留多后端抽象复杂度。
- Slang 只围绕 Cursor 路线深化，不扩展 dynamic dispatch/type conformance/specialization 体系作为当前目标。

---

## 2. 当前代码状态（结论）

`src/rhi` 已具备较好的中层骨架：
- `Device/Queue/FrameContext/CommandPool` 基本齐全。
- `VMA + Buffer/Image + ResourcePool` RAII 方向正确。
- `ShaderService + ReflectedPipelineLayout + DescriptorWriter` 已有“反射驱动管线”的基础。

但要达成“完整可持续运行”的 Vulkan RAII 包装，还缺 3 个闭环：

1. Swapchain 生命周期闭环：acquire/present/recreate/resize。
2. Frame 生命周期闭环：beginFrame/submitFrame/endFrame。
3. 资源状态闭环：barrier/layout transition/queue ownership transfer。

---

## 3. 在当前约束下的重点缺口

## 3.1 必补（P0）

### A) Swapchain 生命周期 API
缺失内容：
- acquireNextImage 封装。
- present 封装。
- `VK_ERROR_OUT_OF_DATE_KHR`、`VK_SUBOPTIMAL_KHR` 自动恢复。
- 窗口尺寸变更后的 swapchain 重建流程（含 imageViews 重建）。

### B) 帧驱动统一 API
缺失内容：
- `beginFrame()`：等待并重置 fence、acquire image、处理重建请求。
- `submitFrame()`：将当前帧 CommandBatch 与同步对象提交到 graphics queue。
- `endFrame()`：present + 帧索引推进 + 必要的重建标记。

### C) Barrier/Transition 最小系统
缺失内容：
- image layout transition helper。
- buffer/image memory barrier helper。
- queue family ownership transfer helper（graphics/compute/transfer）。

## 3.2 应补（P1）

### A) 5070 Ti 定向能力配置
在单设备前提下，不做“复杂 capability matrix”，但需要：
- 启动时打印并验证关键扩展与特性是否满足当前路径。
- 将“必须项”与“可选项”分组，避免全部硬崩。

### B) 上传/下载实用工具
- staging upload（CPU->GPU）模板路径。
- readback（GPU->CPU）模板路径。
- 与 barrier helper 组合形成可复用最小工作流。

### C) Pipeline cache 持久化
- 启动加载 cache，退出落盘 cache。
- 仅 Vulkan 路径，无需跨后端抽象。

---

## 6. 建议的实现顺序（严格执行）

### 阶段 1（必须完成）
1. 增加 Swapchain acquire/present/recreate API。
2. 增加 Device 级 beginFrame/submitFrame/endFrame。
3. 跑通一条稳定渲染循环（含 resize/out-of-date）。

### 阶段 2（必须完成）
1. 增加 barrier helper（image/buffer/ownership）。
2. 增加 staging upload 与 readback 工具。
3. 将示例路径迁移为“工具化调用”而非散落命令。

### 阶段 3（建议完成）
1. 强化 Slang Cursor 风格绑定 API（路径解析、类型校验、错误信息）。
2. 完成 pipeline cache 持久化。
3. 补齐最小运行时诊断输出（编译、绑定、同步）。

---

## 7. 验收标准（面向当前约束）

## M1（运行闭环）
- 可持续循环：acquire -> record -> submit -> present。
- resize/out-of-date 自动恢复。

## M2（资源正确性）
- 关键路径 barrier 正确，验证层无关键同步错误。
- 上传/下载路径稳定可复用。

## M3（Slang Cursor 完整性）
- 资源可通过 Cursor 风格路径稳定绑定。
- shader 变更后路径级错误信息可读、可定位。

---

## 8. 可直接复用的实现提示词（本项目定向）

```text
你正在维护 Newbie-Renderer 的 src/rhi。请严格按以下约束实现：
1) 仅支持 Vulkan + C++23；
2) 目标设备是 NVIDIA GeForce RTX 5070 Ti；

请先完成 P0：
- Swapchain acquire/present/recreate/resize/out-of-date 处理；
- Device beginFrame/submitFrame/endFrame；
- 最小 barrier helper（image/buffer/ownership transfer）。

要求：
- 保持 RAII + move-only 语义；
- 保持模块化风格，最小侵入修改；
- 给出可运行主循环示例并通过当前构建。
```

---

## 9. 一句话结论

VK_KHR_dynamic_rendering
VK_EXT_extended_dynamic_state
VK_KHR_pipeline_library
VK_KHR_deferred_host_operations
VK_KHR_synchronization2
VK_KHR_timeline_semaphore
VK_EXT_descriptor_indexing
VK_KHR_buffer_device_address
VK_KHR_acceleration_structure
VK_KHR_ray_tracing_pipeline
VK_KHR_ray_query
VK_EXT_mesh_shader
VK_KHR_fragment_shading_rate
VK_EXT_robustness2
VK_EXT_pipeline_robustness
VK_KHR_swapchain
vk::NVRayTracingInvocationReorderExtensionName