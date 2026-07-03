module hitShaders;
import common;
import include.rt.resources;
import include.rt.hitSurface;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;

public bool shouldIgnorePathTracingAlphaMaskedHit(
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 viewDirection,
    float hitT,
    uint materialIndex)
{
    if (!hasRtMaterialFeature(material, kRtMaterialFeatureAlphaMask))
    {
        return false;
    }

    ResolvedMaterialPayload resolved = resolveRtMaterialPayload(
        rtMaterialTextureRefs,
        rtMaterialLayers,
        material,
        surface,
        viewDirection,
        hitT,
        materialIndex);
    return resolvedMaterialFailsAlphaMask(resolved);
}

public void handlePathTracingAnyHit(
    inout PathTracingRayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    if (!hit.geometryValid)
    {
        return;
    }

    RtMaterialHeader material = rtMaterialHeaders[hit.geometry.materialIndex];
    orientRtSurfaceForMaterial(hit.surface, material, viewDirection);
    if (shouldIgnorePathTracingAlphaMaskedHit(
        material,
        hit.surface,
        viewDirection,
        RayTCurrent(),
        hit.geometry.materialIndex))
    {
        IgnoreHit();
    }
}

public void handlePathTracingClosestHit(
    inout PathTracingRayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.rayKind != kPathTracingRayKindMaterial)
    {
        return;
    }

    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    if (!hit.geometryValid)
    {
        payload.hit = 0u;
        payload.material = defaultResolvedMaterialPayload();
        return;
    }

    RtMaterialHeader material = rtMaterialHeaders[hit.geometry.materialIndex];
    orientRtSurfaceForMaterial(hit.surface, material, viewDirection);
    payload.hit = 1u;
    payload.material = resolveRtMaterialPayload(
        rtMaterialTextureRefs,
        rtMaterialLayers,
        material,
        hit.surface,
        viewDirection,
        RayTCurrent(),
        hit.geometry.materialIndex);
}
