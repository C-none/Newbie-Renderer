module core;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.pathState;
import renderer.pathTracing.neuralMaterial;
import renderer.pathTracing.resources;
import renderer.pathTracing.environment;
import renderer.pathTracing.guides;
import renderer.pathTracing.visibility;

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

float3 offsetRayOrigin(float3 position, float3 normal, float3 direction)
{
    float3 orientedNormal = dot(normal, direction) >= 0.0f ? normal : -normal;
    return position + orientedNormal * kRayEpsilon;
}

float lightVisibilityDistance(uint lightIndex, float3 hitPosition)
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

// Direct lighting is evaluated after CHS returns, so the CHS link-time LayerFlags is not a raygen
// compile-time constant. This runtime switch maps the resolved combined material flags to the concrete
// static evaluator. The root raygen SPIR-V contains all sixteen valid lit cases.
ResolvedMaterialDirectComponents dispatchEvaluateResolvedMaterialDirectComponents(
    ResolvedMaterialPayload material, MaterialDirectLightingInput lighting)
{
    switch (uint(material.layerFlags))
    {
    case 1u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(1u)>(material, lighting);
    case 3u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(3u)>(material, lighting);
    case 5u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(5u)>(material, lighting);
    case 7u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(7u)>(material, lighting);
    case 9u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(9u)>(material, lighting);
    case 11u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(11u)>(material, lighting);
    case 13u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(13u)>(material, lighting);
    case 15u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(15u)>(material, lighting);
    case 17u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(17u)>(material, lighting);
    case 19u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(19u)>(material, lighting);
    case 21u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(21u)>(material, lighting);
    case 23u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(23u)>(material, lighting);
    case 25u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(25u)>(material, lighting);
    case 27u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(27u)>(material, lighting);
    case 29u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(29u)>(material, lighting);
    case 31u:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(31u)>(material, lighting);
    default:
        return evaluateResolvedMaterialDirectComponentsVariant<RtMaterialLayerFlag(1u)>(material, lighting);
    }
}

void sampleDirectLighting(inout PathState path, ResolvedMaterialPayload material, RtNeuralMaterialPayloadContext neural,
                           float3 viewDirection, float2 randomValues)
{
    SceneLightSample lightSample;
    if (!sampleSceneLightAlias(randomValues, lightSample))
    {
        return;
    }

    SceneLightContribution contribution;
    if (!evaluateSceneLightAt(lightSample.lightIndex, material.position, contribution))
    {
        return;
    }

    contribution.radiance *= 1.0f / lightSample.pdf;
    float3 visibilityOrigin = offsetRayOrigin(material.position, material.geometryNormal, contribution.direction);
    if (!tracePathVisibilityRay(visibilityOrigin, contribution.direction,
                                lightVisibilityDistance(lightSample.lightIndex, material.position)))
    {
        return;
    }

    ResolvedMaterialDirectComponents direct =
        dispatchEvaluateResolvedMaterialDirectComponents(
            material, MaterialDirectLightingInput(viewDirection, contribution.direction, contribution.radiance));
    if (rtNeuralP0Eligible(material, neural.enabled))
    {
        bool neuralValid = false;
        float3 neuralProjected = evaluateRtNeuralProjected(neural.repeatUv, material.anisotropyTangent,
                                                            material.shadingNormal, neural.tangentSign, viewDirection,
                                                            contribution.direction, neuralValid);
        direct.specularRadiance =
            resolveRtNeuralDirectSpecularRadiance(direct.specularRadiance, neuralProjected, contribution.radiance,
                                                  neuralValid);
    }
    if (path.vertexIndex == 1u)
    {
        addPrimaryPathContribution(path, direct.specularRadiance, direct.diffuseRadiance);
    }
    else
    {
        addPathContribution(path, direct.specularRadiance + direct.diffuseRadiance);
    }
}

bool reachedBounceLimit(PathState path)
{
    return path.vertexIndex >= kMaxSurfaceBounces;
}

static const uint kMaterialRayCoherenceHintBitCount = 3u;

uint materialRayBounceClass(uint vertexIndex)
{
    if (vertexIndex <= 1u)
    {
        return 0u;
    }
    if (vertexIndex == 2u)
    {
        return 1u;
    }
    return 2u;
}

uint materialRayCoherenceHint(uint vertexIndex)
{
    // HitObject identity already groups miss and link-time material shaders. The two low bits group
    // primary, second, and later-vertex raygen branches; the MSB marks the fixed bounce-limit path.
    uint hint = materialRayBounceClass(vertexIndex);
    if (vertexIndex >= kMaxSurfaceBounces)
    {
        hint |= 1u << 2u;
    }
    return hint;
}

bool resolvedMaterialIsVolumeBoundary(ResolvedMaterialPayload material)
{
    return uint(material.layerFlags & RtMaterialLayerFlag.transmission) != 0u &&
           material.layers.transmissionMode == RtTransmissionMode.volume;
}

bool applyResolvedMaterialMediumTransition(inout PathState path, ResolvedMaterialPayload material,
                                           ResolvedMaterialScatter scatter)
{
    if (scatter.mediumTransition == ResolvedMaterialMediumTransition.none)
    {
        return true;
    }
    if (!resolvedMaterialIsVolumeBoundary(material) || !isfinite(scatter.etaRatio) || scatter.etaRatio <= 0.0f)
    {
        return false;
    }

    bool transitioned = false;
    if (scatter.mediumTransition == ResolvedMaterialMediumTransition.enter)
    {
        if (!material.frontFace ||
            pathMediumTopMatches(path, material.instanceMetadataIndex, material.geometryIndex, material.materialIndex))
        {
            return false;
        }
        transitioned = pushPathMediumBoundary(path, material.instanceMetadataIndex, material.geometryIndex,
                                              material.materialIndex, material.layers.transmissionIor);
    }
    else if (scatter.mediumTransition == ResolvedMaterialMediumTransition.exit)
    {
        if (material.frontFace)
        {
            return false;
        }
        transitioned =
            popPathMediumBoundary(path, material.instanceMetadataIndex, material.geometryIndex, material.materialIndex);
    }

    if (!transitioned)
    {
        return false;
    }

    float nextEtaScale = path.etaScale * scatter.etaRatio * scatter.etaRatio;
    if (!isfinite(nextEtaScale) || nextEtaScale <= 0.0f)
    {
        return false;
    }
    path.etaScale = nextEtaScale;
    return true;
}

void handleHit(inout PathState path, ResolvedMaterialPayload material, ResolvedMaterialScatter scatter,
               RtNeuralMaterialPayloadContext neural)
{
    float3 viewDirection = safeNormalize(-path.direction, material.geometryNormal);

    if (resolvedMaterialIsVolumeBoundary(material) && !material.frontFace &&
        !pathMediumTopMatches(path, material.instanceMetadataIndex, material.geometryIndex, material.materialIndex))
    {
        // The scoped volume implementation supports nested strict-LIFO boundaries only. Camera-inside,
        // non-manifold, and overlapping/non-LIFO configurations terminate instead of guessing an IOR.
        terminatePathState(path);
        return;
    }

    if (path.vertexIndex == 1u)
    {
        capturePathTracingPrimarySurfaceGuides(path.guides, material, viewDirection, path.dimensions);
    }
    else if (path.vertexIndex == 2u)
    {
        resolvePathTracingSpecularHit(path.guides, material.position);
    }

    float hitAlpha = material.alphaMode == AlphaMode.blend ? material.alpha : 1.0f;
    if (path.vertexIndex == 1u && hitAlpha < 1.0f)
    {
        addDiffusePathContribution(path, sampleEnvironment(path.direction) * (1.0f - hitAlpha));
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
    bool hasDirectLightSample = sceneLightAliasCount() != 0u;
    float4 directLightRandomPacket = {};
    if (hasDirectLightSample)
    {
        directLightRandomPacket = path.rng.rand4();
        sampleDirectLighting(path, material, neural, viewDirection, directLightRandomPacket.xy);
    }

    if (reachedBounceLimit(path))
    {
        terminatePathState(path);
        return;
    }

    if (!scatter.valid)
    {
        terminatePathState(path);
        return;
    }

    if (!applyResolvedMaterialMediumTransition(path, material, scatter))
    {
        terminatePathState(path);
        return;
    }

    ResolvedMaterialScatterKind scatterKind = scatter.kind;

    if (path.vertexIndex == 1u)
    {
        initializePrimaryPathThroughput(path, scatter.specularWeight, scatter.diffuseWeight);
    }
    else
    {
        updatePathThroughput(path, scatter.weight);
    }

    RussianRoulettePolicy roulettePolicy = RussianRoulettePolicy();
    if (roulettePolicy.shouldApply(path.vertexIndex))
    {
        float survivalProbability = roulettePolicy.survivalProbability(pathCombinedThroughput(path) * path.etaScale);
        float rouletteRandomValue = hasDirectLightSample ? directLightRandomPacket.z : path.rng.rand4().x;
        if (!roulettePolicy.survives(rouletteRandomValue, survivalProbability))
        {
            terminatePathState(path);
            return;
        }

        updatePathThroughput(path, float3(roulettePolicy.throughputScale(survivalProbability)));
    }

    if (path.vertexIndex == 1u)
    {
        beginPathTracingSpecularHitTracking(path.guides, scatterKind);
    }
    path.origin = offsetRayOrigin(material.position, material.geometryNormal, scatter.direction);
    path.direction = scatter.direction;
}

void handleMiss(inout PathState path, float3 missRadiance)
{
    if (path.vertexIndex == 1u)
    {
        // Primary camera misses consume the same environment sample written by msMaterial.
        addDiffusePathContribution(path, missRadiance);
    }
    else
    {
        addPathContribution(path, missRadiance);
    }
    terminatePathState(path);
}

void writeOutput(PathState path)
{
    // Path radiance is scene-linear HDR RGB. Present/Display convert this to
    // screen pixels by clamping to [0,1], optional linear->sRGB transfer, optional
    // BGR swizzle, and UI compositing in the Present path.
    outputImage[int2(path.pixel)] = float4(pathCombinedRadiance(path), 1.0f);
    writePathTracingGuides(path.pixel, path.guides);
}

public struct Pt
{
    internal PathState path = PathState();

    public __init(uint2 pixel, uint2 dimensions)
    {
        float2 uv = (float2(pixel) + 0.5f) / float2(dimensions);
        float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

        float4 nearClip = float4(ndc, 0.0f, 1.0f);
        float4 farClip = float4(ndc, 1.0f, 1.0f);
        float3 nearWorld = homogeneousToWorld(mul(nearClip, gFrame.inverseViewProjection));
        float3 farWorld = homogeneousToWorld(mul(farClip, gFrame.inverseViewProjection));

        float3 origin = gFrame.cameraWorld.xyz;
        float3 directionVector = farWorld - origin;
        if (dot(directionVector, directionVector) <= 1.0e-8f)
        {
            directionVector = farWorld - nearWorld;
        }

        float3 direction = safeNormalize(directionVector, float3(0.0f, 0.0f, -1.0f));
        path = PathState(pixel, dimensions, RandomSequence(pixel, gFrame.frameState.xy), origin, direction);
    }

    public bool isActive()
    {
        return path.active;
    }

    [mutating]
    public void traceMaterialRay(out MaterialRayPayload payload)
    {
        path.vertexIndex += 1u;

        float4 scatterRandomValues = path.rng.rand4();
        RandomSequence materialFilterSequence = path.rng;
        // The largest material CHS reads eleven texture semantics. Reserve
        // three packets (12 scalar dimensions, one padding lane) after scatter
        // for every material segment, including miss, unlit, and FAS-off paths.
        // rand4() evolves five seed keys per packet. This one closed-form jump
        // is exactly equivalent to discarding the three complete packets.
        path.rng.advanceThreeRand4Packets();
        payload = MaterialRayPayload(scatterRandomValues, materialFilterSequence, pathCurrentMediumIor(path),
                                     pathExteriorMediumIor(path));

        // TLAS instance flags encode object-space source winding and instance-wide facing-cull policy.
        // Non-opaque candidates use any-hit for alpha coverage and mixed-mesh back-face filtering.
        HitObject hitObject =
            HitObject::TraceRay(scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, uint(RtRayType.material),
                                uint(RtRayType.count), uint(RtRayType.material), pathStateRayDesc(path), payload);
        ReorderThread(hitObject, materialRayCoherenceHint(path.vertexIndex), kMaterialRayCoherenceHintBitCount);
        HitObject::Invoke(scene, hitObject, payload);
    }

    [mutating]
    public void handleTraceResult(MaterialRayPayload payload)
    {
        if (payload.hit)
        {
            RtNeuralMaterialPayloadContext neural = rtNeuralMaterialPayloadContext(payload);
            ResolvedMaterialPayload material =
                resolvedMaterialFromRayPayload(payload, pathCurrentMediumIor(path), pathExteriorMediumIor(path));
            ResolvedMaterialScatter scatter = resolvedMaterialScatterFromRayPayload(payload);
            handleHit(material, scatter, neural);
        }
        else
        {
            handleMiss(payload.missRadiance);
        }
    }

    [mutating]
    internal void handleHit(ResolvedMaterialPayload material, ResolvedMaterialScatter scatter,
                            RtNeuralMaterialPayloadContext neural = RtNeuralMaterialPayloadContext())
    {
        handleHit(path, material, scatter, neural);
    }

    [mutating]
    internal void handleMiss(float3 missRadiance)
    {
        handleMiss(path, missRadiance);
    }

    public void writeOutput()
    {
        writeOutput(path);
    }
};
