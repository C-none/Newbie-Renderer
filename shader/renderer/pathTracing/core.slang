module core;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.random;
import renderer.pathTracing.pathState;
import renderer.pathTracing.resources;
import renderer.pathTracing.visibility;

public float safePathTracingDivideW(float w)
{
    if (abs(w) > 1.0e-6f)
    {
        return w;
    }
    return w < 0.0f ? -1.0e-6f : 1.0e-6f;
}

public float3 pathTracingHomogeneousToWorld(float4 value)
{
    return value.xyz / safePathTracingDivideW(value.w);
}

public float3 offsetPathTracingRayOrigin(float3 position, float3 normal, float3 direction)
{
    float3 orientedNormal = dot(normal, direction) >= 0.0f ? normal : -normal;
    return position + orientedNormal * kPathTracingRayEpsilon;
}

public PathState generatePathTracingPath(uint2 pixel, uint2 dimensions)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(dimensions);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 nearClip = float4(ndc, 0.0f, 1.0f);
    float4 farClip = float4(ndc, 1.0f, 1.0f);
    float3 nearWorld = pathTracingHomogeneousToWorld(mul(gFrame.inverseViewProjection, nearClip));
    float3 farWorld = pathTracingHomogeneousToWorld(mul(gFrame.inverseViewProjection, farClip));

    float3 origin = gFrame.cameraWorld.xyz;
    float3 directionVector = farWorld - origin;
    if (dot(directionVector, directionVector) <= 1.0e-8f)
    {
        directionVector = farWorld - nearWorld;
    }

    PathState path;
    path.pixel = pixel;
    path.dimensions = dimensions;
    path.flags = kPathTracingPathFlagActive;
    path.vertexIndex = 0u;
    path.diffuseBounces = 0u;
    path.specularBounces = 0u;
    path.rngState = makePathTracingSeed(pixel, dimensions, gFrame.frameState.xy);
    path.origin = origin;
    path.direction = safeNormalize(directionVector, float3(0.0f, 0.0f, -1.0f));
    path.throughput = float3(1.0f);
    path.radiance = float3(0.0f);
    path.pdf = 1.0f;
    return path;
}

public MaterialDirectLightingInput makePathTracingLighting(float3 viewDirection, SceneLightContribution contribution)
{
    MaterialDirectLightingInput lighting;
    lighting.viewDirection = viewDirection;
    lighting.lightDirection = contribution.direction;
    lighting.lightRadiance = contribution.radiance;
    return lighting;
}

public float pathTracingLightVisibilityDistance(uint lightIndex, float3 hitPosition)
{
    if (lightIndex >= sceneLightCount())
    {
        return kPathTracingRayTMax;
    }

    SceneLightGpuRecord light = gSceneLights[lightIndex];
    if (light.meta.x == kSceneLightTypeDirectional)
    {
        return kPathTracingRayTMax;
    }

    float3 toLight = light.positionRange.xyz - hitPosition;
    return sqrt(max(dot(toLight, toLight), kSceneLightMinDistanceSquared));
}

public float2 makePathTracingAliasLightSample(float2 baseSample, uint sampleIndex)
{
    if (sampleIndex == 0u)
    {
        return baseSample;
    }

    uint sampleX = uint(saturate(baseSample.x) * 16777215.0f);
    uint sampleY = uint(saturate(baseSample.y) * 16777215.0f);
    uint seed =
        sampleX ^
        (sampleY << 11u) ^
        (sampleIndex * 747796405u) ^
        2891336453u;
    return float2(
        pathTracingRandom01FromHash(seed ^ 1597334677u),
        pathTracingRandom01FromHash(seed ^ 3812015801u));
}

public void samplePathTracingDirectLighting(
    inout PathState path,
    ResolvedMaterialPayload material,
    float3 viewDirection)
{
    if (sceneLightAliasCount() == 0u)
    {
        return;
    }

    float2 lightSampleBase = nextPathTracingRandom02(path.rngState);
    float sampleWeight = 1.0f / float(kPathTracingDirectLightSampleCount);
    for (uint sampleIndex = 0u; sampleIndex < kPathTracingDirectLightSampleCount; ++sampleIndex)
    {
        SceneLightSample lightSample;
        if (!sampleSceneLightAlias(makePathTracingAliasLightSample(lightSampleBase, sampleIndex), lightSample))
        {
            continue;
        }

        SceneLightContribution contribution;
        if (!evaluateSceneLightAt(lightSample.lightIndex, material.position, contribution))
        {
            continue;
        }

        contribution.radiance *= kPathTracingLightEnergyScale / lightSample.pdf;
        float3 visibilityOrigin = offsetPathTracingRayOrigin(
            material.position,
            material.geometryNormal,
            contribution.direction);
        if (!tracePathVisibilityRay(
            visibilityOrigin,
            contribution.direction,
            pathTracingLightVisibilityDistance(lightSample.lightIndex, material.position)))
        {
            continue;
        }

        MaterialShadingResult direct = evaluateResolvedMaterialDirect(
            material,
            makePathTracingLighting(viewDirection, contribution));
        addPathContribution(path, direct.radiance * sampleWeight);
    }
}

public bool pathTracingReachedBounceLimit(PathState path)
{
    return path.vertexIndex >= kPathTracingMaxSurfaceBounces;
}

public void handlePathTracingHit(inout PathState path, ResolvedMaterialPayload material)
{
    pathStateSetHit(path, true);
    float3 viewDirection = safeNormalize(-path.direction, material.geometryNormal);

    float hitAlpha = material.alphaMode == kAlphaModeBlend ? material.alpha : 1.0f;
    if (path.vertexIndex == 1u && hitAlpha < 1.0f)
    {
        addPathContribution(path, kPathTracingMissRadiance * (1.0f - hitAlpha));
        path.throughput *= hitAlpha;
    }

    addPathContribution(path, material.emissive);

    if (!pathTracingReachedBounceLimit(path))
    {
        samplePathTracingDirectLighting(path, material, viewDirection);
    }

    if (pathTracingReachedBounceLimit(path))
    {
        terminatePathState(path);
        return;
    }

    ResolvedMaterialScatter scatter = sampleResolvedMaterialScatter(
        material,
        viewDirection,
        nextPathTracingRandom03(path.rngState));
    if (scatter.valid == 0u)
    {
        terminatePathState(path);
        return;
    }

    if (scatter.kind == kResolvedMaterialScatterSpecular)
    {
        path.specularBounces += 1u;
    }
    else
    {
        path.diffuseBounces += 1u;
    }

    updatePathThroughput(path, scatter.weight);
    path.pdf = scatter.pdf;
    path.origin = offsetPathTracingRayOrigin(material.position, material.geometryNormal, scatter.direction);
    path.direction = scatter.direction;
}

public void handlePathTracingMiss(inout PathState path)
{
    pathStateSetHit(path, false);
    addPathContribution(path, kPathTracingMissRadiance);
    terminatePathState(path);
}

public void writePathTracingOutput(PathState path)
{
    outputImage[int2(path.pixel)] = float4(path.radiance, 1.0f);
}
