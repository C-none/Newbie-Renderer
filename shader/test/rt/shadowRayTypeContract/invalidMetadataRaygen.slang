module invalidMetadataRaygen;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.resources;
import renderer.pathTracing.visibility;
import test.rt.shadowRayTypeContract.abi;

bool invalidMetadataContractFixtureValid()
{
    RtInstanceMetadata instance = rtInstanceMetadata[DispatchRaysIndex().y];
    if (instance.geometryCount != kInvalidMetadataContractValidGeometryCount || instance.vertexStride != 72u)
    {
        return false;
    }

    RtGeometryMetadata geometry = rtGeometryMetadata[instance.geometryOffset];
    if (geometry.geometryIndex != 0u || geometry.materialIndex != 0u || geometry.primitiveOffset != 0u ||
        geometry.primitiveCount != 1u || uint(geometry.flags & RtGeometryFlag.indexed) == 0u)
    {
        return false;
    }

    RtMaterialHeader material = rtMaterialHeaders[geometry.materialIndex];
    if (!hasRtMaterialFeature(material, RtMaterialFeatureFlag.alphaMask) ||
        material.alphaMode != AlphaMode.mask || material.alphaCutoff <= 0.0f ||
        material.baseColorFactor.a >= material.alphaCutoff)
    {
        return false;
    }

    RtMaterialTextureRef textureRef =
        rtMaterialTextureRefs[material.textureRefOffset + uint(MaterialTextureSlot.baseColor)];
    return material.textureRefCount >= uint(MaterialTextureSlot.count) && textureRef.textureId == 0u &&
           textureRef.uvSet == 0u;
}

void traceInvalidMetadataMaterialRay(float originZ)
{
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, originZ);
    ray.Direction = float3(0.0f, 0.0f, -1.0f);
    ray.TMin = kRayEpsilon;
    ray.TMax = kInvalidMetadataContractRayDistance - kRayEpsilon;
    MaterialRayPayload payload = MaterialRayPayload();
    TraceRay(scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, uint(RtRayType.material), uint(RtRayType.count),
             uint(RtRayType.material), ray, payload);
}

[shader("raygeneration")]
void rgInvalidMetadataContract()
{
    gShadowRayTypeResults[kInvalidMetadataContractPrimaryMaterialResult] = kShadowRayTypeContractPending;
    gShadowRayTypeResults[kInvalidMetadataContractPrimaryShadowResult] = kShadowRayTypeContractPending;
    gShadowRayTypeResults[kInvalidMetadataContractControlMaterialResult] = kShadowRayTypeContractPending;
    gShadowRayTypeResults[kInvalidMetadataContractControlShadowResult] = kShadowRayTypeContractPending;
    if (!invalidMetadataContractFixtureValid())
    {
        gShadowRayTypeResults[kInvalidMetadataContractPrimaryMaterialResult] = kShadowRayTypeContractFixtureFailure;
        return;
    }

    traceInvalidMetadataMaterialRay(kInvalidMetadataContractPrimaryRayOriginZ);
    bool primaryVisible = tracePathVisibilityRay(
        float3(0.0f, 0.0f, kInvalidMetadataContractPrimaryRayOriginZ), float3(0.0f, 0.0f, -1.0f),
        kInvalidMetadataContractRayDistance);
    gShadowRayTypeResults[kInvalidMetadataContractPrimaryShadowResult] = uint(!primaryVisible);

    traceInvalidMetadataMaterialRay(kInvalidMetadataContractControlRayOriginZ);
    bool controlVisible = tracePathVisibilityRay(
        float3(0.0f, 0.0f, kInvalidMetadataContractControlRayOriginZ), float3(0.0f, 0.0f, -1.0f),
        kInvalidMetadataContractRayDistance);
    gShadowRayTypeResults[kInvalidMetadataContractControlShadowResult] = uint(!controlVisible);
}
