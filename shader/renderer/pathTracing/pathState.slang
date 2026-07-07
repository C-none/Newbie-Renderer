module pathState;
import common;
import renderer.pathTracing.params;

[Flags]
public enum PathTracingPathFlag : uint
{
    none = 0u,
    active = 1u << 0u,
    hit = 1u << 1u,
}

public struct PathTracingRayPayload
{
    public PathTracingRayKind rayKind = PathTracingRayKind.material;
    public uint hit = 0u;
    public ResolvedMaterialPayload material = {};
};

public struct PathState
{
    public uint2 pixel = uint2(0u);
    public uint2 dimensions = uint2(0u);
    public PathTracingPathFlag flags = PathTracingPathFlag.none;
    public uint vertexIndex = 0u;
    public uint diffuseBounces = 0u;
    public uint specularBounces = 0u;
    public uint4 rngState = uint4(0u);
    public float3 origin = float3(0.0f);
    public float3 direction = float3(0.0f, 0.0f, -1.0f);
    public float3 throughput = float3(1.0f);
    public float3 radiance = float3(0.0f);
    public float pdf = 1.0f;
};

public bool pathStateIsActive(PathState path)
{
    return (uint(path.flags) & uint(PathTracingPathFlag.active)) != 0u;
}

public bool pathStateHasHit(PathState path)
{
    return (uint(path.flags) & uint(PathTracingPathFlag.hit)) != 0u;
}

public void pathStateSetActive(inout PathState path, bool active)
{
    uint flags = uint(path.flags);
    if (active)
    {
        flags |= uint(PathTracingPathFlag.active);
    }
    else
    {
        flags &= ~uint(PathTracingPathFlag.active);
    }

    path.flags = PathTracingPathFlag(flags);
}

public void pathStateSetHit(inout PathState path, bool hit)
{
    uint flags = uint(path.flags);
    if (hit)
    {
        flags |= uint(PathTracingPathFlag.hit);
    }
    else
    {
        flags &= ~uint(PathTracingPathFlag.hit);
    }

    path.flags = PathTracingPathFlag(flags);
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
