module params;
import common;

public extern static const uint kMaxSurfaceBounces;
public static const uint kDirectLightSampleCount = 4u;
public static const float kRayEpsilon = 0.001f;
public static const float kRayTMax = 100000.0f;
public static const float3 kMissRadiance = float3(0.015f, 0.018f, 0.022f);

public enum RayKind : uint
{
    material = 0u,
    visibility = 1u,
}
