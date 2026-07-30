module pathState;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.guides;

[Flags]
public enum PathFlag : uint
{
    none = 0u,
    active = 1u << 0u,
    hit = 1u << 1u,
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

public struct RayPayload
{
    public RayKind rayKind = RayKind.material;
    public uint hit = 0u;
    public float3 missRadiance = float3(0.0f);
    public float4 scatterRandomValues = float4(0.0f);
    public float incidentMediumIor = 1.0f;
    public float exteriorMediumIor = 1.0f;
    public ResolvedMaterialPayload material = {};
    public ResolvedMaterialScatter scatter = {};
};

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
    public float pdf = 1.0f;
    public PathMediumBoundary mediumStack[kPathMediumStackCapacity];
    public uint mediumDepth = 0u;
    public float etaScale = 1.0f;
    public PathTracingGuideState guides = {};
};

public bool pathStateIsActive(PathState path)
{
    return (uint(path.flags) & uint(PathFlag.active)) != 0u;
}

public bool pathStateHasHit(PathState path)
{
    return (uint(path.flags) & uint(PathFlag.hit)) != 0u;
}

public void pathStateSetActive(inout PathState path, bool active)
{
    uint flags = uint(path.flags);
    if (active)
    {
        flags |= uint(PathFlag.active);
    }
    else
    {
        flags &= ~uint(PathFlag.active);
    }

    path.flags = PathFlag(flags);
}

public void pathStateSetHit(inout PathState path, bool hit)
{
    uint flags = uint(path.flags);
    if (hit)
    {
        flags |= uint(PathFlag.hit);
    }
    else
    {
        flags &= ~uint(PathFlag.hit);
    }

    path.flags = PathFlag(flags);
}

public void terminatePathState(inout PathState path)
{
    pathStateSetActive(path, false);
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
