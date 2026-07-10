module hitShaders;
import common;
import include.rt.hitSurface;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;

public bool shouldIgnoreAlphaMaskedHit(
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

    // Alpha coverage only: base color factor x base color texture x vertex color. No normal,
    // metallic-roughness, emissive or layer records are resolved for the any-hit test.
    float alpha = resolveAlphaCoverage(rtMaterialTextureRefs, material, surface);
    return alpha < material.alphaCutoff;
}

public void handleAnyHit(
    inout RayPayload payload,
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
    if (shouldIgnoreAlphaMaskedHit(
        material,
        hit.surface,
        viewDirection,
        RayTCurrent(),
        hit.geometry.materialIndex))
    {
        IgnoreHit();
    }
}

public ClosestHitInput makeClosestHitInput(
    BuiltInTriangleIntersectionAttributes attributes)
{
    float3 viewDirection = normalize(-WorldRayDirection());
    ClosestHitInput input = {};
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
