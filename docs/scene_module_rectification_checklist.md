# Scene 模块整改清单（执行版）

## 0. 文档定位

本文基于 [scene_module_flecs_architecture.md](./scene_module_flecs_architecture.md) 的原始规划，但用于记录当前阶段已经确认的执行决策、阻塞项与后续开发顺序。

本文的作用不是重复原设计，而是把当前讨论已经达成一致的内容收敛成一份可落地的整改清单。

当前代码基线说明：

1. `scene` 现有功能测试集合已通过：
   - `nr_scene_phase1_test`
   - `nr_scene_phase2_test`
   - `nr_scene_phase25_test`
   - `nr_scene_phase3_test`
   - `nr_scene_phase4_test`
   - `nr_scene_phase5_test`
   - `nr_scene_upload_readback_test`
2. 当前判断重点不再是“Scene 是否存在基础闭环”，而是“Scene 还缺哪些内容才能稳定进入下一个渲染器模块”。

---

## 1. 已确认的架构决策

### 1.1 上传策略

当前渲染器架构较简单，并且会保证“所有上传完成之后才开始渲染”。

因此本阶段结论为：

1. `uploadPending()` 的异步化、跨帧化、无阻塞 acquire 提交不是当前阻塞项。
2. 现有同步上传路径只需要保证正确性，不需要在当前阶段优先做性能型重构。
3. `uploadPending()` 的进一步演进保留到“未来扩展项”。

### 1.2 材质语义链推进顺序

本阶段不直接拍板最终材质语义链，而采用两段式推进：

1. 先整理 Assimp 官方材质系统中可解析出的属性与纹理语义。
2. 由用户确认“当前渲染器真正要考虑哪些材质属性”。
3. 在确认之后，再单独产出 `nr.load -> nr.scene -> nr.resource` 的材质语义链完善方案。

### 1.3 模板/实例卸载后的资源释放

这项从现在开始视为必须项，而不是未来优化项。

必须覆盖的范围：

1. template 卸载
2. instance 销毁
3. template pin 释放
4. GPU 资源延迟退休
5. CPU canonical 资源释放策略

### 1.4 Phase6 处理

原设计文档中的独立 `Phase6` 不再保留在当前执行计划中。

原因：

1. 性能观测与策略对比工作曾经做过。
2. 现阶段策略已经固化并实施。
3. 当前主线不再需要把 telemetry / benchmark / compare branch 作为独立 phase 推进。

因此本文中的开发顺序不再包含独立 `Phase6`。

### 1.5 渲染排序原则

当前 renderer 约束为：

1. graphics pipeline 只负责 GBuffer 生成
2. 透明渲染走 ray tracing pass

因此本阶段规则为：

1. graphics packet 不需要基于透明度做深度/混合排序
2. graphics 侧只保留稳定的 GBuffer 友好排序键，例如：
   - pass
   - pipeline family
   - material
   - mesh
   - submesh
   - instance
3. 透明对象不需要为了 graphics pass 再单独建立“按透明度排序”的 Scene 规则
4. graphics path 无论材质类型如何，都不能忽略物体
5. “透明表现主要在 RT pass 完成”不等于“graphics path 可以跳过透明物体”
6. Scene 在 graphics packet 提取阶段不能仅因为材质为 transparent / spec-gloss / anisotropy 等工作流就把对象过滤掉

### 1.6 默认相机

如果场景全部加载完成之后没有解析出任何相机，则 Scene 层自动生成一个默认相机。

默认建议值：

1. position = `(0, 0, 0)`
2. forward = `(0, 0, -1)`
3. up = `(0, 1, 0)`
4. projection = perspective
5. verticalFov = `60 deg`
6. near = `0.1`
7. far = `1000`

这是 runtime fallback，相机只服务于运行时，不回写源资源文件。

---

## 2. Assimp 材质属性调研（待确认输入范围）

### 2.1 官方资料结论

Assimp 的 `aiMaterial` 不是固定字段 struct，而是一个 key-value material property 集合。

官方文档明确说明：

1. `aiMaterial` 通过一组属性键访问材质数据
2. 标准键以 `AI_MATKEY_*` 形式提供
3. 纹理还带有 texture stack / texture semantic / UV mapping 相关属性

这意味着：

1. “Assimp 能解析哪些材质属性”要分成“公共 material keys”与“现代 PBR keys / texture semantics”两层看
2. 不同 importer、不同文件格式、不同导出器不保证所有属性都一定存在
3. 当前项目不能假设所有 importer 都会给出完整 PBR 材质

### 2.2 官方文档中明确列出的经典材质属性

Assimp 官方文档 `use_the_lib` 中明确列出下列经典材质属性：

| 类别 | 属性 |
| --- | --- |
| 标识 | `NAME` |
| 颜色 | `COLOR_DIFFUSE`、`COLOR_SPECULAR`、`COLOR_AMBIENT`、`COLOR_EMISSIVE`、`COLOR_TRANSPARENT`、`COLOR_REFLECTIVE` |
| 开关/模式 | `TWOSIDED`、`SHADING_MODEL`、`BLEND_FUNC`、`WIREFRAME` |
| 标量 | `OPACITY`、`SHININESS`、`SHININESS_STRENGTH`、`REFLECTIVITY`、`REFRACTI`、`TRANSPARENCYFACTOR`、`BUMPSCALING` |

这些属性来自 Assimp 官方文档中的 material keys 列表，而不是第三方整理。

### 2.3 官方文档中明确列出的纹理栈与采样相关属性

Assimp 官方文档同时列出了 texture stack / sampling 元数据：

| 类别 | 属性 |
| --- | --- |
| 纹理路径 | `TEXTURE(t, n)` |
| 强度/混合 | `TEXBLEND(t, n)`、`TEXOP(t, n)` |
| 映射 | `MAPPING(t, n)`、`UVWSRC(t, n)` |
| 包裹模式 | `MAPPINGMODE_U(t, n)`、`MAPPINGMODE_V(t, n)` |
| 额外坐标/标记 | `TEXMAP_AXIS(t, n)`、`TEXFLAGS(t, n)` |

这部分说明 Assimp 不只是给“一个 texture path”，还可能给出：

1. 纹理属于哪类 semantic
2. 使用哪个 UV channel
3. 纹理栈如何混合
4. 包裹模式和 alpha 使用提示

### 2.4 `aiTextureType` 暴露的纹理语义

Assimp 当前官方 `material.h` 中公开的常见 texture semantic 包括：

| 语义组 | `aiTextureType` |
| --- | --- |
| 经典实时材质 | `DIFFUSE`、`SPECULAR`、`AMBIENT`、`EMISSIVE`、`HEIGHT`、`NORMALS`、`SHININESS`、`OPACITY`、`DISPLACEMENT`、`LIGHTMAP`、`REFLECTION` |
| 常见 PBR | `BASE_COLOR`、`NORMAL_CAMERA`、`EMISSION_COLOR`、`METALNESS`、`DIFFUSE_ROUGHNESS`、`AMBIENT_OCCLUSION` |
| 扩展 PBR | `SHEEN`、`CLEARCOAT`、`TRANSMISSION`、`ANISOTROPY`、`GLTF_METALLIC_ROUGHNESS` |
| 兼容/其他 | `UNKNOWN`、`MAYA_BASE`、`MAYA_SPECULAR`、`MAYA_SPECULAR_COLOR`、`MAYA_SPECULAR_ROUGHNESS` |

### 2.5 当前官方 `material.h` 中可见的 PBR 标量/因子键

Assimp 当前官方 `include/assimp/material.h` 中可以直接看到的 PBR 相关 key 至少包括：

| 工作流/扩展 | `AI_MATKEY_*` |
| --- | --- |
| Metallic/Roughness | `BASE_COLOR`、`METALLIC_FACTOR`、`ROUGHNESS_FACTOR` |
| 各向异性 | `ANISOTROPY_FACTOR` |
| Specular/Glossiness | `SPECULAR_FACTOR`、`GLOSSINESS_FACTOR` |
| Sheen | `SHEEN_COLOR_FACTOR`、`SHEEN_ROUGHNESS_FACTOR` |
| Clearcoat | `CLEARCOAT_FACTOR`、`CLEARCOAT_ROUGHNESS_FACTOR` |
| Transmission / Volume | `TRANSMISSION_FACTOR`、`VOLUME_THICKNESS_FACTOR`、`VOLUME_ATTENUATION_DISTANCE` |

说明：

1. 这里列的是当前官方 header 中可明确看到的公共 key
2. 不代表每个 importer、每个文件格式都会稳定填充这些值
3. 当前项目如果要依赖这些字段，必须先确认目标资产格式与 Assimp importer 的真实输出覆盖度

### 2.6 当前仓库与 Assimp 能力之间的差距

当前仓库 `nr.load` 材质路径的现实情况是：

1. 已读取 material name
2. 已读取 texture binding 与 texture semantic
3. 还没有系统读取 Assimp 的颜色、双面、透明、blend、shininess、PBR factor 等数值属性

这意味着：

1. `nr.resource::Material` 当前很多默认值不是“来自源资产”，而是“本地默认值”
2. `scene` 当前基于 `Material` 做的 `rasterOpaque / rasterTransparent / rtTransparent / alphaTest` 分类缺少稳定 authoring 输入
3. 材质链要进入下一步之前，必须先确认“哪些 Assimp 属性值得被正式接进来”

### 2.7 已确认的第一阶段材质支持范围

根据当前确认，第一阶段正式纳入下面三类工作流及其相关参数：

| 工作流 | 第一阶段确认支持 |
| --- | --- |
| Metallic/Roughness | `BASE_COLOR`、`METALLIC_FACTOR`、`ROUGHNESS_FACTOR` |
| Anisotropy | `ANISOTROPY_FACTOR` |
| Specular/Glossiness | `SPECULAR_FACTOR`、`GLOSSINESS_FACTOR` |

同时继续纳入此前 A 组默认输入：

1. `baseColor factor`
2. `emissive factor`
3. `two-sided`
4. `opacity / blend semantic`
5. `metallic factor`
6. `roughness factor`
7. `normal / bump related scale`
8. `ambient occlusion semantic`

把上面两部分合并后，当前第一阶段的必选材质输入集合为：

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

### 2.8 第一阶段暂不纳入范围

当前明确暂缓，不进入第一阶段必做项：

1. `specular color`
2. `shininess`
3. `shininess strength`
4. `reflectivity`
5. `refraction index`
6. `ambient color`
7. `reflective color`
8. `transparent color`
9. `sheen`
10. `clearcoat`
11. `transmission / volume`
12. `texture stack` 的完整混合运算：`TEXBLEND / TEXOP`
13. 多层 texture stack 的保真重建

### 2.9 第一阶段纹理语义归一化假设

为了让当前渲染器尽快进入稳定开发，第一阶段采用下面的归一化规则：

1. `baseColor` 槽优先接 `BASE_COLOR`，其次兼容 `DIFFUSE`
2. `emissive` 槽优先接 `EMISSION_COLOR`，其次兼容 `EMISSIVE`
3. `normal` 槽优先接 `NORMAL_CAMERA`，其次兼容 `NORMALS`，最后才考虑 `HEIGHT` 的保守 fallback
4. `occlusion` 槽优先接 `AMBIENT_OCCLUSION`，其次兼容 `LIGHTMAP`
5. `metallicRoughness` 槽优先接 `GLTF_METALLIC_ROUGHNESS`
6. `METALNESS`、`DIFFUSE_ROUGHNESS`、`SPECULAR`、`SHININESS`、`ANISOTROPY` 这些语义在第一阶段至少要被识别并保留来源信息，但不要求在本阶段完成复杂贴图打包

说明：

1. 当前确认的是“工作流和参数要支持”
2. 对部分高复杂度贴图工作流，第一阶段先保证标量参数和基础语义链正确落地
3. 若后续 renderer 明确需要 spec/gloss 或 anisotropy 的专用贴图输入，再补第二轮 canonical slot 扩展

---

## 3. 当前阻塞项整改清单

### 3.1 P0：材质输入链补全

目标：

1. 把“Scene 里的材质分类和 packet 选择”建立在真实 importer 输出上
2. 避免继续依赖默认值误导渲染路径

### 3.1.1 数据契约整改

`src/load/nrLoadType.ixx` 中的 `MaterialAsset` 需要从“轻量占位”扩展成“authoring 输入容器”。

建议新增或补齐：

1. `doubleSided`
2. `alphaModeHint`
3. `alphaCutoff`
4. `normalScale`
5. `occlusionStrength`
6. `anisotropyFactor`
7. `specularFactor`
8. `glossinessFactor`
9. 需要时为“源资产是否显式提供该值”保留 presence 语义，优先考虑 `std::optional<T>`

`src/resource/nrResourceMaterial.ixx` 中的 `Material` 需要扩展成 canonical 运行时材质：

1. 保留现有：
   - `baseColorFactor`
   - `emissiveFactor`
   - `metallicFactor`
   - `roughnessFactor`
   - `normalScale`
   - `occlusionStrength`
   - `alphaCutoff`
   - `alphaMode`
   - `doubleSided`
2. 新增：
   - `anisotropyFactor`
   - `specularFactor`
   - `glossinessFactor`
3. 增加工作流标识，建议使用低成本枚举或 bitmask，而不是多个松散布尔值

### 3.1.2 `nr.load` 实现整改

`src/load/nrLoadAssimp.ixx` 需要显式读取下列 Assimp 材质键：

1. `AI_MATKEY_BASE_COLOR`
2. `AI_MATKEY_METALLIC_FACTOR`
3. `AI_MATKEY_ROUGHNESS_FACTOR`
4. `AI_MATKEY_ANISOTROPY_FACTOR`
5. `AI_MATKEY_SPECULAR_FACTOR`
6. `AI_MATKEY_GLOSSINESS_FACTOR`
7. `AI_MATKEY_OPACITY`
8. `AI_MATKEY_TWOSIDED`
9. `AI_MATKEY_BUMPSCALING`
10. `AI_MATKEY_COLOR_EMISSIVE`
11. `AI_MATKEY_BLEND_FUNC`
12. 若 importer 可用，则补 `alpha mode / alpha cutoff` 相关读取

同时必须补齐 texture semantic 的显式识别：

1. `BASE_COLOR`
2. `EMISSION_COLOR`
3. `NORMAL_CAMERA`
4. `AMBIENT_OCCLUSION`
5. `GLTF_METALLIC_ROUGHNESS`
6. `DIFFUSE`
7. `EMISSIVE`
8. `NORMALS`
9. `LIGHTMAP`
10. `HEIGHT`
11. `METALNESS`
12. `DIFFUSE_ROUGHNESS`
13. `SPECULAR`
14. `SHININESS`
15. `ANISOTROPY`

要求：

1. `nr.load` 保留 importer 原始语义，不在 load 阶段做过度归一化
2. 对缺失值与默认值要区分
3. 对无法直接稳定解释的 importer 输出，先保留原始值，不在 load 阶段静默丢弃

### 3.1.3 `nr.resource` canonical 归一化整改

`SceneBridge` 进入 `nr.resource::Material` 时，需要把上游 authoring 数据整理成统一 canonical 语义。

第一阶段明确要求：

1. `baseColor factor` 进入 canonical `baseColorFactor`
2. `emissive factor` 进入 canonical `emissiveFactor`
3. `opacity + blend semantic` 共同决定 `alphaMode`
4. `two-sided` 进入 `doubleSided`
5. `metallic factor` 进入 `metallicFactor`
6. `roughness factor` 进入 `roughnessFactor`
7. `normalScale / bump scaling` 进入 `normalScale`
8. AO 相关输入进入 `occlusionStrength` 或至少稳定进入 AO 语义路径
9. `anisotropyFactor` 进入 canonical 字段
10. `specularFactor` 进入 canonical 字段
11. `glossinessFactor` 进入 canonical 字段

归一化策略建议：

1. `opacity` 低于阈值且具备 blend authoring 语义时，进入 `AlphaMode::blend`
2. `alpha cutoff` 只有在明确 mask 语义时才进入 `AlphaMode::mask`
3. `specular/glossiness` 与 `metallic/roughness` 不强制互斥，但需要记录工作流优先级
4. `anisotropy` 在第一阶段先保证数据存在并可上传，不要求本阶段完成 shading 侧完整消耗

### 3.1.4 `SceneBridge` 与 `Scene` 逻辑整改

`src/scene/nrScene.ixx` 需要至少修改下面几处：

1. `bridgeMaterials()`
2. `buildMaterialGpuData()`
3. `defaultSelectionBits()`

具体任务：

1. `bridgeMaterials()` 使用新增的 load 字段构建 canonical `nr.resource::Material`
2. `buildMaterialGpuData()` 扩展上传结构，纳入：
   - `anisotropyFactor`
   - `specularFactor`
   - `glossinessFactor`
3. `defaultSelectionBits()` 只依据真实 authoring 结果判断：
   - `rasterOpaque`
   - `rasterTransparent`
   - `rtMain`
   - `rtTransparent`
   - `alphaTest`
4. graphics 路径不再为了透明排序增加额外逻辑
5. 透明物体仍可在 RT path 中承担主要材质表现
6. 但 graphics path 不能因为材质类型而忽略对象，Scene 仍需为所有物体提供 graphics 侧参与能力
7. 任何“是否进入 graphics path”的判断都不能只由 `alphaMode` 或工作流类别单独决定

### 3.1.5 GPU 数据结构整改

`detail::MaterialGpuData` 需要扩展，以容纳第一阶段确认的工作流参数。

建议新增：

1. `anisotropyFactor`
2. `specularFactor`
3. `glossinessFactor`
4. 工作流标识位

要求：

1. 结构扩展后同步更新 upload/readback 测试
2. 不要在 Scene GPU data 中直接丢失工作流来源
3. 仍然允许 renderer 后续自行决定最终 shading 分支

### 3.1.6 测试整改

需要补齐三层测试：

1. `load` 层：
   - Assimp 材质 key 是否成功进入 `nr::load::MaterialAsset`
2. `scene bridge` 层：
   - `nr::load::MaterialAsset -> nr::resource::Material` 归一化是否正确
3. `scene upload/extract` 层：
   - 新字段是否进入 `MaterialGpuData`
   - `selection bits` 是否基于真实材质 authoring 正确生成

建议至少新增下面几类样例：

1. Metallic/Roughness 材质
2. Specular/Glossiness 材质
3. 带 `ANISOTROPY_FACTOR` 的材质
4. 双面 opaque 材质
5. alpha-blend 材质
6. alpha-mask 材质
7. 含 normal scale / bump scaling 的材质

验收标准：

1. `Material` 中关键渲染字段来自 imported authoring data，而不是默认值
2. `Scene` 的 `selection bits` 不再依赖“未初始化材质语义”
3. `MaterialGpuData` 能携带第一阶段确认的工作流参数
4. 至少有一个真实模型测试验证这三类工作流数据已进入 Scene

### 3.2 P0：模板/实例卸载后的资源释放

目标：

补齐从 template / instance 生命周期到资源生命周期的闭环。

必须完成：

1. 明确 template 销毁 API：
   - `destroyTemplate(...)`
   - 或等价 unload 接口
2. template 销毁时释放 template pin
3. instance 销毁时正确减少 template live count
4. 当 pin/ref 计数归零后，进入资源回收判定
5. GPU 资源走 `retireAfterSerial` 延迟退休
6. CPU canonical 资源根据保留策略释放或降级保留
7. 加测试覆盖：
   - load -> instantiate -> destroyInstance -> destroyTemplate
   - 重复注册/卸载同一资源
   - 卸载后重新加载 generation / handle 正确性

必须明确的策略点：

1. template 未卸载前，template pin 不得释放
2. 有 live instance 时，不允许无条件销毁 template
3. GPU 资源释放不能绕过现有 retire 机制
4. CPU/GPU 释放都不能依赖 Flecs observer

### 3.3 P0：默认相机 fallback

目标：

保证 scene 在“无相机输入”时仍可进入最小运行态。

必须完成：

1. Scene 注册完成后，如果 registry 中没有相机资产，则创建默认相机
2. 提供最小的 active camera 选择规则
3. 默认相机不依赖源文件 authoring
4. 增加测试：
   - 无 camera 的 scene 可正常得到 fallback camera
   - 有 imported camera 时不生成 fallback

### 3.4 P1：提取接口与当前 renderer 的最终对齐

目标：

让 Scene 从“自测模块”真正变成下游 renderer 的数据提供者。

必须完成：

1. 把 `ScenePacketSet` 接到至少一条真实下游消费链
2. GBuffer path 使用 Scene 提供的 graphics packet
3. RT path 使用 Scene 提供的 RT / TLAS 输入
4. `primaryCameraFrustum` 必须接到真实 active camera，而不是空占位
5. `tlasBuildInput` 需要独立输出形状，不再只复用 `rtInstances`

验收标准：

1. 主渲染路径不再绕开 `nr.scene`
2. `ScenePacketSet` 至少被一个 graphics pass 和一个 RT path 真实消费

---

## 4. 明确降级为“未来扩展项”的内容

### 4.1 `uploadPending()` 的异步化和无阻塞化

这项保留到未来扩展，不作为当前阻塞项。

未来再做时再考虑：

1. transfer -> graphics acquire 不阻塞 CPU
2. 真正跨帧的 `waitingAcquire`
3. 渲染与上传重叠
4. 上传 budget 与后台流式加载

### 4.2 重新引入独立 telemetry / benchmark phase

当前执行计划不再保留独立 `Phase6`。

只有在下面情况出现时再单独重开：

1. Flecs query 策略发生变化
2. hierarchy storage 策略重新调整
3. 实际渲染负载下出现 candidate 扫描或 rematch 问题
4. streaming / editor 场景引入新的性能风险

---

## 5. 修订后的开发顺序

### 阶段 1：确认材质输入范围

输出物：

1. 用户确认版材质属性范围
2. 第一阶段必须接入的材质语义清单
3. 可延期材质特性清单

### 阶段 2：实现材质语义链

输出物：

1. `nr.load` 材质属性采集补全
2. `nr.resource::Material` 字段补全
3. `SceneBridge` 材质归一化补全
4. Scene 选择位与材质分类修正

### 阶段 3：补齐模板/实例卸载与资源释放

输出物：

1. template unload / destroy API
2. pin 释放
3. CPU/GPU 资源回收闭环
4. unload / reload 回归测试

### 阶段 4：加入默认相机 fallback

输出物：

1. runtime 默认相机生成规则
2. active camera 最小选择规则
3. 无相机场景回归测试

### 阶段 5：接入真实 renderer 消费链

输出物：

1. GBuffer path 通过 Scene packet 驱动
2. RT path 通过 Scene packet 驱动
3. `primaryCameraFrustum` 与真实 active camera 对接
4. `tlasBuildInput` 独立结构

### 未来扩展

1. `uploadPending()` 异步化
2. streaming 资源管理
3. 如有必要，重新做 telemetry / benchmark 专项

---

## 6. 建议在下一轮确认的问题

下一轮讨论建议优先确认下面几项：

1. 第一阶段材质字段最终纳入哪些：
   - `baseColor / emissive / metallic / roughness / opacity / two-sided / normalScale / AO`
   - 是否还要带上 `specular / glossiness / shininess`
2. graphics path 中“所有材质对象都必须参与”的最小输出契约是什么
   - 例如：是否所有对象都要产出统一 graphics packet，还是只要求参与 visibility / GBuffer 几何阶段
3. 默认相机是否只需要“固定原点相机”，还是需要最小场景包围盒 framing
4. template 卸载时是否允许级联销毁 live instance，还是必须显式先销毁 instance

---

## 7. 参考资料

### 仓库内文档

1. [scene_module_flecs_architecture.md](./scene_module_flecs_architecture.md)
2. [scene_phase6_batch_strategy_report.md](./scene_phase6_batch_strategy_report.md)
3. [resource_module_architecture.md](./resource_module_architecture.md)

### Assimp 官方资料

1. 官方文档 `Working with the Asset-Importer-Lib`  
   <https://assimp-docs.readthedocs.io/en/latest/usage/use_the_lib.html>
2. 官方文档仓库同源文件 `use_the_lib.rst`  
   <https://raw.githubusercontent.com/assimp/assimp-docs/master/source/usage/use_the_lib.rst>
3. 官方仓库 `include/assimp/material.h`  
   <https://github.com/assimp/assimp/blob/master/include/assimp/material.h>

这些资料分别用于：

1. 经典材质 key 列表
2. texture stack / texture semantic 说明
3. 当前 public header 中的 PBR key 与扩展 texture type
