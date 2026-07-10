# Path Tracing Material Layer Variant Implementation Plan

## 1. 文档状态与目标

本文档定义 path tracing RT 材质层、CHS variant、unlit 和纹理采样路径的实施计划。它是实施顺序和验收条件，不是当前架构说明；当前代码仍以 [`RtMaterialLayerKind`](../shader/include/share/rtMaterial.slang)、混合职责的 `RtMaterialFeatureFlag` 和 sparse texture refs 为基础。

本计划采用以下已经确定的设计：

- 将 `RtMaterialLayerKind` 替换为 `[Flags] RtMaterialLayerFlag`。
- `RtMaterialLayerFlag.none == 0` 唯一表示 unlit。
- `baseSurface`、`clearcoat`、`sheen`、`transmission` 分别占用 bit 0 到 bit 3。
- `RtMaterialHeader.layerFlags` 是生成 CHS variant 的唯一材质层字段。
- `RtMaterialFeatureFlag` 删除 clearcoat、sheen、transmission 等 layer 信息，只保留 alpha、double-sided、emissive 和诊断类非 layer 特征。
- 不使用 texture presence mask 生成 variant。
- 一个 layer 一旦被 `layerFlags` 启用，该 layer 所需的纹理采样指令必须执行；没有 authored texture 时，C++ 写入 texture ID 0，由 `gSceneTextures[0]` 提供默认纹理。
- Slang 中所谓“static-if”采用普通 `if` 加 `let LayerFlags` 泛型值参数实现。Slang 在链接特化后常量折叠分支；不引入不存在的 C++ 风格 `if constexpr` 语法。

glTF unlit 的规范语义以 [KHR_materials_unlit](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_unlit/README.md) 为准。Slang 特化行为以 vendored [Generics](../src/extern/slang/docs/language-reference/generics.md) 和 [Link-time Specialization](../src/extern/slang/docs/user-guide/10-link-time-specialization.md) 文档为准，不在本文重复编译器原理。

## 2. 术语归一化

需求描述中“根据 `RtMaterialFeatureFlag` 判断 sheen 后采样 sheen texture”与“从 `RtMaterialFeatureFlag` 删除 layer 信息”不能同时成立。实施时采用以下无歧义规则：

- 所有 baseSurface、clearcoat、sheen、transmission 的代码和纹理采样静态条件都读取 `RtMaterialLayerFlag`。
- `RtMaterialFeatureFlag` 不再出现 clearcoat、sheen、transmission 枚举值，也不得再次承担 layer variant 职责。
- texture ID 是否为 0 不控制是否采样。它只选择真实纹理或 renderer-owned 默认纹理。
- alpha、double-sided 等非 layer 行为继续读取 `RtMaterialFeatureFlag`。

后续命名统一使用 `LayerFlags`、`layerFlags`、`hasRtMaterialLayer(...)`，不再使用含义不清的 `BsdfFeatureMask` 或 `materialFeatureMask` 表示 layer 组合。

## 3. 目标数据模型与不变量

### 3.1 Shared shader ABI

在 [`shader/include/share/rtMaterial.slang`](../shader/include/share/rtMaterial.slang) 中定义：

```slang
[Flags]
public enum RtMaterialLayerFlag : uint
{
    none         = 0u,
    baseSurface  = 1u << 0u,
    clearcoat    = 1u << 1u,
    sheen        = 1u << 2u,
    transmission = 1u << 3u,
}

[Flags]
public enum RtMaterialFeatureFlag : uint
{
    none                  = 0u,
    alphaMask             = 1u << 0u,
    alphaBlend            = 1u << 1u,
    doubleSided           = 1u << 2u,
    emissive              = 1u << 3u,
    unsupportedAnisotropy = 1u << 31u,
}
```

`RtMaterialHeader` 新增独立字段：

```slang
public RtMaterialLayerFlag layerFlags = RtMaterialLayerFlag.none;
public RtMaterialFeatureFlag featureFlags = RtMaterialFeatureFlag.none;
```

`RtMaterialLayerRecord` 的 discriminator 改为同一种 flag 类型：

```slang
public RtMaterialLayerFlag layer = RtMaterialLayerFlag.baseSurface;
```

每个 record 的 `layer` 必须恰好包含一个 bit；combined mask 只允许出现在 header、variant key、泛型参数和 resolved payload 中。

### 3.2 有效 layer 组合

只允许以下 9 种语义组合：

- `none`：unlit。
- `baseSurface` 加 clearcoat、sheen、transmission 的任意组合：8 种 glTF lit variants。

任何非零 mask 都必须包含 `baseSurface`：

```text
LayerFlags == none                              -> valid unlit
LayerFlags contains baseSurface                 -> valid lit
LayerFlags != none and lacks baseSurface        -> invalid
LayerFlags contains bits outside known mask     -> invalid
```

CPU 编译阶段负责规范化并 fail fast；shader 中保留轻量断言/invalid-result 防线，但不得把无效 mask 静默解释为 unlit。

### 3.3 unlit 数据语义

unlit 不创建 PBR layer records，`layerCount == 0`。它仍保留：

- baseColor factor
- baseColor texture reference
- vertex color
- alpha mode / alpha cutoff
- double-sided policy

unlit 忽略 metallic、roughness、normal、occlusion、emissive 和所有 PBR extension layer。path tracer 将最终 baseColor RGB 作为 terminal surface radiance，跳过 direct lighting 和后续 scatter。

### 3.4 Canonical layer record 顺序

lit 材质的 compact records 必须按 bit 顺序写入：

```text
baseSurface -> clearcoat -> sheen -> transmission
```

shader 解析时使用一个递增 `layerRecordIndex`，每个 `if (LayerFlags & flag)` block 内读取并递增。由于 `LayerFlags` 是 `let` 泛型参数，链接特化后 inactive block 和对应 buffer read 会被删除。该方案替代当前 [`tryFindMaterialLayer(...)`](../shader/include/material/sampling.slang) 的运行时线性搜索。

## 4. Texture ABI 与默认纹理合同

### 4.1 不生成 texture variants

以下数据不得进入 `RtBsdfVariantKey`、CHS hash 或 pipeline/SBT cache key：

- 单个 texture slot 是否 authored
- `MaterialTextureSlotFlag`
- texture ID
- UV set

variant 只由 combined `RtMaterialLayerFlag` 生成；alpha mask 继续作为独立 hit-group policy。

### 4.2 Dense texture refs

[`compileRtMaterial(...)`](../src/scene/nrSceneRtMaterial.cpp) 应为每个 RT 材质写入固定 `MaterialTextureSlot.count` 个 `RtMaterialTextureRef`，顺序与 [`MaterialTextureSlot`](../shader/include/share/materialTextureIds.slang) 一致：

```text
textureRefs[material.textureRefOffset + uint(slot)]
```

规则如下：

- authored 且 resident 的纹理写入实际 descriptor ID。
- 未 authored 的 slot 写入 texture ID 0。
- 所有 slot 都有 record，因此 shader 不再扫描 `textureRefCount` 查找 slot。
- `RtMaterialTextureRef.slot` 和 `textureRefCount` 如果清理后无 ABI/诊断用途，应删除；如果暂时保留，必须验证它们与 dense layout 一致。
- `RtMaterialLayerRecord.textureMask`、`RtMaterialTextureRef.flags`、`layerTextureMask(...)` 和 `rtMaterialTextureMask(...)` 在无剩余 consumer 后删除。
- [`MaterialTextureSlotFlag`](../shader/include/share/materialTextureIds.slang) 若全仓库无其他 consumer，也一并删除，避免留下暗示 texture variant 的废弃 ABI。

### 4.3 ID 0 必须从紫色调试图改为中性默认纹理

当前文档把 texture ID 0 定义为 purple fallback，见 [`shader/SHADER_NAMING_AND_ORGANIZATION.md`](../shader/SHADER_NAMING_AND_ORGANIZATION.md) 和 [`src/renderPasses/README.md`](../src/renderPasses/README.md)。这与“缺失 texture 仍无条件采样”不兼容。

实施后 `gSceneTextures[0]` 的合同改为 1x1 linear white RGBA `(1, 1, 1, 1)`：

- baseColor、metallic-roughness、emissive、clearcoat、sheen、transmission 的乘法采样得到中性值 1。
- optional texture absence 不再表现为紫色错误材质。
- 真正的无效资源引用仍应在 load/scene residency 边界通过 `nrInfo`/`nrAssert` 报告，而不是借 ID 0 兼任诊断纹理。

normal map 需要额外遵守：

- base normal slot 未 authored 时，C++ 将写入 ID 0，并把 header 中 effective normal scale 写为 0。
- clearcoat normal slot 未 authored时，C++ 将对应 layer record 的 effective normal scale 写为 0。
- shader 仍执行 ID 0 的 white texture sampling；normal decode 后将 XY 乘以 0，结果回到 tangent-space `(0, 0, 1)`。
- authored normal texture 保留真实 scale；不得通过 `textureId == 0` 跳过 `SampleLevel`。

这样可以同时满足“始终采样”和“缺失 normal 不改变几何法线”，且不增加 texture presence 分支。

### 4.4 按 layer 静态决定采样组

目标采样结构：

```slang
if (LayerFlags == RtMaterialLayerFlag.none)
{
    // Statically retained only for unlit CHS.
    sample baseColor;
}
else
{
    // Every lit variant contains baseSurface.
    sample baseColor;
    sample metallicRoughness;
    sample emissive;
    sample normal;

    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        sample clearcoat;
        sample clearcoatRoughness;
        sample clearcoatNormal;
    }

    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        sample sheenColor;
        sample sheenRoughness;
    }

    if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
    {
        sample transmission;
    }
}
```

这里每个条件只依赖 `let LayerFlags`，因此是链接特化条件。block 内不得先调用采样函数再用 `select` 丢弃结果，否则无法保证资源访问被裁剪。

## 5. Variant 与 SBT 目标架构

### 5.1 Variant key

在 [`src/renderPasses/nrRtHitSbtPlan.ixx`](../src/renderPasses/nrRtHitSbtPlan.ixx) 中将 key 收敛为：

```cpp
struct RtBsdfVariantKey
{
    RtMaterialLayerFlag layerFlags = RtMaterialLayerFlag::none;

    [[nodiscard]] friend auto operator<=>(
        const RtBsdfVariantKey&,
        const RtBsdfVariantKey&) noexcept = default;
};
```

变更要求：

- 删除 `RtHitShadingModel` 字段；unlit 由 `layerFlags == none` 编码。
- 删除 `rtBsdfVariantMaterialFeatureMask(...)`。
- `makeRtBsdfVariantKey(...)` 直接接收规范化后的 `RtMaterialLayerFlag`。
- `makeRtHitGroupKey(...)` 接收 `layerFlags` 和非 layer `featureFlags`；只用后者决定 alpha policy。
- hash domain salt 从 `RtBsdfVariantKey.v1` 升级，防止旧缓存键与新语义碰撞。
- CHS hard upper bound 为 9，alpha policy 后的 hit-group hard upper bound 为 18。
- `RtHitShadingModel` 若无其他 consumer，从 [`rtHitPolicy.slang`](../shader/include/share/rtHitPolicy.slang) 和 generated C++ ABI 中删除。

### 5.2 AS build 到 SBT plan

当前 AS build 只把 `header.featureFlags` 传给 SBT planner。目标流程为：

```text
RtMaterialHeader.layerFlags + RtMaterialHeader.featureFlags
    -> makeRtHitPermutationKey(layerFlags, featureFlags)
    -> per-geometry RtHitPermutationKey
    -> appendRtHitSbtPlanInstance(...)
```

[`AccelerationStructureBuildNode`](../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp) 应构造 per-geometry `RtHitPermutationKey` 列表并传给 planner，避免再增加一个仅用于搬运两组 flags 的中间 policy struct。

SBT record 仍然按 geometry 指向 deduplicated permutation；alphaMask/opaqueLike hit groups 继续共享相同 `RtBsdfVariantKey` 对应的 CHS program。

### 5.3 CHS link variant

[`makePathTracingChsVariantDesc(...)`](../src/renderPasses/PathTracing/nrPathTracingNode.cpp) 统一生成：

```slang
export struct CHS : ICHS = MaterialCHS<RtMaterialLayerFlag(N)>;
```

不需要 C++ 根据 0/nonzero 拼接 `UnlitCHS` 或 `DefaultLitCHS` 两种类型。单一泛型类型减少 C++ variant 生成分支，unlit/lit 分流由 Slang 链接特化完成。

## 6. Shader 实施计划

### 6.1 CHS 入口

在 [`shader/include/pathTracing/chs.slang`](../shader/include/pathTracing/chs.slang) 中：

```slang
public struct MaterialCHS<let LayerFlags : RtMaterialLayerFlag> : ICHS
{
    public ClosestHitResult handleClosestHit(ClosestHitInput input)
    {
        if (LayerFlags == RtMaterialLayerFlag.none)
        {
            return handleUnlitClosestHit<LayerFlags>(input);
        }

        return handleLitClosestHit<LayerFlags>(input);
    }
}
```

普通 `if` 即本计划中的 static-if。`LayerFlags` 必须沿调用链保持为 `let` generic argument，不能在 helper 边界退化成普通 `uint` 参数。

同时把当前混合验证常量拆成 `kKnownRtMaterialLayerMask` 和 `kKnownRtMaterialFeatureMask`。前者验证 4 个 layer bits 及“非零必须含 baseSurface”不变量；后者只验证非 layer feature bits。删除以 `RtHitShadingModel.gltfPbr` 为前提的 `hitPolicySupported(...)` 参数和分支。

### 6.2 Payload resolver

在 [`shader/include/material/payload.slang`](../shader/include/material/payload.slang) 中建立以下窄函数：

- `resolveBaseColorAndAlpha(...)`
- `resolveUnlitMaterialPayload(...)`
- `resolveLitMaterialPayloadVariant<let LayerFlags>(...)`
- `resolveAlphaCoverage(...)`

职责：

- unlit resolver 只访问 baseColor texture、factor、vertex color 和 alpha 状态。
- lit resolver 总是解析 baseSurface，并在三个 optional layer static blocks 中解析对应 record 和纹理。
- resolved payload 保存 `RtMaterialLayerFlag layerFlags`，供 raygen 的公共阶段识别 unlit 和 dispatch direct evaluator。
- 旧的无泛型 `resolveRtMaterialPayload(...)` 在所有 consumer 迁移后删除，避免新代码误用全量解析路径。

### 6.3 BSDF lobe 计算

以下函数统一将泛型参数重命名为 `LayerFlags`：

- `evaluateResolvedMaterialBsdfVariant`
- `resolvedMaterialCombinedPdfVariant`
- `sampleResolvedMaterialScatterVariant`

每个 optional lobe 必须把以下全部操作放在同一个 static block 内：

- selection weight
- total weight 累加
- threshold 累加
- `sample(...)`
- `evaluate(...)`
- `pdf(...)`

禁止只把 inactive lobe weight 置 0、却在 block 外继续无条件调用 `sample/pdf/evaluate`。这项要求用于保证生成的 CHS SPIR-V 不包含 inactive lobe 的可执行路径。

baseSurface 对应 diffuse + GGX specular；只有 lit variants 执行。unlit 不实现假的 BSDF、PDF 或 scatter。

当前 [`TransmissionBsdfLobe`](../shader/include/material/payload.slang) 仍是返回零的 stub。此次重构应先完成正确的静态结构和采样数据流；真实 transmission 数学实现作为独立功能验收，不得用 stub 的常量零优化结果冒充 variant 裁剪验证。

### 6.4 Alpha any-hit

[`shouldIgnoreAlphaMaskedHit(...)`](../shader/renderer/pathTracing/hitShaders.slang) 改为调用 `resolveAlphaCoverage(...)`，只执行：

```text
baseColor factor × baseColor texture × vertex color -> alpha
```

不得解析 normal、metallic-roughness、emissive 或 layer records。alpha any-hit 是否安装仍由 `RtHitAlphaPolicy` 决定，因此该 helper 不需要 layer variant；lit 和 unlit 共享相同 glTF alpha coverage 规则。

### 6.5 Raygen hit handling 与 direct lighting 边界

当前 direct-light sample 在 raygen 中生成，发生在 CHS 返回之后，见 [`core.slang`](../shader/renderer/pathTracing/core.slang)。因此 CHS 的链接期 `LayerFlags` 无法直接跨 stage 成为 raygen 的编译期常量。

本阶段采用最小复杂度方案：

- `handleHit(...)` 首先检查 resolved `layerFlags == none`。
- unlit：累加 `baseColor.rgb` 作为 terminal radiance，应用既有 alpha 行为，然后终止 path；不采 direct light，不执行 scatter。
- lit：继续 direct lighting 和 scatter。
- direct BSDF 使用一个 runtime `switch(payload.layerFlags)` dispatch 到 `evaluateResolvedMaterialDirectVariant<ConcreteFlags>`。
- 每个 switch case 内部是静态特化 evaluator，因此单次 invocation 不执行 inactive lobe 数学；root raygen SPIR-V 会包含所有有效 cases，这是当前单 raygen 架构的明确边界。

本阶段不引入 callable shader，也不把 visibility ray 移入 CHS。若 profiling 证明 runtime switch 成为瓶颈，再单独评估 callable shader 或 wavefront material queues；不得在此次 layer flag 重构中扩大范围。

## 7. C++ 数据流实施计划

### 7.1 Load

在 [`MaterialAsset`](../src/load/nrLoadType.ixx) 中增加简单 `bool unlit = false`，不增加 shading-model wrapper struct。

[`nrLoadAssimp.cpp`](../src/load/nrLoadAssimp.cpp) 读取 `AI_MATKEY_SHADING_MODEL` 对应的 `"$mat.shadingm"` property；当前项目使用的 Assimp API 提供 `aiShadingMode_Unlit`（`aiShadingMode_NoShading` alias）。扩展现有 [`dependency.assets`](../src/extern/dependencyAssets.ixx) 边界，导出窄的 `aiShadingMode` 类型和项目命名的 unlit 常量；不把 `AI_MATKEY_*` 宏传播到 load，也不在 load 模块直接 include Assimp headers。

glTF unlit 与其他 shading-model extension 同时出现属于规范未定义组合。loader 可以保留原始 PBR fallback 数据，但必须设置 `unlit=true`；RT 编译阶段让 `layerFlags=none` 获得确定性优先级，并通过项目统一错误设施记录一次 warning。

### 7.2 Resource 与 scene bridge

在 [`nr::resource::Material`](../src/resource/nrResourceMaterial.ixx) 中增加 `bool unlit = false`。scene bridge 从 `MaterialAsset` 复制该字段，不新增多态材质层级。

resource 自身的 `MaterialFeatureFlag` 可继续服务 CPU 查询，但 RT 编译不得再从一个混合 mask 同时推导 layer variant 和 alpha policy。RT 边界明确拆成：

```cpp
RtMaterialLayerFlag rtLayerFlags(const Material& material);
RtMaterialFeatureFlag rtFeatureFlags(const Material& material);
```

`rtLayerFlags(...)`：

- `material.unlit` -> `none`
- otherwise -> `baseSurface`
- optional clearcoat/sheen/transmission block 或相关 authored texture -> 加对应 layer bit

`rtFeatureFlags(...)`：

- alphaMask / alphaBlend
- doubleSided
- lit-only emissive
- unsupportedAnisotropy diagnostic

unlit 不设置 RT emissive bit，因为规范要求忽略 emissive。

### 7.3 RT material compilation

[`compileRtMaterial(...)`](../src/scene/nrSceneRtMaterial.cpp) 按以下顺序执行：

1. 计算并验证 `layerFlags`。
2. 计算非 layer `featureFlags`。
3. 写 header factors 和两个 flags 字段。
4. 若 lit，按 canonical bit order 写 compact layer records；若 unlit，不写 layer record。
5. 写固定数量 dense texture refs，所有缺失项使用 ID 0。
6. 根据 authored normal slot 计算 effective normal scale，缺失时写 0。
7. 写回 `layerCount` 和固定 `textureRefCount`，或在 ABI 清理后删除不再需要的 count。

fallback RT material 必须是 lit：`layerFlags = baseSurface`，不得因为默认构造的 flags 为 0 而意外成为 unlit。

## 8. 文件级变更清单

### Shared ABI 与 codegen

- [`shader/include/share/rtMaterial.slang`](../shader/include/share/rtMaterial.slang)：新 enum、header 字段、record discriminator、移除 layer feature bits 和 texture masks。
- [`shader/include/share/materialTextureIds.slang`](../shader/include/share/materialTextureIds.slang)：在无 consumer 后删除 `MaterialTextureSlotFlag`。
- [`src/extern/tools/nrShaderShareCodegen.cpp`](../src/extern/tools/nrShaderShareCodegen.cpp)：验证 `[Flags]` enum 的 C++ 翻译和 `slangEnumLiteral` 仍能生成 combined mask literal；只在生成器确有缺口时修改。
- [`shader/include/share/rtHitPolicy.slang`](../shader/include/share/rtHitPolicy.slang)：删除不再使用的 `RtHitShadingModel`，保留 alpha policy。

### Load / resource / scene

- [`src/extern/dependencyAssets.ixx`](../src/extern/dependencyAssets.ixx)：窄导出 Assimp shading-mode 类型和 unlit 常量。
- [`src/load/nrLoadType.ixx`](../src/load/nrLoadType.ixx)：`MaterialAsset::unlit`。
- [`src/load/nrLoadAssimp.cpp`](../src/load/nrLoadAssimp.cpp)：读取 Assimp shading mode。
- [`src/resource/nrResourceMaterial.ixx`](../src/resource/nrResourceMaterial.ixx)：`Material::unlit`。
- [`src/scene/nrScene.cpp`](../src/scene/nrScene.cpp)：load-to-resource 字段桥接和 slot 0 normal-scale 规范化所需数据。
- [`src/scene/nrSceneRtMaterial.ixx`](../src/scene/nrSceneRtMaterial.ixx)：导出新 flag 类型，删除 texture-mask helper API。
- [`src/scene/nrSceneRtMaterial.cpp`](../src/scene/nrSceneRtMaterial.cpp)：拆分两组 flags、canonical records、dense refs、unlit 和默认 normal scale。

### Variant / pipeline / SBT

- [`src/renderPasses/nrRtHitSbtPlan.ixx`](../src/renderPasses/nrRtHitSbtPlan.ixx)：key、hash、upper bound 和 planner 输入。
- [`src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp`](../src/renderPasses/AccelerationStructureBuild/nrAccelerationStructureBuildNode.cpp)：按 geometry 传递 layer flags + feature flags 生成的 permutation key。
- [`src/renderPasses/PathTracing/nrPathTracingNode.cpp`](../src/renderPasses/PathTracing/nrPathTracingNode.cpp)：生成 `MaterialCHS<RtMaterialLayerFlag(N)>` link variant。

### Shader runtime

- [`shader/include/rtMaterial.slang`](../shader/include/rtMaterial.slang)：dense texture ref 访问、layer helper、删除 texture-ID-zero early return 依赖，并让 preview/debug 逻辑改读 `layerFlags`。
- [`shader/include/material/sampling.slang`](../shader/include/material/sampling.slang)：always-sample helper、canonical layer record 解析。
- [`shader/include/material/types.slang`](../shader/include/material/types.slang)：补足 transmission/unlit 所需 resolved 数据，但避免复制 variant key 结构。
- [`shader/include/material/payload.slang`](../shader/include/material/payload.slang)：resolver 和 BSDF/PDF/sample 静态 blocks。
- [`shader/include/pathTracing/chs.slang`](../shader/include/pathTracing/chs.slang)：单一 `MaterialCHS<LayerFlags>`。
- [`shader/renderer/pathTracing/hitShaders.slang`](../shader/renderer/pathTracing/hitShaders.slang)：最小 alpha resolver。
- [`shader/renderer/pathTracing/core.slang`](../shader/renderer/pathTracing/core.slang)：unlit terminal handling 和 direct evaluator dispatch。

### Default texture contract 与文档同步

- renderer scene texture fallback 创建路径：将 ID 0 内容改为 neutral white。
- [`src/renderPasses/nrSceneTextureTableBinding.ixx`](../src/renderPasses/nrSceneTextureTableBinding.ixx)：继续强制 ID 0 descriptor 存在。
- [`shader/SHADER_NAMING_AND_ORGANIZATION.md`](../shader/SHADER_NAMING_AND_ORGANIZATION.md)、[`src/renderPasses/README.md`](../src/renderPasses/README.md) 和 [`docs/architecture/README.md`](architecture/README.md)：实施代码时同步更新 purple fallback、RT material flags 和 CHS variant 流程。本文档本身不替代这些当前架构文档。

## 9. 实施顺序

### Phase 1: ABI 与 CPU 分类

1. 引入 `RtMaterialLayerFlag` 和 header `layerFlags`。
2. 清理 `RtMaterialFeatureFlag` layer bits。
3. 让 shader-share codegen 生成新的 C++ enum。
4. 增加 load/resource `unlit` bool 并完成 Assimp bridge。
5. 拆分 `rtLayerFlags` / `rtFeatureFlags`，建立 9 种有效组合验证。

完成条件：C++ 能从普通 PBR、每种 optional layer 组合和 unlit 输入产生确定的 header flags。

### Phase 2: Dense texture/default contract

1. ID 0 fallback 改为 white。
2. RT material compiler 写 dense refs，缺失 slot 写 ID 0。
3. normal 缺失时写 effective scale 0。
4. shader accessor 改为直接索引并总是执行采样。
5. 删除不再使用的 texture mask 数据。

完成条件：没有 authored texture 的 lit 材质仍产生预期 factor-only 外观；无 normal texture 时 shading normal 等于几何/插值 normal。

### Phase 3: Variant/SBT

1. `RtBsdfVariantKey` 只存 `layerFlags`。
2. AS build 生成 per-geometry permutation keys。
3. 更新 hash domain 和 9/18 upper bounds。
4. CHS link alias 改为 `MaterialCHS<LayerFlags>`。

完成条件：同 layer mask、不同 texture presence 的材质共享 CHS；unlit 获得 mask 0 CHS。

### Phase 4: Static shader data flow

1. resolver 泛型化。
2. optional layer sampling 放入 static blocks。
3. BSDF evaluate/pdf/sample 重写为完整 lobe static blocks。
4. unlit terminal path。
5. minimal alpha any-hit resolver。
6. raygen direct evaluator runtime dispatch 到 concrete generic cases。

完成条件：inactive layer 不产生 CHS buffer reads、texture samples 或 lobe math；unlit 不产生 PBR work。

### Phase 5: 清理、文档和验证

1. 删除旧 enum/helper/unused mask 字段。
2. 更新架构和 shader ABI 文档。
3. 更新/新增 tests 和 SPIR-V 检查。
4. 运行 shaderlint/shaderman 验证，再运行 LLVM Debug C++ tests；不得用 Release 代替 Debug 验证。

## 10. 验证矩阵

### 10.1 CPU contract tests

扩展 [`nr_scene_rt_material_contract_test.cpp`](../test/integration/scene/nr_scene_rt_material_contract_test.cpp)：

- unlit -> `layerFlags == none`、`layerCount == 0`
- plain metallic-roughness -> `baseSurface`
- base + each single optional layer
- base + all three optional layers
- invalid nonzero mask without baseSurface 被拒绝
- fallback material -> baseSurface，不是 unlit
- dense texture ref count 和 slot 顺序固定
- absent textures -> ID 0
- absent base/clearcoat normal -> effective scale 0
- layer records 使用 canonical order，record discriminator 为单 bit
- `RtMaterialFeatureFlag` 不再包含 layer bits

扩展 SBT/AS tests：

- 同 layerFlags、不同 texture IDs 共享 permutation。
- unlit 和 baseSurface 使用不同 CHS key。
- alpha mask 只改变 hit-group policy，不复制 CHS program。
- 9 个 CHS / 18 个 hit-group 上界成立。

### 10.2 Import tests

使用仓库已有 [`UnlitTest.gltf`](../assets/glTF-Sample-Assets/Models/UnlitTest/glTF/UnlitTest.gltf)：

- Assimp shading mode 被转换为 `MaterialAsset::unlit`。
- scene resource 保留 `unlit`。
- RT header 生成 `layerFlags == none`。
- baseColor、vertex color、alpha 和 double-sided 数据仍然保留。

同时准备一个普通无 extension PBR fixture，证明其 mask 是 `baseSurface` 而不是 0。

### 10.3 Shader compilation and IR/SPIR-V checks

所有修改过的 Slang 文件必须通过项目要求的 shaderman/shaderlint 编译验证。增加最小 variant 检查，至少覆盖：

- `none`
- `baseSurface`
- `baseSurface | sheen`
- `baseSurface | clearcoat`
- `baseSurface | transmission`
- all layers

对生成的 CHS SPIR-V/反汇编执行语义检查：

- unlit variant 没有 metallic-roughness、normal、emissive 和 optional layer texture sample。
- base-only variant 没有 clearcoat/sheen/transmission texture sample 和 lobe calls。
- sheen variant 包含 sheen texture sample，即使材质运行时 texture ID 为 0。
- no-transmission variant 不包含 transmission `selectionWeight/sample/evaluate/pdf` 可执行路径。
- static layer conditions 不生成依赖 runtime `layerFlags` 的 CHS `OpBranchConditional`。

不要仅检查 `transmissionWeight == 0`；必须检查 transmission call path 是否消失。

### 10.4 Runtime smoke tests

扩展 [`rtObjectMaterialSmoke.cpp`](../test/smoke/app/rtObjectMaterialSmoke.cpp) 或新增等价 smoke coverage：

- unlit 在不同 direct-light 强度下颜色不变。
- unlit 不投射后续 BSDF bounce，但仍遵守 alpha mask 和 double-sided。
- factor-only PBR 材质在所有纹理 ID 为 0 时保持中性默认采样结果。
- 缺失 normal map 不改变 shading normal。
- 有 sheen layer 但无 authored sheen texture 时仍通过 ID 0 执行采样，并由 factor 控制结果。

## 11. 验收标准

实现完成必须同时满足：

- `RtMaterialLayerKind` 全仓库无引用。
- `RtMaterialFeatureFlag` 不含 baseSurface/clearcoat/sheen/transmission。
- CHS variant key 只包含 `RtMaterialLayerFlag`；texture presence 不参与 hash/cache/SBT permutation。
- `layerFlags == 0` 只表示 unlit；普通 PBR 始终设置 baseSurface。
- shader 不因 `textureId == 0` 跳过 active layer 的 texture sample。
- C++ 为缺失 slot 写 ID 0，renderer 确保 ID 0 neutral descriptor 始终绑定。
- optional layer 的 resolve/sample/evaluate/pdf 全部受 `let LayerFlags` 静态 block 控制。
- alpha any-hit 不再解析完整材质。
- unlit 不执行 direct lighting、PBR emissive 或 scatter。
- ABI/hash 版本、架构文档和 shader naming 文档同步更新。
- shaderlint/shaderman 和对应 Debug tests 全部通过。

## 12. 明确非目标

本轮重构不包含：

- 以 texture presence 生成 shader variants。
- callable shader 或 wavefront path tracer 改造。
- 完整 KHR_materials_transmission/volume/IOR 物理实现。
- 为 unlit 新增额外 shading-model struct、interface 层级或 C++ 多态。
- 用 purple fallback 表达 optional texture absence。

这些项目若后续需要，应建立独立设计和 profiling 依据，不能重新污染 `RtMaterialLayerFlag` 的单一 variant 职责。
