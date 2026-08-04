module raygen;
import common;
import renderer.pathTracing.visibility;
import test.rt.shadowRayTypeContract.abi;

public bool shadowRayTypeContractFixtureValid(uint lane)
{
    uint instanceIndex = DispatchRaysIndex().y;
    RtInstanceMetadata instance = rtInstanceMetadata[instanceIndex];
    if (instance.geometryCount != 4u || instance.vertexStride < 72u)
    {
        return false;
    }

    uint geometrySlot = min(lane > 0u ? lane - 1u : 0u, instance.geometryCount - 1u);
    RtGeometryMetadata geometry = rtGeometryMetadata[instance.geometryOffset + geometrySlot];
    if (geometry.geometryIndex != geometrySlot || geometry.primitiveCount == 0u ||
        uint(geometry.flags & RtGeometryFlag.indexed) == 0u)
    {
        return false;
    }

    RtMaterialHeader material = rtMaterialHeaders[geometry.materialIndex];
    if (material.textureRefCount < uint(MaterialTextureSlot.count))
    {
        return false;
    }

    RtMaterialTextureRef textureRef =
        rtMaterialTextureRefs[material.textureRefOffset + uint(MaterialTextureSlot.baseColor)];
    uint firstIndex = rtIndexData.Load((instance.indexBase + geometry.primitiveOffset) * 4u);
    uint vertexIndex = firstIndex + geometry.firstVertex;
    float3 vertexPosition =
        asfloat(rtVertexData.Load3((instance.vertexBase + vertexIndex) * instance.vertexStride));
    float4 textureSample = gSceneTextures[textureRef.textureId].SampleLevel(float2(0.5f), 0.0f);

    return textureRef.textureId == 0u && textureRef.uvSet <= 1u && all(isfinite(material.baseColorFactor)) &&
           all(isfinite(vertexPosition)) && all(isfinite(textureSample));
}

[shader("raygeneration")]
void rgShadowContract()
{
    uint lane = DispatchRaysIndex().x;
    if (lane >= kShadowRayTypeContractLaneCount)
    {
        return;
    }

    gShadowRayTypeResults[lane] = kShadowRayTypeContractPending;
    if (!shadowRayTypeContractFixtureValid(lane))
    {
        gShadowRayTypeResults[lane] = kShadowRayTypeContractFixtureFailure;
        return;
    }

    bool visible = tracePathVisibilityRay(shadowRayTypeContractOrigin(lane), shadowRayTypeContractDirection(),
                                          kShadowRayTypeContractRayDistance);
    uint routedResult = gShadowRayTypeResults[lane];
    if (routedResult == kShadowRayTypeContractMaterialMissPoison ||
        routedResult == kShadowRayTypeContractMaterialAnyHitPoison)
    {
        return;
    }
    gShadowRayTypeResults[lane] = uint(!visible);
}
