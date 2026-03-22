# Scene 材质语义链整改执行计划

## 0. 文档目的

本文是对 [scene_module_rectification_checklist.md](./scene_module_rectification_checklist.md) 中 `3.1 P0：材质输入链补全` 的展开执行版。

目标不是重复原则，而是把 `3.1` 拆成：

1. 可按顺序实施的开发阶段
2. 精确到文件的修改范围
3. 可直接排进迭代的任务清单
4. 明确的测试矩阵与完成定义

---

## 1. 本计划覆盖范围

### 1.1 已确认必须支持的工作流

第一阶段必须支持：

1. Metallic/Roughness
   - `BASE_COLOR`
   - `METALLIC_FACTOR`
   - `ROUGHNESS_FACTOR`
2. Anisotropy
   - `ANISOTROPY_FACTOR`
3. Specular/Glossiness
   - `SPECULAR_FACTOR`
   - `GLOSSINESS_FACTOR`

同时必须纳入的通用输入：

1. `baseColor factor`
2. `emissive factor`
3. `opacity`
4. `alpha mode / blend semantic`
5. `two-sided`
6. `metallic factor`
7. `roughness factor`
8. `normal scale / bump scaling`
9. `ambient occlusion semantic`
10. `anisotropy factor`
11. `specular factor`
12. `glossiness factor`

### 1.2 本阶段不覆盖

本计划不负责：

1. sheen / clearcoat / transmission / volume
2. texture stack 的完整混合重建
3. specular color / ambient color / reflective color 的完整 shading 方案
4. renderer 最终 BRDF 实现
5. `uploadPending()` 异步化

---

## 2. 目标产出

本计划完成后，项目应具备下面能力：

1. `nr.load` 能从 Assimp 读取第一阶段确认范围内的材质 authoring 数据
2. `nr.resource::Material` 能稳定承载这些 canonical 材质信息
3. `nr.scene` 能把材质 authoring 输入正确桥接为 canonical material
4. `Scene` 的 `selection bits` 不再依赖默认值误判材质类别
5. `MaterialGpuData` 能携带第一阶段所需参数到 GPU
6. 现有 scene/upload/readback 测试能覆盖这些新增材质字段
7. graphics path 不因材质工作流不同而忽略任何对象

---

## 3. 执行顺序总览

建议按下面顺序实施：

1. 阶段 A：冻结数据契约
2. 阶段 B：补齐 `nr.load` 的 Assimp 材质读取
3. 阶段 C：扩展 `nr.resource::Material`
4. 阶段 D：重写 `SceneBridge` 材质归一化
5. 阶段 E：扩展 GPU 材质上传结构
6. 阶段 F：修正 `Scene` 选择位与提取逻辑
7. 阶段 G：补齐测试与真实样例验证

原因：

1. 先定契约，避免 load/resource/scene 三层反复改字段
2. 先拿到 authoring 输入，再做 canonical bridge
3. 先完成 CPU 数据闭环，再扩展 GPU data 和 packet 提取

---

## 4. 详细执行计划

## 阶段 A：冻结数据契约

### A.1 目标

统一 `load -> resource -> scene` 三层对第一阶段材质语义的字段约定。

### A.2 修改文件

1. [nrLoadType.ixx](D:/file/prog/Newbie-Renderer/src/load/nrLoadType.ixx)
2. [nrResourceMaterial.ixx](D:/file/prog/Newbie-Renderer/src/resource/nrResourceMaterial.ixx)
3. [scene_module_rectification_checklist.md](D:/file/prog/Newbie-Renderer/docs/scene_module_rectification_checklist.md)

### A.3 具体任务

1. 为 `nr::load::MaterialAsset` 增加 authoring 字段：
   - `doubleSided`
   - `alphaModeHint`
   - `alphaCutoff`
   - `normalScale`
   - `occlusionStrength`
   - `anisotropyFactor`
   - `specularFactor`
   - `glossinessFactor`
2. 为需要区分“缺失值”和“显式值”的字段确定 presence 语义：
   - 优先考虑 `std::optional<float>`
   - 或配套 `hasXxx` 标识
3. 为 `nr::resource::Material` 增加 canonical 字段：
   - `anisotropyFactor`
   - `specularFactor`
   - `glossinessFactor`
4. 加入工作流标识：
   - 建议 `MaterialWorkflowFlags`
   - 至少可表达 `metallicRoughness`
   - `specularGlossiness`
   - `anisotropy`

### A.4 输出结果

1. 三层材质字段命名统一
2. 可写出后续 bridge 规则，不再依赖口头约定

### A.5 完成定义

1. `load` 和 `resource` 的材质结构可以表达第一阶段确认范围
2. 字段职责清晰：
   - `load` 保存 authoring 原始输入
   - `resource` 保存 canonical 运行时语义

---

## 阶段 B：补齐 `nr.load` 的 Assimp 材质读取

### B.1 目标

让 `nr.load` 真正把第一阶段所需材质 authoring 信息从 Assimp 导入出来。

### B.2 修改文件

1. [nrLoadAssimp.ixx](D:/file/prog/Newbie-Renderer/src/load/nrLoadAssimp.ixx)
2. [nrLoadType.ixx](D:/file/prog/Newbie-Renderer/src/load/nrLoadType.ixx)

### B.3 具体任务

1. 在 `appendMaterials(...)` 中读取 material scalar / flag：
   - `AI_MATKEY_BASE_COLOR`
   - `AI_MATKEY_METALLIC_FACTOR`
   - `AI_MATKEY_ROUGHNESS_FACTOR`
   - `AI_MATKEY_ANISOTROPY_FACTOR`
   - `AI_MATKEY_SPECULAR_FACTOR`
   - `AI_MATKEY_GLOSSINESS_FACTOR`
   - `AI_MATKEY_OPACITY`
   - `AI_MATKEY_TWOSIDED`
   - `AI_MATKEY_BUMPSCALING`
   - `AI_MATKEY_COLOR_EMISSIVE`
   - `AI_MATKEY_BLEND_FUNC`
2. 若 importer 能稳定提供 alpha mode / alpha cutoff，则补齐相应读取逻辑
3. 对每个读取字段建立明确策略：
   - 读取成功则写入 authoring 字段
   - 缺失则保留缺失状态，不在 load 阶段偷偷填默认值
4. texture semantic 识别补齐：
   - `BASE_COLOR`
   - `EMISSION_COLOR`
   - `NORMAL_CAMERA`
   - `AMBIENT_OCCLUSION`
   - `GLTF_METALLIC_ROUGHNESS`
   - `DIFFUSE`
   - `EMISSIVE`
   - `NORMALS`
   - `LIGHTMAP`
   - `HEIGHT`
   - `METALNESS`
   - `DIFFUSE_ROUGHNESS`
   - `SPECULAR`
   - `SHININESS`
   - `ANISOTROPY`
5. 为不能立即 canonical 化的 texture semantic 保留来源信息，不在此阶段丢弃

### B.4 约束

1. `nr.load` 不负责做最终材质工作流判定
2. `nr.load` 不负责决定 `SceneSelectionBits`
3. `nr.load` 只负责忠实保留 importer 输出

### B.5 完成定义

1. `nr::load::MaterialAsset` 能稳定携带第一阶段 authoring 输入
2. 缺失值与显式值在数据结构上可区分

---

## 阶段 C：扩展 `nr.resource::Material`

### C.1 目标

把 `nr.resource::Material` 扩展成可服务当前 renderer 的 canonical 材质结构。

### C.2 修改文件

1. [nrResourceMaterial.ixx](D:/file/prog/Newbie-Renderer/src/resource/nrResourceMaterial.ixx)

### C.3 具体任务

1. 在 `Material` 中加入新增 canonical 参数：
   - `anisotropyFactor`
   - `specularFactor`
   - `glossinessFactor`
2. 加入工作流表达：
   - `materialWorkflowFlags`
   - 或等价枚举/bitmask
3. 保持现有字段继续可用：
   - `baseColorFactor`
   - `emissiveFactor`
   - `metallicFactor`
   - `roughnessFactor`
   - `normalScale`
   - `occlusionStrength`
   - `alphaCutoff`
   - `alphaMode`
   - `doubleSided`
4. 如有必要，增加辅助方法：
   - `usesMetallicRoughnessWorkflow()`
   - `usesSpecularGlossinessWorkflow()`
   - `usesAnisotropy()`

### C.4 完成定义

1. `nr::resource::Material` 可以独立表达第一阶段 canonical 语义
2. 不需要再通过外部 side table 才能知道材质工作流

---

## 阶段 D：重写 `SceneBridge` 材质归一化

### D.1 目标

把 `nr.load::MaterialAsset` authoring 输入稳定映射到 `nr.resource::Material` canonical 语义。

### D.2 修改文件

1. [nrScene.ixx](D:/file/prog/Newbie-Renderer/src/scene/nrScene.ixx)
2. [nrSceneUtils.ixx](D:/file/prog/Newbie-Renderer/src/scene/nrSceneUtils.ixx)

### D.3 具体任务

1. 重构 `bridgeMaterials(...)`：
   - 不再只拷贝现有少数字段
   - 按新字段进行完整归一化
2. 落地 slot 归一化规则：
   - `baseColor` 优先 `BASE_COLOR`，兼容 `DIFFUSE`
   - `emissive` 优先 `EMISSION_COLOR`，兼容 `EMISSIVE`
   - `normal` 优先 `NORMAL_CAMERA`，兼容 `NORMALS`
   - `occlusion` 优先 `AMBIENT_OCCLUSION`，兼容 `LIGHTMAP`
   - `metallicRoughness` 优先 `GLTF_METALLIC_ROUGHNESS`
3. 对 `HEIGHT` 的处理明确为：
   - 第一阶段只做保守 fallback
   - 不把它误当作已经等价 normal map
4. 工作流归一化策略：
   - 若存在 `METALLIC_FACTOR / ROUGHNESS_FACTOR`，标记 metallic-roughness
   - 若存在 `SPECULAR_FACTOR / GLOSSINESS_FACTOR`，标记 spec-gloss
   - 若存在 `ANISOTROPY_FACTOR`，标记 anisotropy
   - 多工作流同时存在时保留组合信息，不做强制互斥
5. 透明语义归一化：
   - `opacity + blend semantic` 共同决定 `alphaMode`
   - `alphaCutoff` 只有在 mask 语义明确时才用于 `AlphaMode::mask`

### D.4 需要补的辅助函数

建议新增：

1. `classifyAlphaModeHint(...)`
2. `resolveMaterialWorkflowFlags(...)`
3. `resolveBaseColorSlot(...)`
4. `resolveNormalSlot(...)`
5. `resolveOcclusionSlot(...)`

### D.5 完成定义

1. `bridgeMaterials()` 不再依赖默认值推断主要材质语义
2. `nr.resource::Material` 的关键字段都来自明确的 authoring 输入或明确 fallback 规则

---

## 阶段 E：扩展 GPU 材质上传结构

### E.1 目标

让第一阶段新增的 canonical 材质参数可以进入 GPU 侧数据结构。

### E.2 修改文件

1. [nrSceneType.ixx](D:/file/prog/Newbie-Renderer/src/scene/nrSceneType.ixx)
2. [nrScene.ixx](D:/file/prog/Newbie-Renderer/src/scene/nrScene.ixx)
3. 对应 upload/readback 测试文件

### E.3 具体任务

1. 扩展 `detail::MaterialGpuData`
2. 加入至少这些字段：
   - `anisotropyFactor`
   - `specularFactor`
   - `glossinessFactor`
   - 工作流标识位
3. 更新 `buildMaterialGpuData(...)`
4. 更新 material upload byte 布局的期望值构造
5. 更新 upload/readback 对比逻辑

### E.4 完成定义

1. CPU canonical material 与 GPU upload layout 一致
2. upload/readback 测试能验证新增字段

---

## 阶段 F：修正 `Scene` 选择位与提取逻辑

### F.1 目标

让 `SceneSelectionBits` 基于真实材质 authoring 生效，同时遵守“graphics path 不能忽略任何物体”的约束。

### F.2 修改文件

1. [nrScene.ixx](D:/file/prog/Newbie-Renderer/src/scene/nrScene.ixx)
2. [nrSceneType.ixx](D:/file/prog/Newbie-Renderer/src/scene/nrSceneType.ixx)

### F.3 具体任务

1. 重写 `defaultSelectionBits(...)` 的判断依据
2. 保证以下规则成立：
   - 所有 renderable 都进入 graphics path 候选集
   - 材质工作流不同不能导致对象被 graphics path 忽略
   - `rasterTransparent` / `rtTransparent` 仍可作为分类位保留
   - 但不是“是否存在 graphics packet”的唯一门槛
3. 确保 graphics packet 规则为：
   - 不做按透明度排序
   - 但保留对象参与 GBuffer/几何阶段的能力
4. 确保 RT 分类规则继续可用：
   - `rtMain`
   - `rtTransparent`
   - `alphaTest`

### F.4 完成定义

1. graphics path 对所有材质对象都能产出可消费候选
2. Scene 不会因为材质工作流不同而把对象从 graphics packet 中剔除

---

## 阶段 G：补齐测试与样例验证

### G.1 目标

让新增材质语义链具备可回归验证能力。

### G.2 修改文件

建议新增或扩展：

1. [nr_load_model_asset_test.cpp](D:/file/prog/Newbie-Renderer/test/profile/nr_load_model_asset_test.cpp)
2. [nr_scene_phase2_test.cpp](D:/file/prog/Newbie-Renderer/test/scene/nr_scene_phase2_test.cpp)
3. [nr_scene_phase5_test.cpp](D:/file/prog/Newbie-Renderer/test/scene/nr_scene_phase5_test.cpp)
4. [nr_scene_upload_readback_test.cpp](D:/file/prog/Newbie-Renderer/test/scene/nr_scene_upload_readback_test.cpp)
5. 新增专门材质测试文件，建议：
   - `test/profile/nr_load_material_workflow_test.cpp`
   - `test/scene/nr_scene_material_workflow_test.cpp`

### G.3 测试矩阵

至少覆盖下面样例：

1. Metallic/Roughness 材质
2. Specular/Glossiness 材质
3. 带 `ANISOTROPY_FACTOR` 的材质
4. 双面 opaque 材质
5. alpha-blend 材质
6. alpha-mask 材质
7. 含 `normalScale / bump scaling` 的材质
8. AO 语义材质

### G.4 测试层级

1. `load` 层测试
   - 检查 Assimp key 是否正确进入 `nr::load::MaterialAsset`
2. `resource/bridge` 层测试
   - 检查 canonical `nr::resource::Material` 是否正确
3. `scene` 层测试
   - 检查 `selection bits`
   - 检查 graphics path 不忽略对象
4. upload/readback 测试
   - 检查 `MaterialGpuData` 新字段布局

### G.5 完成定义

1. 三个工作流都至少有一个真实样例通过
2. scene packet 提取能覆盖这些材质
3. upload/readback 能验证 GPU data 正确

---

## 5. 建议提交切片

建议不要一次性大提交，推荐按下面顺序切 commit：

1. `load/material contract`：类型定义与 authoring 字段
2. `load/assimp material import`：Assimp key 读取
3. `resource/material canonical fields`：`nr::resource::Material` 扩展
4. `scene/material bridge rewrite`：`bridgeMaterials()` 与语义归一化
5. `scene/material gpu payload`：`MaterialGpuData` 扩展
6. `scene/selection bits fix`：graphics path 不忽略对象
7. `tests/material workflows`：补全所有测试

这样做的好处：

1. 每一步都可独立 review
2. 回归问题更容易定位
3. 减少跨层大改动一次性出错

---

## 6. 风险点与处理策略

### 风险 1：Assimp importer 对不同格式输出不一致

处理：

1. `load` 层保留 presence 语义
2. 不把“缺失值”误写成“authoring 明确给了默认值”

### 风险 2：Spec/Gloss 与 Metal/Rough 同时出现

处理：

1. 不强制互斥
2. 记录工作流组合信息
3. shading 端再决定最终优先级

### 风险 3：graphics path 与透明语义发生误解

处理：

1. graphics path 不做透明排序
2. 但任何材质都不得被 graphics path 忽略

### 风险 4：GPU layout 更新导致测试大面积失效

处理：

1. 单独切分 `MaterialGpuData` commit
2. 同步更新 readback expected bytes

---

## 7. Definition Of Done

当下面条件全部满足时，`3.1` 可视为完成：

1. `nr.load` 已读取第一阶段确认的 Assimp 材质 authoring 输入
2. `nr.resource::Material` 已扩展到可表达这三类工作流
3. `SceneBridge` 已完成 canonical 归一化
4. `MaterialGpuData` 已包含新增参数
5. `SceneSelectionBits` 已不再依赖默认值误判材质
6. graphics path 对所有材质对象都不忽略
7. 相关测试全部通过

---

## 8. 下一份文档建议

本计划执行完成后，建议再补一份专门文档：

1. `scene_material_semantic_mapping.md`

用于记录：

1. Assimp key -> `nr.load::MaterialAsset`
2. `nr.load::MaterialAsset` -> `nr.resource::Material`
3. `nr.resource::Material` -> `MaterialGpuData`
4. `Material` -> `SceneSelectionBits`

这样后续做 renderer 或 shader 侧联调时，不需要再从代码里倒推语义来源。
