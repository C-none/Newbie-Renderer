# Scene Phase6 报告状态标注与当前策略说明

## 1. 文档状态

本文件当前用于记录状态变更：

1. 之前的 Phase6 多策略对比与批测数据已转为历史归档信息。
2. 当前代码已回退到 Phase5 之后的功能基线，不再保留 Phase6 的策略对比/遥测接口与批测入口。

更新时间：2026-03-22

## 2. 当前采取策略

当前 Scene 提取路径已固化为单策略实现：

1. 使用 dedicated cached query 单路径提取。
2. 不再保留 broadCandidates 与 dedicatedCachedQuery 的运行时对比分支。
3. 不再提供 `compareExtractionStrategies` 与 `telemetrySnapshot` 这类性能分析接口。

实现位置：

1. `src/scene/nrScene.ixx` 中 `extractPackets(...)` 直接调用 dedicated 路径。
2. `src/scene/nrScene.ixx` 中 `extractPacketsDedicated(...)` 为当前唯一提取执行路径。

## 3. 测试标注与结果概要

本次回退后执行了 Scene 功能测试集合，结果如下（全部通过）：

| 测试项 | 关注点 | 结果 |
| --- | --- | --- |
| `nr_scene_phase1_test` | 基础导入与模板注册 | PASS |
| `nr_scene_phase2_test` | mesh/material/texture bridge | PASS |
| `nr_scene_phase25_test` | camera/light bridge | PASS |
| `nr_scene_phase3_test` | hierarchy/transform/bounds | PASS |
| `nr_scene_phase4_test` | frame serial + 上传生命周期 | PASS |
| `nr_scene_phase5_test` | packet extraction 功能正确性 | PASS |
| `nr_scene_upload_readback_test` | 上传与读回链路正确性 | PASS |

测试结论概要：

1. 加载功能正常（phase1/2/2.5）。
2. 实例与场景管理功能正常（phase3/phase5）。
3. 上传与回收路径正常（phase4 + upload/readback）。

## 4. 与历史 Phase6 报告的关系

历史 Phase6 批量策略对比结论仅用于当时的性能分析背景，不再代表当前代码状态。

当前生效结论以本文件第 2、3 节为准：

1. 策略已固定为 dedicated 单路径。
2. 功能质量由现行功能测试集合保障。
