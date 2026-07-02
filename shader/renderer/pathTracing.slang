module pathTracing;
import common;
import include.rt.hitSurface;

RaytracingAccelerationStructure scene;
RWTexture2D<float4> outputImage;
StructuredBuffer<RtInstanceMetadata> rtInstanceMetadata;
StructuredBuffer<RtGeometryMetadata> rtGeometryMetadata;
StructuredBuffer<RtMaterialHeader> rtMaterialHeaders;
StructuredBuffer<RtMaterialLayerRecord> rtMaterialLayers;
StructuredBuffer<RtMaterialTextureRef> rtMaterialTextureRefs;
ByteAddressBuffer rtVertexData;
ByteAddressBuffer rtIndexData;

static const float3 kMissRadiance = float3(0.015f, 0.018f, 0.022f);

struct RayPayload
{
    uint hit;
    float3 radiance;
    float alpha;
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

float3 hardcodedLightRadiance(float3 hitPosition, out float3 lightDirection)
{
    float3 lightPosition = float3(0.0f, 5.0f, 0.0f);
    float3 lightColor = float3(1.0f, 0.96f, 0.88f);
    float lightPower = 360.0f;

    float3 toLight = lightPosition - hitPosition;
    float distanceSquared = max(dot(toLight, toLight), 0.05f);
    lightDirection = toLight * rsqrt(distanceSquared);
    return lightColor * (lightPower / distanceSquared);
}

MaterialDirectLightingInput makePrimaryLighting(RtSurfaceData surface, float3 viewDirection)
{
    MaterialDirectLightingInput lighting;
    lighting.viewDirection = viewDirection;
    lighting.lightRadiance = hardcodedLightRadiance(surface.position, lighting.lightDirection);
    return lighting;
}

MaterialShadingResult shadePrimarySurface(RtMaterialHeader material, RtSurfaceData surface, float3 viewDirection)
{
    GltfMultiLayerMaterial materialModel;
    return materialModel.evaluateDirect(
        rtMaterialTextureRefs,
        rtMaterialLayers,
        material,
        surface,
        makePrimaryLighting(surface, viewDirection));
}

bool shouldIgnoreAlphaMaskedHit(RtMaterialHeader material, RtSurfaceData surface)
{
    if (!hasRtMaterialFeature(material, kRtMaterialFeatureAlphaMask))
    {
        return false;
    }

    GltfMultiLayerMaterial materialModel;
    return materialModel.alpha(rtMaterialTextureRefs, material, surface) < material.alphaCutoff;
}

void writeClosestHitPayload(
    inout RayPayload payload,
    RtMaterialHeader material,
    MaterialShadingResult shading)
{
    payload.hit = 1u;
    payload.radiance = shading.radiance;
    payload.alpha = material.alphaMode == kAlphaModeBlend ? shading.alpha : 1.0f;
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
    payload.radiance = float3(0.0f);
    payload.alpha = 1.0f;

    // TLAS instance flags encode glTF source winding, double-sided culling, and transform determinant.
    TraceRay(scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

    float3 color = payload.hit != 0u
                       ? lerp(kMissRadiance, payload.radiance, payload.alpha)
                       : kMissRadiance;
    outputImage[int2(pixel)] = float4(color, 1.0f);
}

[shader("miss")]
void msMain(inout RayPayload payload)
{
    payload.hit = 0u;
    payload.radiance = float3(0.0f);
    payload.alpha = 1.0f;
}

[shader("anyhit")]
void ahMain(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = nrCurrentRtHitSurface(
        rtInstanceMetadata,
        rtGeometryMetadata,
        rtVertexData,
        rtIndexData,
        attributes,
        viewDirection);
    if (!hit.geometryValid)
    {
        return;
    }

    RtMaterialHeader material = rtMaterialHeaders[hit.geometry.materialIndex];
    nrOrientRtSurfaceForMaterial(hit.surface, material, viewDirection);
    if (shouldIgnoreAlphaMaskedHit(material, hit.surface))
    {
        IgnoreHit();
    }
}

[shader("closesthit")]
void chMain(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    float3 viewDirection = normalize(-WorldRayDirection());
    RtHitSurface hit = nrCurrentRtHitSurface(
        rtInstanceMetadata,
        rtGeometryMetadata,
        rtVertexData,
        rtIndexData,
        attributes,
        viewDirection);
    RtMaterialHeader material = rtMaterialHeaders[hit.geometry.materialIndex];
    nrOrientRtSurfaceForMaterial(hit.surface, material, viewDirection);
    writeClosestHitPayload(payload, material, shadePrimarySurface(material, hit.surface, viewDirection));
}
