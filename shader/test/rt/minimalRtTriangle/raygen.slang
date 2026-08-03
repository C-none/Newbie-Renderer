module raygen;
import common;
import test.rt.minimalRtTriangle.abi;

[shader("raygeneration")]
void rgMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
    float2 ndc = uv * 2.0 - 1.0;

    float3 origin = camera.origin.xyz;
    float3 direction = normalize(camera.forward.xyz + ndc.x * camera.right.xyz + ndc.y * camera.up.xyz);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.color = float3(0.02, 0.02, 0.05);

    HitObject hitObject = HitObject::TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    ReorderThread(hitObject);
    HitObject::Invoke(scene, hitObject, payload);

    outputImage[int2(pixel)] = float4(payload.color, 1.0);
}
