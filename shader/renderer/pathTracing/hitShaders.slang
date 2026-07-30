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

public bool shouldIgnoreSingleSidedBackFace(
    RtMaterialHeader material,
    bool frontFace)
{
    return
        !frontFace &&
        !hasRtMaterialFeature(material, RtMaterialFeatureFlag.doubleSided) &&
        !hasRtMaterialFeature(material, RtMaterialFeatureFlag.volumeBoundary);
}

public void handleAnyHit(
    inout RayPayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    RtInstanceMetadata instance = currentRtInstance();
    bool geometryValid = false;
    RtGeometryMetadata geometry = currentRtGeometry(instance, geometryValid);
    if (!geometryValid)
    {
        return;
    }

    RtMaterialHeader material = rtMaterialHeaders[geometry.materialIndex];
    bool frontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
    if (shouldIgnoreSingleSidedBackFace(material, frontFace))
    {
        IgnoreHit();
        return;
    }
    if (!hasRtMaterialFeature(material, RtMaterialFeatureFlag.alphaMask))
    {
        return;
    }

    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = currentRtHitSurface(attributes, viewDirection);
    if (!hit.geometryValid)
    {
        return;
    }

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
    // InstanceID is the dense, collision-free RT metadata index written to Vulkan
    // InstanceCustomIndex. GeometryIndex is unique within that instance's BLAS.
    input.instanceMetadataIndex = uint(InstanceID());
    input.geometryIndex = uint(GeometryIndex());
    input.geometryNormal = hit.geometryNormal;
    input.frontFace = hit.frontFace;
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
