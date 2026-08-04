module anyHitPolicy;
import common;
import include.rt.hitSurface;

public bool shouldIgnoreAlphaMaskedHit(RtMaterialHeader material, RtSurfaceData surface)
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
    if (shouldIgnoreAlphaMaskedHit(material, hit.surface))
    {
        IgnoreHit();
    }
}
