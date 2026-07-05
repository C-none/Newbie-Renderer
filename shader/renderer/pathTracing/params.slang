module params;
import common;

public extern static const uint kPathTracingMaxSurfaceBounces;
public static const uint kPathTracingDirectLightSampleCount = 4u;
public static const float kPathTracingRayEpsilon = 0.001f;
public static const float kPathTracingRayTMax = 100000.0f;
public static const float3 kPathTracingMissRadiance = float3(0.015f, 0.018f, 0.022f);

public static const uint kPathTracingRayKindMaterial = 0u;
public static const uint kPathTracingRayKindVisibility = 1u;
