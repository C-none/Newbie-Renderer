module anyHitPolicy;
import common;
import include.rt.hitSurface;

public bool shouldIgnoreSingleSidedBackFace(RtMaterialHeader material, bool frontFace)
{
    return !frontFace && !hasRtMaterialFeature(material, RtMaterialFeatureFlag.doubleSided) &&
           !hasRtMaterialFeature(material, RtMaterialFeatureFlag.volumeBoundary);
}

public void handleAnyHitPolicy(BuiltInTriangleIntersectionAttributes attributes)
{
    RtInstanceMetadata instance = currentRtInstance();
    bool geometryValid = false;
    RtGeometryMetadata geometry = currentRtGeometry(instance, geometryValid);
    if (!geometryValid)
    {
        // Accept the candidate: ignoring a broken TLAS/sideband association can leak light.
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

    // Alpha coverage only: base color factor x base color texture x vertex color. No normal,
    // metallic-roughness, emissive or layer records are resolved for the any-hit test.
    if (resolveAlphaCoverage(rtMaterialTextureRefs, material, hit.surface) < material.alphaCutoff)
    {
        IgnoreHit();
    }
}
