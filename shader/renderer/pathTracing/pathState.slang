module pathState;
import common;
import renderer.pathTracing.params;

public static const uint kPathTracingPathFlagActive = 1u << 0u;
public static const uint kPathTracingPathFlagHit = 1u << 1u;

public struct PathTracingRayPayload
{
    public uint rayKind;
    public uint hit;
    public ResolvedMaterialPayload material;
};

public struct PathState
{
    public uint2 pixel;
    public uint2 dimensions;
    public uint flags;
    public uint vertexIndex;
    public uint diffuseBounces;
    public uint specularBounces;
    public uint4 rngState;
    public float3 origin;
    public float3 direction;
    public float3 throughput;
    public float3 radiance;
    public float pdf;
};

public PathTracingRayPayload defaultPathTracingRayPayload(uint rayKind)
{
    PathTracingRayPayload payload;
    payload.rayKind = rayKind;
    payload.hit = 0u;
    payload.material = defaultResolvedMaterialPayload();
    return payload;
}

public PathTracingRayPayload defaultVisibilityRayPayload()
{
    PathTracingRayPayload payload = defaultPathTracingRayPayload(kPathTracingRayKindVisibility);
    payload.hit = 1u;
    return payload;
}

public bool pathStateIsActive(PathState path)
{
    return (path.flags & kPathTracingPathFlagActive) != 0u;
}

public bool pathStateHasHit(PathState path)
{
    return (path.flags & kPathTracingPathFlagHit) != 0u;
}

public void pathStateSetActive(inout PathState path, bool active)
{
    if (active)
    {
        path.flags |= kPathTracingPathFlagActive;
    }
    else
    {
        path.flags &= ~kPathTracingPathFlagActive;
    }
}

public void pathStateSetHit(inout PathState path, bool hit)
{
    if (hit)
    {
        path.flags |= kPathTracingPathFlagHit;
    }
    else
    {
        path.flags &= ~kPathTracingPathFlagHit;
    }
}

public void terminatePathState(inout PathState path)
{
    pathStateSetActive(path, false);
}

public void addPathContribution(inout PathState path, float3 radiance)
{
    path.radiance += path.throughput * radiance;
}

public void updatePathThroughput(inout PathState path, float3 weight)
{
    path.throughput *= weight;
}

public RayDesc pathStateRayDesc(PathState path)
{
    RayDesc ray;
    ray.Origin = path.origin;
    ray.Direction = path.direction;
    ray.TMin = kPathTracingRayEpsilon;
    ray.TMax = kPathTracingRayTMax;
    return ray;
}
