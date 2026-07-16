module core;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.resources;
import renderer.pathTracing.guides;
import renderer.pathTracing.visibility;

public float safeDivideW(float w)
{
    if (abs(w) > 1.0e-6f)
    {
        return w;
    }
    return w < 0.0f ? -1.0e-6f : 1.0e-6f;
}

public float3 homogeneousToWorld(float4 value)
{
    return value.xyz / safeDivideW(value.w);
}

public float3 offsetRayOrigin(float3 position, float3 normal, float3 direction)
{
    float3 orientedNormal = dot(normal, direction) >= 0.0f ? normal : -normal;
    return position + orientedNormal * kRayEpsilon;
}

public PathState generatePath(uint2 pixel, uint2 dimensions)
{
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

    PathState path = {};
    path.pixel = pixel;
    path.dimensions = dimensions;
    path.flags = PathFlag.active;
    path.rng = makeErrorDiffusionSequence(pixel, gFrame.frameState.xy);
    path.origin = origin;
    path.direction = safeNormalize(directionVector, float3(0.0f, 0.0f, -1.0f));
    initializePathTracingSkyGuides(path.guides, path.direction, dimensions);
    return path;
}

public MaterialDirectLightingInput makeLighting(float3 viewDirection, SceneLightContribution contribution)
{
    MaterialDirectLightingInput lighting;
    lighting.viewDirection = viewDirection;
    lighting.lightDirection = contribution.direction;
    lighting.lightRadiance = contribution.radiance;
    return lighting;
}

public float lightVisibilityDistance(uint lightIndex, float3 hitPosition)
{
    if (lightIndex >= sceneLightCount())
    {
        return kRayTMax;
    }

    SceneLightGpuRecord light = gSceneLights[lightIndex];
    if (SceneLightType(light.meta.x) == SceneLightType.directional)
    {
        return kRayTMax;
    }

    float3 toLight = light.positionRange.xyz - hitPosition;
    return sqrt(max(dot(toLight, toLight), kSceneLightMinDistanceSquared));
}

public float2 makeAliasLightSample(float2 baseSample, uint sampleIndex)
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
        random01FromHash(seed ^ 1597334677u),
        random01FromHash(seed ^ 3812015801u));
}

// Direct lighting is evaluated after CHS returns, so the CHS link-time LayerFlags is not a raygen
// compile-time constant. This runtime switch maps the resolved layerFlags to the concrete static
// evaluator so each invocation runs only its active-layer math. The root raygen SPIR-V contains every
// valid lit case; that is the accepted boundary of the current single-raygen architecture.
public ResolvedMaterialDirectComponents dispatchEvaluateResolvedMaterialDirectComponents(
    ResolvedMaterialPayload material,
    MaterialDirectLightingInput lighting)
{
    switch (uint(material.layerFlags))
    {
    case 1u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(1u)>(material, lighting);
    case 3u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(3u)>(material, lighting);
    case 5u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(5u)>(material, lighting);
    case 7u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(7u)>(material, lighting);
    case 9u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(9u)>(material, lighting);
    case 11u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(11u)>(material, lighting);
    case 13u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(13u)>(material, lighting);
    case 15u: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(15u)>(material, lighting);
    default: return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(1u)>(material, lighting);
    }
}

public ResolvedMaterialBsdfComponents dispatchEvaluateResolvedMaterialBsdfComponents(
    ResolvedMaterialPayload material,
    float3 viewDirection,
    float3 lightDirection)
{
    switch (uint(material.layerFlags))
    {
    case 1u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(1u)>(material, viewDirection, lightDirection);
    case 3u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(3u)>(material, viewDirection, lightDirection);
    case 5u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(5u)>(material, viewDirection, lightDirection);
    case 7u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(7u)>(material, viewDirection, lightDirection);
    case 9u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(9u)>(material, viewDirection, lightDirection);
    case 11u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(11u)>(material, viewDirection, lightDirection);
    case 13u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(13u)>(material, viewDirection, lightDirection);
    case 15u: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(15u)>(material, viewDirection, lightDirection);
    default: return evaluateResolvedMaterialBsdfComponentsVariant<RtMaterialLayerFlag(1u)>(material, viewDirection, lightDirection);
    }
}

public void sampleDirectLighting(
    inout PathState path,
    ResolvedMaterialPayload material,
    float3 viewDirection)
{
    if (sceneLightAliasCount() == 0u)
    {
        return;
    }

    float2 lightSampleBase = path.rng.get2D();
    float sampleWeight = 1.0f / float(kDirectLightSampleCount);
    for (uint sampleIndex = 0u; sampleIndex < kDirectLightSampleCount; ++sampleIndex)
    {
        SceneLightSample lightSample;
        if (!sampleSceneLightAlias(makeAliasLightSample(lightSampleBase, sampleIndex), lightSample))
        {
            continue;
        }

        SceneLightContribution contribution;
        if (!evaluateSceneLightAt(lightSample.lightIndex, material.position, contribution))
        {
            continue;
        }

        contribution.radiance *= 1.0f / lightSample.pdf;
        float3 visibilityOrigin = offsetRayOrigin(
            material.position,
            material.geometryNormal,
            contribution.direction);
        if (!tracePathVisibilityRay(
            visibilityOrigin,
            contribution.direction,
            lightVisibilityDistance(lightSample.lightIndex, material.position)))
        {
            continue;
        }

        ResolvedMaterialDirectComponents direct = dispatchEvaluateResolvedMaterialDirectComponents(
            material,
            makeLighting(viewDirection, contribution));
        if (path.vertexIndex == 1u)
        {
            addPrimaryPathContribution(
                path,
                direct.specularRadiance * sampleWeight,
                direct.diffuseRadiance * sampleWeight);
        }
        else
        {
            addPathContribution(
                path,
                (direct.specularRadiance + direct.diffuseRadiance) * sampleWeight);
        }
    }
}

public bool reachedBounceLimit(PathState path)
{
    return path.vertexIndex >= kMaxSurfaceBounces;
}

public void handleHit(inout PathState path, ResolvedMaterialPayload material, ResolvedMaterialScatter scatter)
{
    pathStateSetHit(path, true);
    float3 viewDirection = safeNormalize(-path.direction, material.geometryNormal);

    if (path.vertexIndex == 1u)
    {
        capturePathTracingPrimarySurfaceGuides(
            path.guides,
            material,
            viewDirection,
            path.dimensions);
    }
    else if (path.vertexIndex == 2u)
    {
        resolvePathTracingSpecularHit(path.guides, material.position);
    }

    float hitAlpha = material.alphaMode == AlphaMode.blend ? material.alpha : 1.0f;
    if (path.vertexIndex == 1u && hitAlpha < 1.0f)
    {
        addDiffusePathContribution(path, kMissRadiance * (1.0f - hitAlpha));
        updatePathThroughput(path, float3(hitAlpha));
    }

    // Unlit (KHR_materials_unlit): base color RGB is terminal surface radiance. Skip emissive, direct
    // lighting and scatter; the path ends at this hit.
    if (material.layerFlags == RtMaterialLayerFlag.none)
    {
        if (path.vertexIndex == 1u)
        {
            addDiffusePathContribution(path, material.baseColor.rgb);
        }
        else
        {
            addPathContribution(path, material.baseColor.rgb);
        }
        terminatePathState(path);
        return;
    }

    if (path.vertexIndex == 1u)
    {
        addDiffusePathContribution(path, material.emissive);
    }
    else
    {
        addPathContribution(path, material.emissive);
    }

    // NEE is a single-segment shadow connection that does not consume a bounce, so it must run at
    // every surface vertex including the terminal one. The bounce limit only gates BSDF continuation.
    sampleDirectLighting(path, material, viewDirection);

    if (reachedBounceLimit(path))
    {
        terminatePathState(path);
        return;
    }

    if (scatter.valid == 0u)
    {
        terminatePathState(path);
        return;
    }

    if (scatter.kind == ResolvedMaterialScatterKind.specular ||
        scatter.kind == ResolvedMaterialScatterKind.clearcoat ||
        scatter.kind == ResolvedMaterialScatterKind.transmission)
    {
        path.specularBounces += 1u;
    }
    else
    {
        path.diffuseBounces += 1u;
    }

    if (path.vertexIndex == 1u)
    {
        float3 normal = safeNormalize(material.shadingNormal, material.geometryNormal);
        float noL = saturate(dot(normal, scatter.direction));
        ResolvedMaterialBsdfComponents bsdf = dispatchEvaluateResolvedMaterialBsdfComponents(
            material,
            viewDirection,
            scatter.direction);
        float inversePdf = 1.0f / scatter.pdf;
        initializePrimaryPathThroughput(
            path,
            bsdf.specular * noL * inversePdf,
            bsdf.diffuse * noL * inversePdf);
    }
    else
    {
        updatePathThroughput(path, scatter.weight);
    }

    RussianRoulettePolicy roulettePolicy = RussianRoulettePolicy();
    if (roulettePolicy.shouldApply(path.vertexIndex))
    {
        float survivalProbability = roulettePolicy.survivalProbability(pathCombinedThroughput(path));
        if (!roulettePolicy.survives(path.rng.get1D(), survivalProbability))
        {
            terminatePathState(path);
            return;
        }

        updatePathThroughput(path, float3(roulettePolicy.throughputScale(survivalProbability)));
    }

    path.pdf = scatter.pdf;
    if (path.vertexIndex == 1u)
    {
        beginPathTracingSpecularHitTracking(path.guides, scatter.kind);
    }
    path.origin = offsetRayOrigin(material.position, material.geometryNormal, scatter.direction);
    path.direction = scatter.direction;
}

public void handleMiss(inout PathState path)
{
    pathStateSetHit(path, false);
    if (path.vertexIndex == 2u)
    {
        resolvePathTracingSpecularMiss(path.guides);
    }
    if (path.vertexIndex == 1u)
    {
        addDiffusePathContribution(path, kMissRadiance);
    }
    else
    {
        addPathContribution(path, kMissRadiance);
    }
    terminatePathState(path);
}

public void writeOutput(PathState path)
{
    // Path radiance is scene-linear HDR RGB. Present/Display convert this to
    // screen pixels by clamping to [0,1], optional linear->sRGB transfer, optional
    // BGR swizzle, and UI compositing in the Present path.
    outputImage[int2(path.pixel)] = float4(pathCombinedRadiance(path), 1.0f);
    writePathTracingGuides(path.pixel, path.guides);
}

public struct Pt
{
    public PathState path;

    public bool isActive()
    {
        return pathStateIsActive(path);
    }

    [mutating]
    public void traceMaterialRay(out RayPayload payload)
    {
        path.vertexIndex += 1u;
        payload = {};
        payload.rayKind = RayKind.material;
        payload.scatterRandomValues = path.rng.get3D();

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
    public void handleTraceResult(RayPayload payload)
    {
        if (payload.hit != 0u)
        {
            handleHit(payload.material, payload.scatter);
        }
        else
        {
            handleMiss();
        }
    }

    [mutating]
    public void handleHit(ResolvedMaterialPayload material, ResolvedMaterialScatter scatter)
    {
        handleHit(path, material, scatter);
    }

    [mutating]
    public void handleMiss()
    {
        handleMiss(path);
    }

    public void writeOutput()
    {
        writeOutput(path);
    }
};

public Pt makePt(uint2 pixel, uint2 dimensions)
{
    Pt pt;
    pt.path = generatePath(pixel, dimensions);
    return pt;
}
