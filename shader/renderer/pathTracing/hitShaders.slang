module hitShaders;
import common;
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
    if (!hasRtMaterialFeature(material, RtMaterialFeatureFlag.alphaMask))
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

public PathTracingClosestHitInput makePathTracingClosestHitInput(
    BuiltInTriangleIntersectionAttributes attributes)
{
    float3 viewDirection = normalize(-WorldRayDirection());
    PathTracingClosestHitInput input = {};
    input.viewDirection = viewDirection;
    input.hitT = RayTCurrent();

    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    input.geometryValid = hit.geometryValid;
    input.materialIndex = hit.geometry.materialIndex;
    input.surface = hit.surface;
    if (!hit.geometryValid)
    {
        return input;
    }

    RtMaterialHeader material = rtMaterialHeaders[hit.geometry.materialIndex];
    orientRtSurfaceForMaterial(hit.surface, material, viewDirection);
    input.material = material;
    input.surface = hit.surface;
    return input;
}
