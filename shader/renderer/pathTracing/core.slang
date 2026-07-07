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

    PathState path = {};
    path.pixel = pixel;
    path.dimensions = dimensions;
    path.flags = PathTracingPathFlag.active;
    path.rngState = makePathTracingSeed(pixel, dimensions, gFrame.frameState.xy);
    path.origin = origin;
    path.direction = safeNormalize(directionVector, float3(0.0f, 0.0f, -1.0f));
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
    if (SceneLightType(light.meta.x) == SceneLightType.directional)
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

        contribution.radiance *= 1.0f / lightSample.pdf;
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

    float hitAlpha = material.alphaMode == AlphaMode.blend ? material.alpha : 1.0f;
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

    if (scatter.kind == ResolvedMaterialScatterKind.specular)
    {
        path.specularBounces += 1u;
    }
    else
    {
        path.diffuseBounces += 1u;
    }

    updatePathThroughput(path, scatter.weight);
    PathTracingRussianRoulettePolicy roulettePolicy = PathTracingRussianRoulettePolicy();
    if (roulettePolicy.shouldApply(path.vertexIndex))
    {
        float survivalProbability = roulettePolicy.survivalProbability(path.throughput);
        if (!roulettePolicy.survives(nextPathTracingRandom01(path.rngState), survivalProbability))
        {
            terminatePathState(path);
            return;
        }

        path.throughput *= roulettePolicy.throughputScale(survivalProbability);
    }

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
    // Path radiance is scene-linear HDR RGB. Present/Display convert this to
    // screen pixels by clamping to [0,1], optional linear->sRGB transfer, optional
    // BGR swizzle, and UI compositing in the Present path.
    outputImage[int2(path.pixel)] = float4(path.radiance, 1.0f);
}

public struct Pt
{
    public PathState path;

    public bool isActive()
    {
        return pathStateIsActive(path);
    }

    [mutating]
    public void traceMaterialRay(out PathTracingRayPayload payload)
    {
        path.vertexIndex += 1u;
        payload = {};
        payload.rayKind = PathTracingRayKind.material;

        // TLAS instance flags encode glTF source winding, double-sided culling, and transform determinant.
        TraceRay(
            scene,
            RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
            0xFF,
            0,
            1,
            0,
            pathStateRayDesc(path),
            payload);
    }

    [mutating]
    public void handleTraceResult(PathTracingRayPayload payload)
    {
        if (payload.hit != 0u)
        {
            handleHit(payload.material);
        }
        else
        {
            handleMiss();
        }
    }

    [mutating]
    public void handleHit(ResolvedMaterialPayload material)
    {
        handlePathTracingHit(path, material);
    }

    [mutating]
    public void handleMiss()
    {
        handlePathTracingMiss(path);
    }

    public void writeOutput()
    {
        writePathTracingOutput(path);
    }
};

public Pt makePathTracingPt(uint2 pixel, uint2 dimensions)
{
    Pt pt;
    pt.path = generatePathTracingPath(pixel, dimensions);
    return pt;
}
