# RHI 综合对比审查与详细改进建议（agent1 + agent2）

> 同步状态（2026-03-10）：已对齐 `docs/rhi_stage2_temp_execution_playbook.md` 到 Session C。
> 
> 标记约定：
> - `[已更新]`：原有结论已按当前代码改写。
> - `[新增]`：总文档新增的结构说明或补充解释。
> - `[已完成]`：对应改造项已在 Stage-2 A/B/C 落地。

## 1. 文档目标

本文用于替代 `docs/agent1` 与 `docs/agent2` 下的原始调研结果，作为当前 `src/rhi` 架构的统一详细评审结论。

本文不是简单摘录原始报告，而是在对照当前代码后，对每一项问题给出统一判断：

- 该结论当前是否仍然成立
- 该问题是否必须优先处理
- 当前代码中的现状和证据
- 推荐的改进方向
- 为什么应按该方向改进

审查目标对象为 `src/rhi`，其定位是：包装 Vulkan、Slang、GLFW、VMA，对外提供现代、模块化、可持续演进的图形 API。

---

## 2. 审查方法与判定标准

### 2.1 信息来源

本文综合以下来源：

- `docs/agent1/rhi_architecture_review.md`
- `docs/agent1/resource_and_command_system_analysis.md`
- `docs/agent2/rhi_architecture_review.md`
- `docs/agent2/rhi_resource_and_submission_review.md`
- 当前 `src/rhi/*.ixx` 真实实现
- `AGENTS.md` 中项目约束

新增项目设计路线约束（本次修订新增）：

- 项目不支持多窗口。
- 项目不支持 headless 模式。
- 项目不支持 offscreen-only 渲染模式。
- present 默认并固定由 compute queue 承担。

### 2.2 判定维度

每项结论都按两个维度判断：

- 合理性：`成立` / `部分成立` / `不成立（已过时）`
- 必要性：`必须` / `建议` / `可暂缓`

### 2.3 “必须 / 建议 / 可暂缓”的含义

`必须`：如果不处理，会直接阻碍 RHI 成为稳定、可持续演进的基础层，或者存在明确的行为风险、架构闭环缺失、可移植性问题。

`建议`：问题真实存在，但不会立即阻断当前系统运行。更偏向中期架构质量、API 收敛、后续扩展性改善。

`可暂缓`：方向本身合理，但更偏性能、生态、上层能力增强，不应先于主流程与基础正确性问题处理。

---

## 3. 当前系统关键事实

以下事实来自对当前代码的直接核对，是后续判定的依据。

1. `Device` 采用两阶段初始化，在 `initialize()` 中串联内存、资源、命令系统初始化。
	证据：`src/rhi/nrDevice.ixx:58`, `src/rhi/nrDevice.ixx:78`, `src/rhi/nrDevice.ixx:79`

2. present 路由当前已显式收口到 `presentQueueFamilyIndex()`，并按策略默认使用 `compute` queue 作为 present carrier。
  证据：`src/rhi/nrDevice.ixx:583`, `src/rhi/nrDevice.ixx:369`

3. 运行时队列角色只有 `Graphics/Compute/Transfer`，没有 `Present`。
	证据：`src/rhi/nrType.ixx:52`

4. 队列提交通道已统一切换到 sync2：`CommandBatch` 构建 `SubmitInfo2Packet`，`GpuQueue::submit` 使用 `queue.submit2`。
  证据：`src/rhi/nrCommandBatch.ixx:36`, `src/rhi/nrCommandBatch.ixx:171`, `src/rhi/nrQueue.ixx:72`, `src/rhi/nrQueue.ixx:106`

5. `FrameContext` secondary pool 已改为 frame-begin 预建固定槽位（`maxThreads`），录制期不再动态扩容。
  证据：`src/rhi/nrFrameContext.ixx:80`, `src/rhi/nrFrameContext.ixx:199`, `src/rhi/nrFrameContext.ixx:375`, `src/rhi/nrDevice.ixx:109`

6. `SwapChain` 已具备 acquire/present 运行时 API，并支持通过子模块接口执行 create/recreate。
  证据：`src/rhi/nrSwapchain.ixx:47`, `src/rhi/nrSwapchain.ixx:61`, `src/rhi/nrSwapchain.ixx:42`

7. [已更新][已完成] 资源边界已完成物理拆分：`ResourceFactory` 负责 persistent caller-owned `Buffer/Image` 创建，`ResourcePool` 仅保留 frame-local arena 语义。
  证据：`src/rhi/nrResourcePool.ixx:39`, `src/rhi/nrResourcePool.ixx:60`, `src/rhi/nrResourcePool.ixx:66`, `src/rhi/nrResourcePool.ixx:89`, `src/rhi/nrDevice.ixx:53`, `src/rhi/nrDevice.ixx:54`, `src/rhi/nrDevice.ixx:88`, `src/rhi/nrDevice.ixx:89`

8. [已更新][已完成] `UploadReadbackContext` 已迁移到 `ResourceFactory` 路径，不再依赖 `ResourcePool::acquire*`。
  证据：`src/rhi/nrResourceOps.ixx:584`, `src/rhi/nrResourceOps.ixx:598`, `src/rhi/nrResourceOps.ixx:604`

9. `Buffer` 的 BDA 仍是 `std::optional<VkDeviceAddress>` 的 lazy cache。
	证据：`src/rhi/nrResource.ixx:270`, `src/rhi/nrResource.ixx:418`

10. [已更新][已完成] descriptor runtime 已从 pipeline 实现分离：`ShaderBindingSet` / `ShaderBindingPool` / `ShaderResourceWriter` 定义和更新逻辑位于 `nrDescriptor.ixx`；pipeline 仅消费该能力。
  证据：`src/rhi/nrDescriptor.ixx:197`, `src/rhi/nrDescriptor.ixx:213`, `src/rhi/nrDescriptor.ixx:233`, `src/rhi/nrDescriptor.ixx:1138`, `src/rhi/nrPipeline.ixx:534`, `src/rhi/nrPipeline.ixx:540`

11. [已更新][已完成] descriptor command binding authority 保持在 `CursorPipelineLayout`，并保留 `bindDescriptorSet(s)` / `pushConstants` 的 layout-aware 入口。
  证据：`src/rhi/nrPipeline.ixx:81`, `src/rhi/nrPipeline.ixx:140`, `src/rhi/nrPipeline.ixx:146`, `src/rhi/nrPipeline.ixx:152`, `src/rhi/nrPipeline.ixx:181`, `src/rhi/nrPipeline.ixx:194`

12. [已更新][已完成] 分配策略命名已统一：`AllocationStrategy::Transient` 已替换为 `AllocationStrategy::StagingTransient`，并同步到 allocator 配置路径。
  证据：`src/rhi/nrType.ixx:79`, `src/rhi/nrType.ixx:83`, `src/rhi/nrMemoryAllocator.ixx:119`, `src/rhi/nrMemoryAllocator.ixx:120`, `src/rhi/nrMemoryAllocator.ixx:419`

13. `VkShaderProgram` 当前仍围绕单 entrypoint 构建 stage create info，multi-stage graphics program 能力尚未补齐。
  证据：`src/rhi/nrPipeline.ixx:592`, `src/rhi/nrPipeline.ixx:613`, `src/rhi/nrPipeline.ixx:634`

14. pipeline cache 当前未接入，graphics/compute/ray tracing pipeline 创建都仍使用空 cache。
  证据：`src/rhi/nrPipeline.ixx:565`, `src/rhi/nrPipeline.ixx:603`, `src/rhi/nrPipeline.ixx:624`

15. [已更新] immutable sampler 参数已在 pipeline service API 预留，但当前仍为 seam-only（参数保留，尚未落地到 descriptor set layout binding 构造）。
  证据：`src/rhi/nrPipeline.ixx:565`, `src/rhi/nrPipeline.ixx:570`, `src/rhi/nrPipeline.ixx:603`, `src/rhi/nrPipeline.ixx:608`, `src/rhi/nrPipeline.ixx:624`, `src/rhi/nrPipeline.ixx:629`

16. Session 最新状态：`descriptorCount > 1` 的 strict capability 二次校验链路已移除，capability 由 device 扩展启用阶段统一保证。
  证据：`src/rhi/nrDevice.ixx`, `src/rhi/nrPipeline.ixx`

17. 帧事务入口已在 `Device` 暴露：`beginFrame/submitFrame/presentFrame/endFrame`，且 `submitFrame/endFrame` 默认队列参数已设为 `QueueRole::Compute`。
  证据：`src/rhi/nrDevice.ixx:102`, `src/rhi/nrDevice.ixx:133`, `src/rhi/nrDevice.ixx:164`, `src/rhi/nrDevice.ixx:187`

18. 资源到命令桥接基础层已落地：新增 `nrResourceOps` 子模块并通过 `exportModule` 对外导出，包含 `BarrierBatch` 与常用 barrier 工厂函数。
  证据：`src/rhi/nrResourceOps.ixx:194`, `src/rhi/nrResourceOps.ixx:267`, `src/rhi/nrResourceOps.ixx:66`, `src/rhi/nrResourceOps.ixx:133`, `src/rhi/exportModule.ixx:13`

19. 当前窗口与展示路径是单 surface 生命周期：`Device::initialize()` 中创建 `Surface` 并立即绑定 `SwapChain`，未出现多窗口 surface 容器或路由层。
  证据：`src/rhi/nrDevice.ixx:94`, `src/rhi/nrDevice.ixx:95`, `src/rhi/nrDevice.ixx:96`

20. present 支持校验与执行均绑定 compute 路径：`presentQueueFamilyIndex()` 固定返回 compute family，`presentFrame()` 使用 `queueManager.compute().handle()`。
  证据：`src/rhi/nrDevice.ixx:519`, `src/rhi/nrDevice.ixx:530`, `src/rhi/nrDevice.ixx:172`

21. 后续整理已将 `PipelineService` 与 `PresentationContext` 从 `nrDevice` 内联定义迁移到对应职责 partition（`nrPipeline` / `nrSwapchain`），`Device` 仅做组合与编排。
  证据：`src/rhi/nrPipeline.ixx`, `src/rhi/nrSwapchain.ixx`, `src/rhi/nrDevice.ixx`

22. [新增] Stage-2 已形成可读的内部组合骨架：`Device` 组合 `PipelineService` / `PresentationContext` / `ResourceFactory` / `ResourcePool`，其中资源层生命周期边界已显式化，descriptor 运行时边界已实体化。
  证据：`src/rhi/nrDevice.ixx:53`, `src/rhi/nrDevice.ixx:54`, `src/rhi/nrDevice.ixx:85`, `src/rhi/nrDevice.ixx:86`, `src/rhi/nrDescriptor.ixx:197`, `src/rhi/nrDescriptor.ixx:213`, `src/rhi/nrDescriptor.ixx:233`

---

## 4. 详细问题清单与改进建议

本节按综合问题编号 `J1-J25` 展开。每一项包含：

- 判定
- 现状/证据
- 改进方向
- 详细改进原因

### J1. Device 职责过载（God Object）

判定：`成立`，`必须`

现状/证据：

- `Device` 负责实例/物理设备/逻辑设备初始化。
- `Device::initialize()` 中继续初始化 `MemoryAllocator`、`ResourcePool` 和命令系统。
- `Device` 还承担了 swapchain 创建、shader 编译入口、binding pool 创建、pipeline 创建等工作。
- 证据：`src/rhi/nrDevice.ixx:58`, `src/rhi/nrDevice.ixx:78`, `src/rhi/nrDevice.ixx:79`, `src/rhi/nrDevice.ixx:266`, `src/rhi/nrDevice.ixx:305`, `src/rhi/nrDevice.ixx:366`, `src/rhi/nrDevice.ixx:401`

改进方向：

- 将 `Device` 收缩为“核心设备上下文”，只负责 Vulkan 实例、物理设备、逻辑设备、基础 capability 查询和底层对象持有。
- 将 presentation 相关逻辑拆到独立的 `PresentationContext` 或 `SwapchainContext`。
- 将 pipeline/layout/binding pool 创建逻辑拆到独立服务层，如 `PipelineFactory`、`BindingSystem`。
- 将 frame orchestration 拆到 `FrameRuntime` 或 `FrameScheduler`。

详细改进原因：

- 当前 `Device` 是组合根，但不仅是组合根，还是多个子系统的行为中心。这样会导致所有后续改造都向 `nrDevice.ixx` 聚集。
- 即便项目明确是单窗口、非 headless/offscreen，`Device` 仍同时承载设备初始化、frame 编排、present 流程、pipeline/binding 创建入口，长期仍会继续膨胀。
- God Object 最大的问题不是“大”，而是它让架构演进路径变得只有一条：继续堆逻辑。长期会使模块化名义存在、边界却逐渐失真。
- 对当前项目而言，这一项之所以是 `必须`，不是因为它立刻导致崩溃，而是因为后面几乎所有 P0/P1 改造都会被它阻塞或放大耦合成本。

### J2. 两阶段初始化偏离强 RAII

判定：`成立`，`建议`

现状/证据：

- `Device`、`MemoryAllocator`、`ResourcePool` 都支持默认构造，随后通过 `initialize()` 进入有效状态。
- 证据：`src/rhi/nrDevice.ixx:58`, `src/rhi/nrMemoryAllocator.ixx:43`, `src/rhi/nrMemoryAllocator.ixx:62`, `src/rhi/nrResourcePool.ixx:55`, `src/rhi/nrResourcePool.ixx:68`

改进方向：

- 外部可见对象尽量改为“构造完成即可使用”。
- 如果存在复杂依赖链，可用 builder/factory 函数封装构建流程，而不是将半初始化对象暴露给调用方。
- 对内部延迟构造对象，可以使用 `std::optional`、私有工厂、隐藏的 state object，而不是公开 `initialize()`。

详细改进原因：

- 当前方案工程上能工作，也符合 Vulkan 初始化依赖链较重的现实，所以它不是最高优先级阻塞项。
- 但两阶段初始化会天然引入“对象已存在但不可用”的中间态，导致 `valid()` 检查散落，接口语义变弱。
- 这与 `nrrhi` 在 `AGENTS.md` 中强调的 RAII 理念并不完全一致。
- 它被判为 `建议` 而不是 `必须`，因为先解决主流程闭环和并发正确性，比先清理构造模型更关键。

### J5. 队列模型缺少 present role，扩展性不足

判定：`不成立（在当前项目约束下）`，`可暂缓`

现状/证据：

- `QueueRole` 当前只有 `Graphics/Compute/Transfer`。
- present 家族选择固定收口于 `presentQueueFamilyIndex()`，并硬编码为 compute family。
- `presentFrame()` 直接使用 compute queue 执行 present。
- 证据：`src/rhi/nrType.ixx:52`, `src/rhi/nrDevice.ixx:519`, `src/rhi/nrDevice.ixx:172`

改进方向：

- 不建议为当前阶段引入独立 `Present` 运行时角色。
- 保持三角色模型（`Graphics/Compute/Transfer`）与“compute 负责 present”的固定策略。
- 将该策略显式文档化并强化断言：
  - 初始化阶段继续校验 compute family 的 surface present support。
  - 运行时禁止将 present 路由到非 compute queue 的可变策略入口。

详细改进原因：

- 在“单窗口 + 非 headless + 非 offscreen + compute present 固定”前提下，新增 `Present` 角色不会实质降低复杂度，反而会引入额外抽象层和策略分支。
- 当前真正需要的是把既定策略从“隐含约定”提升为“明确契约”，而不是为未计划支持的模式预先设计扩展点。
- 因此该项从“建议新增抽象”调整为“保持现状并加固约束表达”。

### J6. sync2 策略与旧提交路径不一致

判定：`已完成（原判定：成立，必须）`

现状/证据：

- `VK_KHR_synchronization2` 作为必需扩展启用策略保持不变。
- `CommandBatch` 已提供 `buildSubmitInfo2()`，使用 `vk::SemaphoreSubmitInfo` / `vk::CommandBufferSubmitInfo` / `vk::SubmitInfo2`。
- `GpuQueue::submit` 已统一调用 `queue.submit2(...)`。
- 证据：`src/rhi/nrCommandBatch.ixx:36`, `src/rhi/nrCommandBatch.ixx:171`, `src/rhi/nrQueue.ixx:68`, `src/rhi/nrQueue.ixx:76`, `src/rhi/nrQueue.ixx:106`

改进方向：

- 已完成：提交通路已统一到 `SubmitInfo2`。
- 后续仅需在新增提交辅助 API 时保持 sync2-only 约束，不再引回旧 `vk::SubmitInfo` 路径。

详细改进原因：

- 当前不是“没启用 sync2”，而是“策略已经站在 sync2 一侧，提交路径却没跟上”。
- 这会让同步模型的推荐范式不明确，导致未来 timeline semaphore、stage2 flags、细粒度同步都难以自然接入。
- 这是基础同步层的一致性问题，应作为 P0 处理。

### J7. FrameContext secondary 动态扩容存在并发风险

判定：`已完成（原判定：成立，必须）`

现状/证据：

- secondary pools 已改为固定槽位 `std::array<std::optional<CommandPool>, kMaxSecondaryWorkers>`。
- `prepareSecondaryPools()` 在 frame begin 预建全部 worker 槽位，录制期按索引直接复用，不再动态扩容。
- worker 数量固定使用 `maxThreads`，已删除运行时可配 worker count 路径。
- 证据：`src/rhi/nrFrameContext.ixx:80`, `src/rhi/nrFrameContext.ixx:199`, `src/rhi/nrFrameContext.ixx:350`, `src/rhi/nrDevice.ixx:109`

改进方向：

- 已完成：采用“frame-begin 预建 + 固定槽位复用”方案，消除动态扩容并发风险。
- 后续可选：若引入 job system，再评估将 slot 分配与调度器绑定。

详细改进原因：

- 该问题的严重性在于，它不是简单“可能有点慢”，而是 C++ 内存模型层面的并发正确性风险。
- 即便在 x86 上通常不会出问题，也不应该把“当前平台大概率工作”当作设计正确性依据。
- RHI 作为底层基础层，不应在核心资源分配路径中保留未定义行为可能性。

### J8. 命令录制抽象重复

判定：`已完成（原判定：成立，建议）`

现状/证据：

- [已更新][已完成] 当前收敛为 `CommandRecorder` + `ScopedCommandBuffer` 两层；`cmd::*` convenience wrappers 已删除。
- 证据：`src/rhi/nrCommand.ixx:24`, `src/rhi/nrCommand.ixx:83`

改进方向：

- 已完成：推荐路径为 `ScopedCommandBuffer`，低层保留 begin/end；不再保留命令层重复 helper。
- 后续仅需在新增命令 API 时维持该收敛边界，不回引 `cmd::*` 风格包装层。

详细改进原因：

- 这项问题不在于“代码写法不好看”，而在于 API 使用者需要不断判断自己该走哪条路径。
- RHI 的价值之一是减少调用歧义；如果同一类操作有三四种近似但不同的入口，API 可学习性会快速下降。
- 但这仍属于结构整理问题，优先级低于正确性和闭环问题。

### J9. CommandRecorder::reset 与 pool flags 语义不一致

判定：`已完成（原判定：成立，建议）`

现状/证据：

- [已更新][已完成] `CommandRecorder::reset()` 已移除，回收策略统一为 frame-scope，由 `FrameContext::resetPools()` 在帧边界执行。
- 证据：`src/rhi/nrFrameContext.ixx:169`, `src/rhi/nrFrameContext.ixx:172`, `src/rhi/nrFrameContext.ixx:181`

改进方向：

- 已完成：保留 frame-scope reset 策略，不再提供 per-buffer reset 便捷语义。
- 后续重点转为记录规则与并发策略文档化，而非 API 再扩展。

详细改进原因：

- API 最忌讳“看起来支持，实际上取决于隐藏前提”。
- 在当前项目约束下（单窗口、固定帧事务、compute present 固定），frame-scope reset 与系统主路径完全一致，且认知成本最低。
- 相比保留 helper 再做策略参数化，`A` 方案更直接地消除误导性能力，避免未来接口层继续累积条件分支。

### J10. ResourcePool 与 MemoryAllocator 职责重叠

判定：`已完成（原判定：成立，建议）`

现状/证据：

- [已更新][已完成] `ResourcePool` 的 persistent 工厂接口已去除，并引入独立 `ResourceFactory` 承载 caller-owned persistent 资源创建。
- `MemoryAllocator` 保持底层策略与分配职责，`ResourcePool` 保持 frame-local arena 角色。
- 证据：`src/rhi/nrResourcePool.ixx:39`, `src/rhi/nrResourcePool.ixx:60`, `src/rhi/nrResourcePool.ixx:66`, `src/rhi/nrResourcePool.ixx:89`, `src/rhi/nrDevice.ixx:53`, `src/rhi/nrDevice.ixx:54`

改进方向：

- [新增] 新结构说明：
- `MemoryAllocator`：底层分配策略与 VMA 持有。
- `ResourceFactory`：persistent caller-owned `Buffer/Image` 创建。
- `ResourcePool`：frame-local 临时容器与 `resetFrame()` 语义。
- 该三层边界已落地，可作为后续 `FrameResourceArena` 命名收束的直接基础。

详细改进原因：

- 当前“分配策略”和“资源生命周期容器”是拆成了两个对象，但边界没有收硬，导致上层看不清应该通过谁获得什么。
- 三类对象拆清后，可以把“怎么分配”和“活多久”两类问题分离，避免 API 既像工厂又像临时池的双重语义。
- 这不是严重 bug，但会持续增加使用和维护的认知成本。

### J11. Transient / PerFrame 语义命名混杂

判定：`已完成（原判定：成立，建议）`

现状/证据：

- [已更新][已完成] 公共策略命名已切换为 `PerFrame` + `StagingTransient`，并在 allocator 的配置函数和注释中同步。
- 证据：`src/rhi/nrType.ixx:82`, `src/rhi/nrType.ixx:83`, `src/rhi/nrMemoryAllocator.ixx:119`, `src/rhi/nrMemoryAllocator.ixx:419`

改进方向：

- 将“上传临时资源”和“每帧 scratch 资源”从命名上彻底拆开。
- 例如：`PerFrame` / `UploadTransient`，或者 `FrameLocal` / `StagingTransient`。

详细改进原因：

- 命名就是接口语义的一部分。
- 当同一个词 `Transient` 同时指代两种不同生命周期，后续无论写文档还是写高层调度器都会不断制造歧义。

### J12. ResourcePool 的 `acquire*` 价值有限

判定：`已完成（原判定：成立，可暂缓）`

现状/证据：

- [已更新][已完成] `ResourcePool::acquireBuffer/acquireImage` 已移除，等价 persistent 能力由 `ResourceFactory::createBuffer/createImage` 负责。
- 证据：`src/rhi/nrResourcePool.ixx:60`, `src/rhi/nrResourcePool.ixx:66`, `docs/rhi_stage2_temp_execution_playbook.md`

改进方向：

- 如果继续保留，应在文档里明确它们不是 pool reuse，而只是 convenience factory。
- 若后续重构资源体系，可将其移出 `ResourcePool`，由独立工厂承担。

详细改进原因：

- 该问题不影响正确性，也不会阻碍短期功能闭环。
- 它更像 API 清洁度问题，因此适合放到第二阶段之后。

### J13. Buffer/Image 元数据与缓存偏重

判定：`部分成立`，`可暂缓`

现状/证据：

- `Buffer` 和 `Image` 缓存了较多元数据。
- `Buffer` 的 BDA使用 `std::optional<VkDeviceAddress>` 做 lazy cache。
- 证据：`src/rhi/nrResource.ixx:270`, `src/rhi/nrResource.ixx:418`, `src/rhi/nrResource.ixx:450` 附近实现

改进方向：

- 保留高频查询字段，审慎压缩低频字段。
- 将 BDA cache 改为更轻量的固定字段也可考虑。
- 但所有这类优化都应建立在 profiling 基础上。

详细改进原因：

- 当前这类问题更接近“对象重量控制”和“内存布局整洁”，而不是系统性缺陷。
- 如果在主流程未闭环前优先做这些微优化，收益通常不高。

### J14. 资源到命令的执行桥接缺失（barrier/layout/upload）

判定：`部分完成（原判定：成立，必须）`

现状/证据：

- 已新增 `nrResourceOps` 作为资源到命令的桥接子模块。
- 当前已提供：`BarrierBatch`、`pipelineBarrier`、buffer/image barrier 构建函数与常见 barrier 工厂预设。
- [已更新] 当前已覆盖 upload/readback context 与 queue ownership transfer 便捷路径；轻量 resource state tracker 仍可作为后续增强项。
- 证据：`src/rhi/nrResourceOps.ixx:66`, `src/rhi/nrResourceOps.ixx:133`, `src/rhi/nrResourceOps.ixx:194`, `src/rhi/nrResourceOps.ixx:267`, `src/rhi/exportModule.ixx:13`

改进方向：

- 已完成：
  - image/buffer barrier builder 与基础 pipeline barrier 工具层。
  - upload/readback context
  - queue ownership transfer 便捷路径

详细改进原因：

- 对显式图形 API 而言，“资源被创建”不等于“资源可安全使用”。
- 真正的使用闭环是：创建 -> 上传/填充 -> 状态转换 -> 绑定/访问 -> 读回/复用。
- 当前缺失的是这条闭环中最核心的一段，所以它必须排在 P0。

### J15. Dynamic Rendering 便利封装不足

判定：`成立`，`建议`

现状/证据：

- 当前 pipeline 路径已面向 dynamic rendering，但没有形成 `RenderingInfo` builder 或 RAII rendering scope。
- 证据：graphics pipeline 创建路径中 `renderPass = nullptr`，表明系统已走 dynamic rendering 方向。见 `src/rhi/nrPipeline.ixx:374`

改进方向：

- 提供 `ScopedRendering` 或 `RenderingBuilder`。
- 让 color/depth attachment、load/store、clear 值组织更简洁。

详细改进原因：

- 这是易用性和一致性提升项。
- 在基础 barrier / submit / present 没补齐前，它不会带来决定性收益，所以属于建议项。

### J16. Pipeline cache 缺失

判定：`成立`，`建议`

现状/证据：

- graphics/compute/ray tracing pipeline 创建当前都未接入有效 cache 对象。
- 证据：`src/rhi/nrPipeline.ixx:499`, `src/rhi/nrPipeline.ixx:500`

改进方向：

- 引入 `PipelineCacheService` 或由 `Device` 持有 pipeline cache。
- 支持序列化和反序列化。

详细改进原因：

- 该项主要改善启动时和首次加载时的 pipeline 创建成本。
- 它不会改变系统正确性，但会影响中大型场景下的工程体验。

### J17. 扩展要求过于激进（RT/NV/BDA 强制）

判定：`部分成立`，`建议`

现状/证据：

- 当前设备扩展选择包含较多 RT / NV 相关扩展。
- `bufferDeviceAddress` 和 `rayTracingInvocationReorder` 被直接 assert 为必须支持。
- 证据：`src/rhi/nrDevice.ixx:126`, `src/rhi/nrDevice.ixx:203`, `src/rhi/nrDevice.ixx:204`, `src/rhi/nrDevice.ixx:205`, `src/rhi/nrDevice.ixx:206`

改进方向：

- 明确并文档化“产品能力基线”而非“通用 RHI 基线”。
- 若短期目标聚焦 RT/NV 高端能力，可保留强约束；但建议把能力选择从散落 assert 收敛为集中 profile 描述，降低后续维护成本。
- 仅在需要扩大硬件覆盖时，再引入分层 capability profile。

详细改进原因：

- 在当前项目边界收缩后（单窗口、非 headless/offscreen、固定 compute present），系统目标更偏“明确产品约束下的稳定 RHI 子集”，而非最大兼容面的通用层。
- 因此“强能力要求”不再自动等同于架构错误，但其表达方式仍应工程化收敛，避免未来演进时出现能力条件分散。

### J18. Descriptor 边界分散

判定：`已完成（原判定：成立，建议）`

现状/证据：

- [已更新][已完成] descriptor 三层边界已收敛：
- reflection/layout：`nrDescriptor.ixx`
- set allocation/update runtime：`nrDescriptor.ixx`（`ShaderBindingPool` / `ShaderResourceWriter`）
- command binding authority：`nrPipeline.ixx` 的 `CursorPipelineLayout`
- 证据：`src/rhi/nrDescriptor.ixx:197`, `src/rhi/nrDescriptor.ixx:213`, `src/rhi/nrDescriptor.ixx:233`, `src/rhi/nrDescriptor.ixx:1138`, `src/rhi/nrPipeline.ixx:81`, `src/rhi/nrPipeline.ixx:181`

改进方向：

- 已完成：pipeline implementation 已转为 descriptor runtime 的消费者，而非定义承载者。
- 后续仅需在 immutable sampler 落地时保持同一边界，不回流到 pipeline 分区混写。

详细改进原因：

- 当前设计方向本身是对的，问题主要在边界分散而不是能力错误。
- 这会影响可读性和维护性，但不妨碍短期把系统跑通。

### J20. immutable sampler 参数未落地

判定：`部分完成（原判定：成立，建议）`

现状/证据：

- [已更新] immutable sampler 入口已统一保留在 `PipelineService` 的 graphics/compute/ray tracing 创建 API，但当前仍为 seam-only，尚未消费到 set layout 构建。
- 证据：`src/rhi/nrPipeline.ixx:565`, `src/rhi/nrPipeline.ixx:570`, `src/rhi/nrPipeline.ixx:603`, `src/rhi/nrPipeline.ixx:608`, `src/rhi/nrPipeline.ixx:624`, `src/rhi/nrPipeline.ixx:629`

改进方向：

- 在 layout 创建阶段消化 immutable samplers。
- 将其并入 descriptor layout / set layout binding 构造流程。

详细改进原因：

- 该能力已经在 API 层露出，但实现尚未到位。继续保持这种状态，会使接口与实际行为脱节。
- 它不属于第一阶段必须项，但适合在 descriptor 边界整理阶段一起落地。

### J21. 单 entrypoint shader program 约束过强

判定：`成立`，`必须（若要完整 graphics pipeline）`

现状/证据：

- `VkShaderProgram::create()` 明确要求 one compiled entrypoint binary。
- 内部 `modules_` 和 `stageCreateInfos_` 只预留了 1 个 entry。
- 证据：`src/rhi/nrPipeline.ixx:729`, `src/rhi/nrPipeline.ixx:730`, `src/rhi/nrPipeline.ixx:734`, `src/rhi/nrPipeline.ixx:735`

改进方向：

- 支持 multi-stage graphics program 组装。
- 至少支持 vertex + fragment 的组合输入。
- 如果继续保留单 entrypoint 对象，则应在更上层引入 multi-stage `GraphicsShaderProgram`。

详细改进原因：

- graphics pipeline 的常规需求就是多 stage 组合。
- 如果这一层始终坚持“单 entrypoint”，上层只能通过额外绕路来补齐，会削弱 pipeline API 的完整性。
- 对“真正可用”的图形 RHI 来说，这应当被视为必须补全项。

### J23. 其它线程安全问题（ShaderService 锁粒度、descriptor update 并发模型）

判定：`部分成立`，`建议`

现状/证据：

- `ShaderService` 存在全局锁串行化编译的可能。
- `ShaderBindingPool::update` 未呈现并发隔离策略。
- 证据主要来自当前实现结构和先前调研，不属于单点明显错误，但确实存在后续扩展风险。

改进方向：

- 先明确并发模型，再决定是否需要更细粒度锁。
- 对 descriptor update，应定义“单 set 是否允许并发写入”的规则。

详细改进原因：

- 这类问题更多是“未来并发策略没有定型”，而不是当前立即有 bug 证据。
- 因此属于建议项，不应先于 `J7` 这种明确并发风险。

### J24. RenderGraph / Bindless / GPU Query 等高级能力缺失

判定：`成立`，`可暂缓`

现状/证据：

- 当前未形成 render graph。
- bindless、GPU query、多队列协作等能力尚未形成完整封装。
- 这些结论与当前模块结构相符，但都属于“高阶引擎能力”。

改进方向：

- 在 P0/P1 完成后，基于稳定的 frame/submit/resource state 基础层推进。
- 优先建设 render graph 所依赖的 barrier、resource lifetime、submission packet 基础设施。

详细改进原因：

- 这些能力都很重要，但它们建立在底层主流程已稳定的前提上。
- 在 present、frame orchestration、resource transition 都未闭环之前，引入 render graph 往往只会放大复杂度。

### J25. 生态能力缺口（ImGui 后端、热重载、mipmap、纹理系统、AS 管理等）

判定：`成立`，`可分阶段`

现状/证据：

- 当前这些能力整体上尚未形成正式模块化支持。
- 原始 agent 调研在这方面的结论总体仍成立。

改进方向：

- 按产品需求分批建设。
- 如果当前目标是先形成稳定图形基础层，这些能力都应压后。
- 如果项目短期重点是 shader 迭代或 RT 演示，则可局部上调热重载或 AS 管理优先级。

详细改进原因：

- 这些能力的价值高度依赖项目阶段。
- 它们不是基础 RHI 成立的前提，而是“基础层稳定后”的增强层。

---

## 5. 推荐改进优先级与实施步骤

下面给出的不是简单的 P0/P1/P2 标签，而是推荐的实际改造顺序。

架构约束（未来计划统一前提）：

- 对外统一暴露 `Device` 作为 RHI 主入口（Facade）。
- `Device` 负责编排与生命周期入口，不承载各子系统实现细节。
- 内部按职责拆分为核心设备、presentation runtime、frame runtime、pipeline/binding 等成员子系统（`Big Facade, Small Internals`）。
- 本项目范围内固定单窗口、非 headless/offscreen。
- present 策略固定为 compute queue，不引入多路 present 路由。

### 阶段 1：补齐资源到命令的执行桥接

涉及项：`J14`, `J6`, `J7`

状态：`已完成（本阶段步骤已执行）`

为什么现在先做这些：

- 帧主线闭环已具备，下一阶段重点是“帧内部怎么安全执行”。
- barrier 和 upload 系统必须建立在提交模型和帧模型明确之后，否则容易出现后面再返工同步接口的问题。
- 并发问题要在这些工具层大规模使用前先清掉，否则会把不稳定基础扩散到更多子系统。

### 阶段 2：收紧架构边界，降低后续演进阻力

涉及项：`J1`, `J2`, `J5`, `J8`, `J9`, `J10`, `J11`, `J18`

状态：`大部分已完成（Session A/B/C）`

建议步骤：

1. 保持对外 `Device` 不变，内部拆分为核心设备、presentation、frame runtime、pipeline/binding 服务，并由 `Device` 统一编排。
2. [已完成] 统一命令录制入口，减少重复 helper，并按 `J9-A` 移除 `CommandRecorder::reset()`，统一 frame-scope pool reset。
3. [已完成] 按 `J10` 落实 allocator / persistent factory / frame-local arena 三类对象边界。
4. 保持三角色队列模型不扩展 `Present`，并把“compute present 固定策略”做成显式契约（初始化校验 + 文档 + API 约束）。
5. [已完成] 收敛 descriptor 子系统边界。

[新增] 阶段 2 剩余重点：
1. `J1/J2` 的结构收紧继续推进（进一步减少 `Device` 组合根上的行为粘连）。
2. 维持边界不回流，避免新功能把 descriptor runtime 或 persistent resource 创建再次塞回旧桶。

为什么第三步才做：

- 这些改造收益很高，但如果放在 P0 之前做，会因为主流程未定而不断返工。
- 先把帧流程、提交、资源桥接稳定下来，再做边界收敛，才能保证拆分不是“漂亮但空转”的重命名。

### 阶段 3：补齐通用图形 API 的关键能力面

涉及项：`J17`, `J20`, `J21`, `J15`, `J16`

建议步骤：

1. 先将当前能力要求收敛为单一产品 profile（集中定义，而非散落 assert）；不以“通用硬件覆盖”作为当前阶段目标。
2. 补齐 multi-stage graphics shader program。
3. [已更新] 落地 immutable sampler（当前 seam-only，需进入 layout 构建路径）与更精细的 descriptor capability 判断。
4. 引入 dynamic rendering convenience layer 与 pipeline cache。

为什么放在第四步：

- 这些能力决定系统是否“像一个完整现代图形 API 子集”，但它们建立在基础执行层已经稳定的前提上。
- 尤其是 graphics 多阶段和能力 profile 收敛，需要上层流程已稳定，才能清晰定义其接入点。

### 阶段 4：再做高级引擎化与生态能力

涉及项：`J23`, `J24`, `J25`, `J13`

建议步骤：

1. 根据并发模型优化锁粒度和 descriptor update 策略。
2. 规划 RenderGraph、Bindless、GPU Query、多队列协作。
3. 按项目目标推进 ImGui 后端、热重载、纹理系统、AS 管理。
4. 最后再处理对象轻量化、缓存压缩、便利工厂清理等微观收尾工作。

为什么最后做：

- 这部分大多属于“系统已经正确后，再提升能力和体验”。
- 如果提前做，往往会把问题复杂度抬高，而不是降低。

---

## 6. 建议的最小可执行改造路线

如果希望将本报告转化为实际开发路线，推荐按以下最小闭环推进：

1. `对外 Device 门面稳定 + 内部子系统拆分 + resource/command/descriptor 边界收敛`
2. `graphics multi-stage + 产品能力 profile 收敛 + immutable sampler + pipeline cache`
3. `RenderGraph / bindless / tooling / eco system`

这条路线的核心原则是：

- 先修“会错”的地方
- 再补“没闭环”的地方
- 再拆“太耦合”的地方
- 最后扩“更高级”的地方

---

## 7. 最终结论

两组 agent 的调研在大方向上高度一致：当前 `src/rhi` 的问题不是方向错误，而是系统已经明显向中层图形框架演进，但关键闭环仍未完成。

最重要的结论有四点：

1. **阶段 1（`J6/J7/J14`）已完成核心落地：submit2 统一、secondary pool 并发模型收敛、resourceOps 基础桥接层接入。**
2. **阶段 2 的关键边界项已在 Session A/B/C 落地：`J8/J9/J10/J11/J18` 已完成，`ResourceFactory + ResourcePool` 与 descriptor runtime 分层均已实体化。**
3. **在新增项目约束下，`J5` 维持“保持三角色并加固 compute-present 契约”的重判不变。**
4. **后续仍应坚持“边界收敛 -> 能力扩展 -> 生态增强”的顺序推进，当前直接下一步是 `J20`（immutable sampler 从 seam 走向落地）与 `J21`（multi-stage program）。**

因此，这份文档给出的最终判断是：

- 当前 RHI 已经具备较好的模块化方向和较强的底层 RAII 基础。
- 阶段 1 的执行桥接问题已完成主线闭环，但它还不能被认为是“边界稳定、能力面完整的现代图形 API 层”。
- 在阶段 2-3 完成前，不建议优先投入 RenderGraph、热重载、生态工具链等高级能力建设。
- 在“单窗口 + 非 headless/offscreen + compute present 固定”约束下，不建议为未计划场景引入额外运行时队列抽象。

`src/rhi` 已跨过 `J6/J7/J14` 与阶段 2 的核心边界改造门槛；下一阶段应聚焦 `J20/J21/J16/J15` 的能力面补齐，以稳健承接高层系统建设。
