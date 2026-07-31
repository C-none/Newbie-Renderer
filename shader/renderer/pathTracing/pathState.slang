module pathState;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.guides;

[Flags]
public enum PathFlag : uint
{
    none = 0u,
    active = 1u << 0u,
}

public static const uint kPathMediumStackCapacity = 4u;
public static const uint kInvalidPathMediumBoundaryIndex = 0xffffffffu;

public struct PathMediumBoundary
{
    public uint instanceMetadataIndex = kInvalidPathMediumBoundaryIndex;
    public uint geometryIndex = kInvalidPathMediumBoundaryIndex;
    public uint materialIndex = kRtMaterialFallbackIndex;
    public float ior = 1.0f;
};

// Full-precision, lossless transport representation. Metadata-only fields share one uint-backed
// flag enum; numerical fields stay float32. A future lossy packed variant belongs beside this type
// rather than changing the BSDF-facing ResolvedMaterialPayload working record.
public struct ResolvedMaterialLayerRayPayload
{
    public float clearcoat = 0.0f;
    public float clearcoatRoughness = 0.0f;
    public float3 clearcoatNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 sheenColor = float3(0.0f);
    public float sheenRoughness = 0.0f;
    public float transmission = 0.0f;
    public float transmissionIor = 1.5f;
};

public struct ResolvedMaterialRayPayload
{
    public ResolvedMaterialFlag flags = ResolvedMaterialFlag.none;
    public uint materialIndex = kRtMaterialFallbackIndex;
    public uint instanceMetadataIndex = 0xffffffffu;
    public uint geometryIndex = 0xffffffffu;
    public float3 position = float3(0.0f);
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 shadingNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 anisotropyTangent = float3(1.0f, 0.0f, 0.0f);
    public float anisotropyStrength = 0.0f;
    public float4 baseColor = float4(1.0f);
    public float metallic = 0.0f;
    public float roughness = 1.0f;
    public float3 emissive = float3(0.0f);
    public ResolvedMaterialLayerRayPayload layers = {};
    public float3 scatterDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 scatterDiffuseWeight = float3(0.0f);
    public float3 scatterSpecularWeight = float3(0.0f);
    public float scatterEtaRatio = 1.0f;
};

public struct RayPayload
{
    public ResolvedMaterialRayPayload resolved = {};

    public property RayKind kind
    {
        get
        {
            return resolved.flags.has(ResolvedMaterialFlag.visibilityRay)
                ? RayKind.visibility
                : RayKind.material;
        }
        set(value)
        {
            resolved.flags = resolved.flags.withFlag(
                ResolvedMaterialFlag.visibilityRay,
                value == RayKind.visibility);
        }
    }

    public property bool hit
    {
        get { return resolved.flags.has(ResolvedMaterialFlag.hit); }
        set(value) { resolved.flags = resolved.flags.withFlag(ResolvedMaterialFlag.hit, value); }
    }

    public property float4 scatterRandomValues
    {
        get { return float4(resolved.scatterDirection, resolved.scatterDiffuseWeight.x); }
        set(value)
        {
            resolved.scatterDirection = value.xyz;
            resolved.scatterDiffuseWeight.x = value.w;
        }
    }

    public property RandomSequence materialFilterSequence
    {
        get
        {
            RandomSequence result;
            result.sampleIndex = resolved.materialIndex;
            result.sampleSeed = resolved.instanceMetadataIndex;
            return result;
        }
        set(value)
        {
            resolved.materialIndex = value.sampleIndex;
            resolved.instanceMetadataIndex = value.sampleSeed;
        }
    }

    public property float incidentMediumIor
    {
        get { return asfloat(resolved.geometryIndex); }
        set(value) { resolved.geometryIndex = asuint(value); }
    }

    public property float exteriorMediumIor
    {
        get { return resolved.anisotropyStrength; }
        set(value) { resolved.anisotropyStrength = value; }
    }

    public property float3 missRadiance
    {
        get { return resolved.position; }
        set(value) { resolved.position = value; }
    }
};

// Invoke input and hit output have disjoint lifetimes. Store the transient CHS input in output slots,
// then copy every input to ClosestHitInput before writeResolvedMaterialRayPayload overwrites them.
public void initializeMaterialRayPayload(
    inout RayPayload payload,
    float4 scatterRandomValues,
    RandomSequence materialFilterSequence,
    float incidentMediumIor,
    float exteriorMediumIor)
{
    payload = {};
    payload.scatterRandomValues = scatterRandomValues;
    payload.materialFilterSequence = materialFilterSequence;
    payload.incidentMediumIor = incidentMediumIor;
    payload.exteriorMediumIor = exteriorMediumIor;
}

public void initializeVisibilityRayPayload(inout RayPayload payload)
{
    payload = {};
    payload.kind = RayKind.visibility;
    payload.hit = true;
}

public void writeResolvedMaterialRayPayload(
    inout RayPayload payload,
    ResolvedMaterialPayload material,
    ResolvedMaterialScatter scatter)
{
    ResolvedMaterialFlag rayState =
        payload.resolved.flags & ResolvedMaterialFlag.rayStateMask;
    payload.resolved = {};
    payload.resolved.flags =
        rayState |
        (material.flags & ResolvedMaterialFlag.materialMask) |
        (scatter.flags & ResolvedMaterialFlag.scatterMask);
    if (material.layers.transmissionMode == RtTransmissionMode.volume)
    {
        payload.resolved.flags |= ResolvedMaterialFlag.volumeTransmission;
    }

    payload.resolved.materialIndex = material.materialIndex;
    payload.resolved.instanceMetadataIndex = material.instanceMetadataIndex;
    payload.resolved.geometryIndex = material.geometryIndex;
    payload.resolved.position = material.position;
    payload.resolved.geometryNormal = material.geometryNormal;
    payload.resolved.shadingNormal = material.shadingNormal;
    payload.resolved.anisotropyTangent = material.anisotropyTangent;
    payload.resolved.anisotropyStrength = material.anisotropyStrength;
    payload.resolved.baseColor = material.baseColor;
    payload.resolved.metallic = material.metallic;
    payload.resolved.roughness = material.roughness;
    payload.resolved.emissive = material.emissive;
    payload.resolved.layers.clearcoat = material.layers.clearcoat;
    payload.resolved.layers.clearcoatRoughness = material.layers.clearcoatRoughness;
    payload.resolved.layers.clearcoatNormal = material.layers.clearcoatNormal;
    payload.resolved.layers.sheenColor = material.layers.sheenColor;
    payload.resolved.layers.sheenRoughness = material.layers.sheenRoughness;
    payload.resolved.layers.transmission = material.layers.transmission;
    payload.resolved.layers.transmissionIor = material.layers.transmissionIor;
    payload.resolved.scatterDirection = scatter.direction;
    payload.resolved.scatterDiffuseWeight = scatter.diffuseWeight;
    payload.resolved.scatterSpecularWeight = scatter.specularWeight;
    payload.resolved.scatterEtaRatio = scatter.etaRatio;
    payload.hit = true;
}

public ResolvedMaterialPayload resolvedMaterialFromRayPayload(
    RayPayload payload,
    float incidentMediumIor,
    float exteriorMediumIor)
{
    ResolvedMaterialPayload material;
    material.flags = payload.resolved.flags & ResolvedMaterialFlag.materialMask;
    material.materialIndex = payload.resolved.materialIndex;
    material.instanceMetadataIndex = payload.resolved.instanceMetadataIndex;
    material.geometryIndex = payload.resolved.geometryIndex;
    material.position = payload.resolved.position;
    material.geometryNormal = payload.resolved.geometryNormal;
    material.shadingNormal = payload.resolved.shadingNormal;
    material.anisotropyTangent = payload.resolved.anisotropyTangent;
    material.anisotropyStrength = payload.resolved.anisotropyStrength;
    material.baseColor = payload.resolved.baseColor;
    material.metallic = payload.resolved.metallic;
    material.roughness = payload.resolved.roughness;
    material.emissive = payload.resolved.emissive;
    material.layers.clearcoat = payload.resolved.layers.clearcoat;
    material.layers.clearcoatRoughness = payload.resolved.layers.clearcoatRoughness;
    material.layers.clearcoatNormal = payload.resolved.layers.clearcoatNormal;
    material.layers.sheenColor = payload.resolved.layers.sheenColor;
    material.layers.sheenRoughness = payload.resolved.layers.sheenRoughness;
    material.layers.transmission = payload.resolved.layers.transmission;
    material.layers.transmissionMode =
        payload.resolved.flags.has(ResolvedMaterialFlag.volumeTransmission)
            ? RtTransmissionMode.volume
            : RtTransmissionMode.thin;
    material.layers.transmissionIor = payload.resolved.layers.transmissionIor;
    // These values remain authoritative in PathState while the ray is in flight. Restoring them at
    // decode preserves direct-light volume evaluation without spending two output payload lanes.
    material.incidentMediumIor = incidentMediumIor;
    material.exteriorMediumIor = exteriorMediumIor;
    return material;
}

public ResolvedMaterialScatter resolvedMaterialScatterFromRayPayload(
    RayPayload payload)
{
    ResolvedMaterialScatter scatter;
    scatter.flags = payload.resolved.flags & ResolvedMaterialFlag.scatterMask;
    scatter.direction = payload.resolved.scatterDirection;
    scatter.diffuseWeight = payload.resolved.scatterDiffuseWeight;
    scatter.specularWeight = payload.resolved.scatterSpecularWeight;
    scatter.weight = scatter.diffuseWeight + scatter.specularWeight;
    scatter.etaRatio = payload.resolved.scatterEtaRatio;
    return scatter;
}

public struct PathState
{
    public uint2 pixel = uint2(0u);
    public uint2 dimensions = uint2(0u);
    public PathFlag flags = PathFlag.none;
    public uint vertexIndex = 0u;
    public uint diffuseBounces = 0u;
    public uint specularBounces = 0u;
    public RandomSequence rng = {};
    public float3 origin = float3(0.0f);
    public float3 direction = float3(0.0f, 0.0f, -1.0f);
    public float3 specularThroughput = float3(0.0f);
    public float3 diffuseThroughput = float3(1.0f);
    public float3 specularRadiance = float3(0.0f);
    public float3 diffuseRadiance = float3(0.0f);
    public PathMediumBoundary mediumStack[kPathMediumStackCapacity];
    public uint mediumDepth = 0u;
    public float etaScale = 1.0f;
    public PathTracingGuideState guides = {};

    public property bool active
    {
        get { return flags & PathFlag.active; }
        set(value)
        {
            flags = value
                ? flags | PathFlag.active
                : flags & ~PathFlag.active;
        }
    }
};

public void terminatePathState(inout PathState path)
{
    path.active = false;
}

public float3 pathCombinedThroughput(PathState path)
{
    return path.specularThroughput + path.diffuseThroughput;
}

public float3 pathCombinedRadiance(PathState path)
{
    return path.specularRadiance + path.diffuseRadiance;
}

public float pathCurrentMediumIor(PathState path)
{
    return path.mediumDepth == 0u ? 1.0f : path.mediumStack[path.mediumDepth - 1u].ior;
}

public float pathExteriorMediumIor(PathState path)
{
    return path.mediumDepth < 2u ? 1.0f : path.mediumStack[path.mediumDepth - 2u].ior;
}

public bool pathMediumTopMatches(
    PathState path,
    uint instanceMetadataIndex,
    uint geometryIndex,
    uint materialIndex)
{
    if (path.mediumDepth == 0u)
    {
        return false;
    }

    PathMediumBoundary top = path.mediumStack[path.mediumDepth - 1u];
    return top.instanceMetadataIndex == instanceMetadataIndex &&
        top.geometryIndex == geometryIndex &&
        top.materialIndex == materialIndex;
}

public bool pushPathMediumBoundary(
    inout PathState path,
    uint instanceMetadataIndex,
    uint geometryIndex,
    uint materialIndex,
    float ior)
{
    if (path.mediumDepth >= kPathMediumStackCapacity ||
        !isfinite(ior) ||
        ior <= 0.0f)
    {
        return false;
    }

    PathMediumBoundary boundary;
    boundary.instanceMetadataIndex = instanceMetadataIndex;
    boundary.geometryIndex = geometryIndex;
    boundary.materialIndex = materialIndex;
    boundary.ior = ior;
    path.mediumStack[path.mediumDepth] = boundary;
    path.mediumDepth += 1u;
    return true;
}

public bool popPathMediumBoundary(
    inout PathState path,
    uint instanceMetadataIndex,
    uint geometryIndex,
    uint materialIndex)
{
    if (!pathMediumTopMatches(path, instanceMetadataIndex, geometryIndex, materialIndex))
    {
        return false;
    }

    path.mediumDepth -= 1u;
    path.mediumStack[path.mediumDepth] = {};
    return true;
}

public void addPathContribution(inout PathState path, float3 radiance)
{
    path.specularRadiance += path.specularThroughput * radiance;
    path.diffuseRadiance += path.diffuseThroughput * radiance;
}

public void addDiffusePathContribution(inout PathState path, float3 radiance)
{
    path.diffuseRadiance += path.diffuseThroughput * radiance;
}

public void addPrimaryPathContribution(
    inout PathState path,
    float3 specularRadiance,
    float3 diffuseRadiance)
{
    path.specularRadiance += path.diffuseThroughput * specularRadiance;
    path.diffuseRadiance += path.diffuseThroughput * diffuseRadiance;
}

public void updatePathThroughput(inout PathState path, float3 weight)
{
    path.specularThroughput *= weight;
    path.diffuseThroughput *= weight;
}

public void initializePrimaryPathThroughput(
    inout PathState path,
    float3 specularWeight,
    float3 diffuseWeight)
{
    float3 prefixThroughput = path.diffuseThroughput;
    path.specularThroughput = prefixThroughput * specularWeight;
    path.diffuseThroughput = prefixThroughput * diffuseWeight;
}

public RayDesc pathStateRayDesc(PathState path)
{
    RayDesc ray;
    ray.Origin = path.origin;
    ray.Direction = path.direction;
    ray.TMin = kRayEpsilon;
    ray.TMax = kRayTMax;
    return ray;
}
