module pathState;
import common;
import renderer.pathTracing.params;
import renderer.pathTracing.guides;

[Flags]
public enum PathFlag : uint
{
    none = 0u,
    active = 1u << 0u,
}

public static const uint kPathMediumStackCapacity = 4u;
public static const uint kInvalidPathMediumBoundaryIndex = 0xffffffffu;

public struct PathMediumBoundary
{
    public uint instanceMetadataIndex = kInvalidPathMediumBoundaryIndex;
    public uint geometryIndex = kInvalidPathMediumBoundaryIndex;
    public uint materialIndex = kRtMaterialFallbackIndex;
    public float ior = 1.0f;

    public __init()
    {
    }

    public __init(uint boundaryInstanceMetadataIndex, uint boundaryGeometryIndex, uint boundaryMaterialIndex,
                  float boundaryIor)
    {
        instanceMetadataIndex = boundaryInstanceMetadataIndex;
        geometryIndex = boundaryGeometryIndex;
        materialIndex = boundaryMaterialIndex;
        ior = boundaryIor;
    }
};

// Fixed 32-lane transport representation. Directional and bounded material values are packed only
// while crossing the ray boundary; the BSDF-facing working records remain full precision.
public struct ResolvedMaterialRayPayload
{
    public ResolvedMaterialFlag flags = ResolvedMaterialFlag.none;
    public uint materialIndex = kRtMaterialFallbackIndex;
    public uint instanceMetadataIndex = 0xffffffffu;
    public uint geometryIndex = 0xffffffffu;
    public float3 position = float3(0.0f);
    public float4 baseColor = float4(1.0f);
    public float3 emissive = float3(0.0f);
    public float transmissionIor = 1.5f;
    public float3 scatterDiffuseWeight = float3(0.0f);
    public float3 scatterSpecularWeight = float3(0.0f);
    public float scatterEtaRatio = 1.0f;
    public uint geometryNormalOct32 = 0u;
    public uint shadingNormalOct32 = 0u;
    public uint anisotropyTangentOct32 = 0u;
    public uint clearcoatNormalOct32 = 0u;
    public uint scatterDirectionOct32 = 0u;
    public uint anisotropyStrengthMetallicUnorm2x16 = 0u;
    public uint roughnessClearcoatUnorm2x16 = 0u;
    public uint clearcoatRoughnessSheenColorXUnorm2x16 = 0u;
    public uint sheenColorYZUnorm2x16 = 0u;
    public uint sheenRoughnessTransmissionUnorm2x16 = 0u;

    public __init()
    {
    }

    public __init(ResolvedMaterialPayload material, ResolvedMaterialScatter scatter)
    {
        flags = (material.flags & ResolvedMaterialFlag.materialMask) | (scatter.flags & ResolvedMaterialFlag.scatterMask) |
                ResolvedMaterialFlag.hit;
        if (material.layers.transmissionMode == RtTransmissionMode.volume)
        {
            flags |= ResolvedMaterialFlag.volumeTransmission;
        }

        materialIndex = material.materialIndex;
        instanceMetadataIndex = material.instanceMetadataIndex;
        geometryIndex = material.geometryIndex;
        position = material.position;
        baseColor = material.baseColor;
        emissive = material.emissive;
        transmissionIor = material.layers.transmissionIor;
        scatterDiffuseWeight = scatter.diffuseWeight;
        scatterSpecularWeight = scatter.specularWeight;
        scatterEtaRatio = scatter.etaRatio;
        geometryNormalOct32 = packPayloadOct32(material.geometryNormal, float3(0.0f, 1.0f, 0.0f));
        shadingNormalOct32 = packPayloadOct32(material.shadingNormal, float3(0.0f, 1.0f, 0.0f));
        anisotropyTangentOct32 = packPayloadOct32(material.anisotropyTangent, float3(1.0f, 0.0f, 0.0f));
        clearcoatNormalOct32 = packPayloadOct32(material.layers.clearcoatNormal, float3(0.0f, 1.0f, 0.0f));
        scatterDirectionOct32 = packPayloadOct32(scatter.direction, float3(0.0f, 1.0f, 0.0f));
        anisotropyStrengthMetallicUnorm2x16 =
            packPayloadUnorm2x16(float2(material.anisotropyStrength, material.metallic));
        roughnessClearcoatUnorm2x16 = packPayloadUnorm2x16(float2(material.roughness, material.layers.clearcoat));
        clearcoatRoughnessSheenColorXUnorm2x16 =
            packPayloadUnorm2x16(float2(material.layers.clearcoatRoughness, material.layers.sheenColor.x));
        sheenColorYZUnorm2x16 = packPayloadUnorm2x16(material.layers.sheenColor.yz);
        sheenRoughnessTransmissionUnorm2x16 =
            packPayloadUnorm2x16(float2(material.layers.sheenRoughness, material.layers.transmission));
    }
};

public float2 oct32NonzeroSign(float2 value)
{
    return float2(value.x >= 0.0f ? 1.0f : -1.0f, value.y >= 0.0f ? 1.0f : -1.0f);
}

public float3 safePayloadDirection(float3 value, float3 fallback)
{
    if (!all(isfinite(value)))
    {
        return fallback;
    }

    float scale = max(max(abs(value.x), abs(value.y)), abs(value.z));
    if (scale <= 1.0e-8f)
    {
        return fallback;
    }

    float3 scaled = value / scale;
    return scaled * rsqrt(dot(scaled, scaled));
}

public uint packPayloadOct32(float3 value, float3 fallback)
{
    float3 direction = safePayloadDirection(value, fallback);
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    float2 encoded = direction.xy;
    if (direction.z < 0.0f)
    {
        encoded = (1.0f - abs(encoded.yx)) * oct32NonzeroSign(encoded);
    }
    return packSnorm2x16(encoded);
}

public float3 unpackPayloadOct32(uint value, float3 fallback)
{
    float2 encoded = unpackSnorm2x16ToFloat(value);
    float3 direction = float3(encoded, 1.0f - abs(encoded.x) - abs(encoded.y));
    if (direction.z < 0.0f)
    {
        direction.xy = (1.0f - abs(direction.yx)) * oct32NonzeroSign(direction.xy);
    }
    return safePayloadDirection(direction, fallback);
}

public uint packPayloadUnorm2x16(float2 value)
{
    return packUnorm2x16(saturate(value));
}

public struct MaterialRayPayload
{
    public ResolvedMaterialRayPayload resolved = ResolvedMaterialRayPayload();

    public __init()
    {
    }

    public __init(float4 initialScatterRandomValues, RandomSequence initialMaterialFilterSequence,
                  float initialIncidentMediumIor, float initialExteriorMediumIor)
    {
        scatterRandomValues = initialScatterRandomValues;
        materialFilterSequence = initialMaterialFilterSequence;
        incidentMediumIor = initialIncidentMediumIor;
        exteriorMediumIor = initialExteriorMediumIor;
    }

    public property bool hit
    {
        get {
            return resolved.flags.has(ResolvedMaterialFlag.hit);
        }
        set(value) {
            resolved.flags = resolved.flags.withFlag(ResolvedMaterialFlag.hit, value);
        }
    }

    public property float4 scatterRandomValues
    {
        get {
            return resolved.baseColor;
        }
        set(value) {
            resolved.baseColor = value;
        }
    }

    public property RandomSequence materialFilterSequence
    {
        get {
            return RandomSequence(resolved.materialIndex, resolved.instanceMetadataIndex);
        }
        set(value) {
            resolved.materialIndex = value.sampleIndex;
            resolved.instanceMetadataIndex = value.sampleSeed;
        }
    }

    public property float incidentMediumIor
    {
        get {
            return asfloat(resolved.geometryIndex);
        }
        set(value) {
            resolved.geometryIndex = asuint(value);
        }
    }

    public property float exteriorMediumIor
    {
        get {
            return resolved.transmissionIor;
        }
        set(value) {
            resolved.transmissionIor = value;
        }
    }

    public property float3 missRadiance
    {
        get {
            return resolved.position;
        }
        set(value) {
            resolved.position = value;
        }
    }
};

public void writeResolvedMaterialRayPayload(inout MaterialRayPayload payload, ResolvedMaterialPayload material,
                                             ResolvedMaterialScatter scatter)
{
    payload.resolved = ResolvedMaterialRayPayload(material, scatter);
}

public ResolvedMaterialPayload resolvedMaterialFromRayPayload(MaterialRayPayload payload, float incidentMediumIor,
                                                              float exteriorMediumIor)
{
    float3 geometryNormal = unpackPayloadOct32(payload.resolved.geometryNormalOct32, float3(0.0f, 1.0f, 0.0f));
    float3 shadingNormal = unpackPayloadOct32(payload.resolved.shadingNormalOct32, geometryNormal);
    float3 anisotropyTangent =
        unpackPayloadOct32(payload.resolved.anisotropyTangentOct32, orthogonalTangent(shadingNormal));
    anisotropyTangent = safeNormalize(anisotropyTangent - shadingNormal * dot(shadingNormal, anisotropyTangent),
                                      orthogonalTangent(shadingNormal));
    float2 anisotropyStrengthMetallic = unpackUnorm2x16ToFloat(payload.resolved.anisotropyStrengthMetallicUnorm2x16);
    float2 roughnessClearcoat = unpackUnorm2x16ToFloat(payload.resolved.roughnessClearcoatUnorm2x16);
    float2 clearcoatRoughnessSheenColorX =
        unpackUnorm2x16ToFloat(payload.resolved.clearcoatRoughnessSheenColorXUnorm2x16);
    float2 sheenColorYZ = unpackUnorm2x16ToFloat(payload.resolved.sheenColorYZUnorm2x16);
    float2 sheenRoughnessTransmission = unpackUnorm2x16ToFloat(payload.resolved.sheenRoughnessTransmissionUnorm2x16);

    MaterialLayerStack layers = MaterialLayerStack(
        roughnessClearcoat.y, max(clearcoatRoughnessSheenColorX.x, kMaterialMinRoughness),
        unpackPayloadOct32(payload.resolved.clearcoatNormalOct32, shadingNormal),
        float3(clearcoatRoughnessSheenColorX.y, sheenColorYZ),
        max(sheenRoughnessTransmission.x, kMaterialMinRoughness), sheenRoughnessTransmission.y,
        payload.resolved.flags.has(ResolvedMaterialFlag.volumeTransmission) ? RtTransmissionMode.volume
                                                                            : RtTransmissionMode.thin,
        payload.resolved.transmissionIor);
    // These values remain authoritative in PathState while the ray is in flight. Restoring them at
    // decode preserves direct-light volume evaluation without spending two output payload lanes.
    return ResolvedMaterialPayload(
        payload.resolved.flags & ResolvedMaterialFlag.materialMask, payload.resolved.materialIndex,
        payload.resolved.instanceMetadataIndex, payload.resolved.geometryIndex, payload.resolved.position,
        geometryNormal, shadingNormal, anisotropyTangent, anisotropyStrengthMetallic.x, payload.resolved.baseColor,
        anisotropyStrengthMetallic.y, max(roughnessClearcoat.x, kMaterialMinRoughness), payload.resolved.emissive,
        layers, incidentMediumIor, exteriorMediumIor);
}

public ResolvedMaterialScatter resolvedMaterialScatterFromRayPayload(MaterialRayPayload payload)
{
    float3 diffuseWeight = payload.resolved.scatterDiffuseWeight;
    float3 specularWeight = payload.resolved.scatterSpecularWeight;
    return ResolvedMaterialScatter(
        payload.resolved.flags & ResolvedMaterialFlag.scatterMask,
        unpackPayloadOct32(payload.resolved.scatterDirectionOct32, float3(0.0f, 1.0f, 0.0f)),
        diffuseWeight + specularWeight, diffuseWeight, specularWeight, 0.0f, 0.0f, payload.resolved.scatterEtaRatio);
}

public struct PathState
{
    public uint2 pixel = uint2(0u);
    public uint2 dimensions = uint2(0u);
    public PathFlag flags = PathFlag.none;
    public uint vertexIndex = 0u;
    public RandomSequence rng = RandomSequence();
    public float3 origin = float3(0.0f);
    public float3 direction = float3(0.0f, 0.0f, -1.0f);
    public float3 specularThroughput = float3(0.0f);
    public float3 diffuseThroughput = float3(1.0f);
    public float3 specularRadiance = float3(0.0f);
    public float3 diffuseRadiance = float3(0.0f);
    public PathMediumBoundary mediumStack[kPathMediumStackCapacity] = {};
    public uint mediumDepth = 0u;
    public float etaScale = 1.0f;
    public PathTracingGuideState guides = PathTracingGuideState();

    public __init()
    {
    }

    public __init(uint2 initialPixel, uint2 initialDimensions, RandomSequence initialRng, float3 initialOrigin,
                  float3 initialDirection)
    {
        pixel = initialPixel;
        dimensions = initialDimensions;
        active = true;
        rng = initialRng;
        origin = initialOrigin;
        direction = initialDirection;
        guides = PathTracingGuideState(initialDirection, initialDimensions);
    }

    public property bool active
    {
        get {
            return flags & PathFlag.active;
        }
        set(value) {
            flags = value ? flags | PathFlag.active : flags & ~PathFlag.active;
        }
    }
};

public void terminatePathState(inout PathState path)
{
    path.active = false;
}

public float3 pathCombinedThroughput(PathState path)
{
    return path.specularThroughput + path.diffuseThroughput;
}

public float3 pathCombinedRadiance(PathState path)
{
    return path.specularRadiance + path.diffuseRadiance;
}

public float pathCurrentMediumIor(PathState path)
{
    return path.mediumDepth == 0u ? 1.0f : path.mediumStack[path.mediumDepth - 1u].ior;
}

public float pathExteriorMediumIor(PathState path)
{
    return path.mediumDepth < 2u ? 1.0f : path.mediumStack[path.mediumDepth - 2u].ior;
}

public bool pathMediumTopMatches(PathState path, uint instanceMetadataIndex, uint geometryIndex, uint materialIndex)
{
    if (path.mediumDepth == 0u)
    {
        return false;
    }

    PathMediumBoundary top = path.mediumStack[path.mediumDepth - 1u];
    return top.instanceMetadataIndex == instanceMetadataIndex && top.geometryIndex == geometryIndex &&
           top.materialIndex == materialIndex;
}

public bool pushPathMediumBoundary(inout PathState path, uint instanceMetadataIndex, uint geometryIndex,
                                   uint materialIndex, float ior)
{
    if (path.mediumDepth >= kPathMediumStackCapacity || !isfinite(ior) || ior <= 0.0f)
    {
        return false;
    }

    path.mediumStack[path.mediumDepth] =
        PathMediumBoundary(instanceMetadataIndex, geometryIndex, materialIndex, ior);
    path.mediumDepth += 1u;
    return true;
}

public bool popPathMediumBoundary(inout PathState path, uint instanceMetadataIndex, uint geometryIndex,
                                  uint materialIndex)
{
    if (!pathMediumTopMatches(path, instanceMetadataIndex, geometryIndex, materialIndex))
    {
        return false;
    }

    path.mediumDepth -= 1u;
    path.mediumStack[path.mediumDepth] = PathMediumBoundary();
    return true;
}

public void addPathContribution(inout PathState path, float3 radiance)
{
    path.specularRadiance += path.specularThroughput * radiance;
    path.diffuseRadiance += path.diffuseThroughput * radiance;
}

public void addDiffusePathContribution(inout PathState path, float3 radiance)
{
    path.diffuseRadiance += path.diffuseThroughput * radiance;
}

public void addPrimaryPathContribution(inout PathState path, float3 specularRadiance, float3 diffuseRadiance)
{
    path.specularRadiance += path.diffuseThroughput * specularRadiance;
    path.diffuseRadiance += path.diffuseThroughput * diffuseRadiance;
}

public void updatePathThroughput(inout PathState path, float3 weight)
{
    path.specularThroughput *= weight;
    path.diffuseThroughput *= weight;
}

public void initializePrimaryPathThroughput(inout PathState path, float3 specularWeight, float3 diffuseWeight)
{
    float3 prefixThroughput = path.diffuseThroughput;
    path.specularThroughput = prefixThroughput * specularWeight;
    path.diffuseThroughput = prefixThroughput * diffuseWeight;
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
