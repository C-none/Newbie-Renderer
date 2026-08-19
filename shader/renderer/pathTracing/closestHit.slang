module closestHit;
import common;
import renderer.pathTracing.pathState;
import include.rt.hitSurface;

[require(spvCooperativeVectorNV)]
[shader("closesthit")]
void chMain(inout MaterialRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    CHS chs = CHS();
    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    RtMaterialHeader material = {};
    if (hit.geometryValid)
    {
        material = rtMaterialHeaders[hit.geometry.materialIndex];
        orientRtSurfaceForMaterial(hit.surface, viewDirection);
    }

    // Copy every shared-lifetime input before the result overwrites the same physical payload slots.
    ClosestHitInput input = ClosestHitInput(
        hit.geometryValid, material, hit.surface, viewDirection, hit.geometryNormal, hit.frontFace,
        hit.geometry.materialIndex, uint(InstanceID()), uint(GeometryIndex()), payload.scatterRandomValues,
        payload.materialFilterSequence, payload.incidentMediumIor, payload.exteriorMediumIor);

    ClosestHitResult result = chs.handleClosestHit(input);
    if (result.hit != 0u)
    {
        writeResolvedMaterialRayPayload(payload, result.material, result.scatter);
    }
    else
    {
        payload.hit = false;
    }
}
