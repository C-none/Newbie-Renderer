module pathState;
import common;
import renderer.pathTracing.params;

[Flags]
public enum PathFlag : uint
{
    none = 0u,
    active = 1u << 0u,
    hit = 1u << 1u,
}

public struct RayPayload
{
    public RayKind rayKind = RayKind.material;
    public uint hit = 0u;
    public float3 scatterRandomValues = float3(0.0f);
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
    public float3 throughput = float3(1.0f);
    public float3 radiance = float3(0.0f);
    public float pdf = 1.0f;
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
    ray.TMin = kRayEpsilon;
    ray.TMax = kRayTMax;
    return ray;
}
