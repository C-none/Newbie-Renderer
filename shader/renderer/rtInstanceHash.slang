module rtInstanceHash;
import common;

RaytracingAccelerationStructure scene;
RWTexture2D<float4> outputImage;

struct RayPayload
{
    uint hit;
    uint instanceId;
};

float safeDivideW(float w)
{
    if (abs(w) > 1.0e-6f)
    {
        return w;
    }
    return w < 0.0f ? -1.0e-6f : 1.0e-6f;
}

float3 homogeneousToWorld(float4 value)
{
    return value.xyz / safeDivideW(value.w);
}

uint hashUint(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float3 colorFromInstanceId(uint instanceId)
{
    uint hashValue = hashUint(instanceId + 1u);
    float r = float(hashValue & 0xFFu) / 255.0f;
    float g = float((hashValue >> 8) & 0xFFu) / 255.0f;
    float b = float((hashValue >> 16) & 0xFFu) / 255.0f;
    return lerp(float3(0.12f, 0.18f, 0.28f), float3(r, g, b), 0.85f);
}

[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    float2 uv = (float2(pixel) + 0.5f) / float2(dimensions);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 nearClip = float4(ndc, 0.0f, 1.0f);
    float4 farClip = float4(ndc, 1.0f, 1.0f);
    float3 nearWorld = homogeneousToWorld(mul(gFrame.inverseViewProjection, nearClip));
    float3 farWorld = homogeneousToWorld(mul(gFrame.inverseViewProjection, farClip));

    float3 origin = gFrame.cameraWorld.xyz;
    float3 directionVector = farWorld - origin;
    if (dot(directionVector, directionVector) <= 1.0e-8f)
    {
        directionVector = farWorld - nearWorld;
    }
    float3 direction = normalize(directionVector);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = 100000.0f;

    RayPayload payload;
    payload.hit = 0u;
    payload.instanceId = 0xFFFFFFFFu;

    // TLAS instance flags encode glTF source winding and transform determinant.
    TraceRay(scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

    float3 color = payload.hit != 0u
                       ? colorFromInstanceId(payload.instanceId)
                       : float3(0.015f, 0.018f, 0.022f);
    outputImage[int2(pixel)] = float4(color, 1.0f);
}

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.hit = 0u;
    payload.instanceId = 0xFFFFFFFFu;
}

[shader("closesthit")]
void chMain(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    float barycentricUse = attributes.barycentrics.x + attributes.barycentrics.y;
    payload.hit = 1u + uint(barycentricUse * 0.0f);
    payload.instanceId = uint(InstanceID());
}
