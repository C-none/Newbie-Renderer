module materialLobeSpecificNormalContract;
import common;

export static const bool kEnableFilterAfterShading = false;

ConstantBuffer<RtMaterialHeader> gMaterialHeader;
RWStructuredBuffer<uint> gInstantiationResults;

uint instantiateMaterialChsNormalContract<
    let LayerFlags : RtMaterialLayerFlag>(
    ClosestHitInput input)
{
    MaterialCHS<LayerFlags> chs;
    ClosestHitResult hit = chs.handleClosestHit(input);

    float3 viewDirection = safeNormalize(
        input.viewDirection,
        float3(0.0f, 1.0f, 0.0f));
    float3 facingGeometryNormal = materialPayloadFacingGeometryNormal(
        hit.material.geometryNormal,
        hit.material.shadingNormal,
        viewDirection);
    float3 specularNormal = adjustMaterialPayloadSpecularNormal(
        hit.material.shadingNormal,
        hit.material.geometryNormal,
        viewDirection);

    bool finite =
        all(isfinite(facingGeometryNormal)) &&
        all(isfinite(specularNormal)) &&
        all(isfinite(hit.scatter.weight));
    return
        uint(finite) |
        ((hit.scatter.valid ? 1u : 0u) << 1u);
}

uint instantiateFullLobeEvaluationContract<
    let LayerFlags : RtMaterialLayerFlag>(
    ClosestHitInput input)
{
    MaterialCHS<LayerFlags> chs;
    ClosestHitResult hit = chs.handleClosestHit(input);
    float3 viewDirection = safeNormalize(
        input.viewDirection,
        float3(0.0f, 1.0f, 0.0f));
    float3 lightDirection = safeNormalize(
        float3(0.75f, 0.5f, 0.25f),
        float3(0.0f, 1.0f, 0.0f));
    ResolvedMaterialBsdfComponents components =
        evaluateResolvedMaterialBsdfComponentsVariant<LayerFlags>(
            hit.material,
            viewDirection,
            lightDirection);
    MaterialDirectLightingInput lighting = {};
    lighting.viewDirection = viewDirection;
    lighting.lightDirection = lightDirection;
    lighting.lightRadiance = float3(1.0f);
    ResolvedMaterialDirectComponents direct =
        evaluateResolvedMaterialDirectComponentsVariant<LayerFlags>(
            hit.material,
            lighting);
    float combinedPdf = resolvedMaterialCombinedPdfVariant<LayerFlags>(
        hit.material,
        viewDirection,
        lightDirection);

    bool finite =
        all(isfinite(components.diffuse)) &&
        all(isfinite(components.specular)) &&
        all(isfinite(components.diffuseProjected)) &&
        all(isfinite(components.specularProjected)) &&
        all(isfinite(direct.diffuseRadiance)) &&
        all(isfinite(direct.specularRadiance)) &&
        isfinite(combinedPdf);
    return uint(finite);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ClosestHitInput input = {};
    input.geometryValid = gMaterialHeader.textureRefCount != 0xffffffffu;
    input.material = gMaterialHeader;
    input.surface.normal = float3(0.8660254f, 0.5f, 0.0f);
    input.viewDirection = float3(0.0f, 1.0f, 0.0f);
    input.geometryNormal = float3(0.0f, 1.0f, 0.0f);

    uint selector = dispatchThreadId.x & 15u;
    switch (selector)
    {
    case 0u: gInstantiationResults[0] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(1u)>(input); break;
    case 1u: gInstantiationResults[1] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(17u)>(input); break;
    case 2u: gInstantiationResults[2] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(3u)>(input); break;
    case 3u: gInstantiationResults[3] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(19u)>(input); break;
    case 4u: gInstantiationResults[4] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(5u)>(input); break;
    case 5u: gInstantiationResults[5] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(21u)>(input); break;
    case 6u: gInstantiationResults[6] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(7u)>(input); break;
    case 7u: gInstantiationResults[7] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(23u)>(input); break;
    case 8u: gInstantiationResults[8] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(9u)>(input); break;
    case 9u: gInstantiationResults[9] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(25u)>(input); break;
    case 10u: gInstantiationResults[10] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(11u)>(input); break;
    case 11u: gInstantiationResults[11] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(27u)>(input); break;
    case 12u: gInstantiationResults[12] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(13u)>(input); break;
    case 13u: gInstantiationResults[13] = instantiateMaterialChsNormalContract<RtMaterialLayerFlag(29u)>(input); break;
    case 14u:
        gInstantiationResults[14] =
            instantiateMaterialChsNormalContract<RtMaterialLayerFlag(15u)>(input) |
            (instantiateFullLobeEvaluationContract<RtMaterialLayerFlag(15u)>(input) << 2u);
        break;
    default:
        gInstantiationResults[15] =
            instantiateMaterialChsNormalContract<RtMaterialLayerFlag(31u)>(input) |
            (instantiateFullLobeEvaluationContract<RtMaterialLayerFlag(31u)>(input) << 2u);
        break;
    }
}
