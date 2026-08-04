module materialPayloadPackingContract;
import common;
import renderer.pathTracing.pathState;

[[vk::binding(8, 3)]]
RWStructuredBuffer<uint> gContractResults;

public bool payloadDirectionMatches(float3 actual, float3 expected, float3 fallback)
{
    float3 normalizedExpected = safePayloadDirection(expected, fallback);
    return all(isfinite(actual)) && abs(dot(actual, actual) - 1.0f) < 1.0e-4f &&
           dot(actual, normalizedExpected) > 0.999f;
}

public bool payloadPairMatches(float2 actual, float2 expected)
{
    return all(abs(actual - expected) <= float2(2.0f / 65535.0f));
}

public uint verifyMaterialFilterPacketAdvance()
{
    RandomSequence reference;
    reference.sampleIndex = 0x13579bdfu;
    reference.sampleSeed = 0x2468ace0u;
    RandomSequence advanced = reference;

    float4 discarded0 = reference.rand4();
    float4 discarded1 = reference.rand4();
    float4 discarded2 = reference.rand4();
    advanced.advanceThreeRand4Packets();

    uint finiteMask =
        uint(all(isfinite(discarded0))) & uint(all(isfinite(discarded1))) & uint(all(isfinite(discarded2)));
    return finiteMask & uint(reference.sampleIndex == advanced.sampleIndex) &
           uint(reference.sampleSeed == advanced.sampleSeed);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void payloadPackingContractMain(uint3 dispatchThreadId: SV_DispatchThreadID)
{
    float3 fallbackY = float3(0.0f, 1.0f, 0.0f);
    float3 canonicalDirections[6] = {
        float3(1.0f, 0.0f, 0.0f),  float3(-1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, -1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f),  float3(0.0f, 0.0f, -1.0f),
    };
    for (uint directionIndex = 0u; directionIndex < 6u; ++directionIndex)
    {
        float3 direction = canonicalDirections[directionIndex];
        gContractResults[directionIndex] = uint(payloadDirectionMatches(
            unpackPayloadOct32(packPayloadOct32(direction, fallbackY), fallbackY), direction, fallbackY));
    }

    float3 seamDirection = safePayloadDirection(float3(0.0f, 0.25f, -1.0f), fallbackY);
    float3 negativeSeamDirection = safePayloadDirection(float3(-0.001f, 0.25f, -1.0f), fallbackY);
    uint seamWord = packPayloadOct32(seamDirection, fallbackY);
    uint negativeSeamWord = packPayloadOct32(negativeSeamDirection, fallbackY);
    float2 seamEncoded = unpackSnorm2x16ToFloat(seamWord);
    float2 negativeSeamEncoded = unpackSnorm2x16ToFloat(negativeSeamWord);
    gContractResults[6] = uint(
        seamEncoded.x > 0.0f && negativeSeamEncoded.x < 0.0f &&
        payloadDirectionMatches(unpackPayloadOct32(seamWord, fallbackY), seamDirection, fallbackY) &&
        payloadDirectionMatches(unpackPayloadOct32(negativeSeamWord, fallbackY), negativeSeamDirection, fallbackY));

    float invalidNan = asfloat(0x7fc00000u);
    float invalidInfinity = asfloat(0x7f800000u);
    gContractResults[7] =
        uint(all(unpackPayloadOct32(packPayloadOct32(float3(0.0f), fallbackY), fallbackY) == fallbackY) &&
             all(unpackPayloadOct32(packPayloadOct32(float3(invalidNan, 0.0f, 0.0f), float3(1.0f, 0.0f, 0.0f)),
                                    float3(1.0f, 0.0f, 0.0f)) == float3(1.0f, 0.0f, 0.0f)) &&
             all(unpackPayloadOct32(packPayloadOct32(float3(0.0f, invalidInfinity, 0.0f), float3(0.0f, 0.0f, 1.0f)),
                                    float3(0.0f, 0.0f, 1.0f)) == float3(0.0f, 0.0f, 1.0f)));

    ResolvedMaterialPayload material = {};
    material.materialIndex = 0x10203040u;
    material.instanceMetadataIndex = 0x50607080u;
    material.geometryIndex = 0x90abcdefu;
    material.position = float3(1.25f, -2.5f, 3.75f);
    material.geometryNormal = safePayloadDirection(float3(1.0f, 2.0f, 3.0f), fallbackY);
    material.shadingNormal = safePayloadDirection(float3(-2.0f, 1.0f, 3.0f), fallbackY);
    material.anisotropyTangent = safePayloadDirection(float3(1.0f, 2.0f, -1.0f), float3(1.0f, 0.0f, 0.0f));
    material.anisotropyStrength = 0.0f;
    material.baseColor = float4(0.125f, 0.25f, 0.5f, 0.75f);
    material.metallic = 1.0f;
    material.roughness = 0.2f;
    material.emissive = float3(0.5f, 1.5f, 2.5f);
    material.layers.clearcoat = 0.8f;
    material.layers.clearcoatRoughness = 1.0f;
    material.layers.clearcoatNormal = safePayloadDirection(float3(2.0f, -3.0f, 1.0f), fallbackY);
    material.layers.sheenColor = float3(0.125f, 0.375f, 0.625f);
    material.layers.sheenRoughness = 0.0f;
    material.layers.transmission = 0.875f;
    material.layers.transmissionMode = RtTransmissionMode.volume;
    material.layers.transmissionIor = 1.625f;

    ResolvedMaterialScatter scatter = {};
    scatter.direction = safePayloadDirection(float3(-3.0f, -1.0f, -2.0f), fallbackY);
    scatter.diffuseWeight = float3(0.125f, 0.25f, 0.375f);
    scatter.specularWeight = float3(0.5f, 0.625f, 0.75f);
    scatter.etaRatio = 0.875f;

    RandomSequence filterSequence;
    filterSequence.sampleIndex = 0x12345678u;
    filterSequence.sampleSeed = 0x9abcdef0u;
    MaterialRayPayload rayPayload;
    float4 scatterRandomValues = float4(0.0625f, 0.1875f, 0.3125f, 0.4375f);
    initializeMaterialRayPayload(rayPayload, scatterRandomValues, filterSequence, 1.25f, 1.75f);
    gContractResults[8] = uint(all(rayPayload.scatterRandomValues == scatterRandomValues) &&
                               rayPayload.materialFilterSequence.sampleIndex == filterSequence.sampleIndex &&
                               rayPayload.materialFilterSequence.sampleSeed == filterSequence.sampleSeed &&
                               rayPayload.incidentMediumIor == 1.25f && rayPayload.exteriorMediumIor == 1.75f);

    writeResolvedMaterialRayPayload(rayPayload, material, scatter);
    ResolvedMaterialPayload decodedMaterial = resolvedMaterialFromRayPayload(rayPayload, 1.25f, 1.75f);
    ResolvedMaterialScatter decodedScatter = resolvedMaterialScatterFromRayPayload(rayPayload);

    gContractResults[9] = uint(
        payloadDirectionMatches(decodedMaterial.geometryNormal, material.geometryNormal, fallbackY) &&
        payloadDirectionMatches(decodedMaterial.shadingNormal, material.shadingNormal, fallbackY) &&
        payloadDirectionMatches(decodedMaterial.layers.clearcoatNormal, material.layers.clearcoatNormal, fallbackY) &&
        payloadDirectionMatches(decodedScatter.direction, scatter.direction, fallbackY));
    float3 expectedTangent =
        safeNormalize(material.anisotropyTangent - decodedMaterial.shadingNormal *
                                                       dot(decodedMaterial.shadingNormal, material.anisotropyTangent),
                      orthogonalTangent(decodedMaterial.shadingNormal));
    gContractResults[10] =
        uint(abs(dot(decodedMaterial.anisotropyTangent, decodedMaterial.shadingNormal)) < 1.0e-4f &&
             abs(dot(decodedMaterial.anisotropyTangent, decodedMaterial.anisotropyTangent) - 1.0f) < 1.0e-4f &&
             dot(decodedMaterial.anisotropyTangent, expectedTangent) > 0.999f);

    gContractResults[11] = uint(payloadPairMatches(
        unpackUnorm2x16ToFloat(rayPayload.resolved.anisotropyStrengthMetallicUnorm2x16), float2(0.0f, 1.0f)));
    gContractResults[12] = uint(payloadPairMatches(
        unpackUnorm2x16ToFloat(rayPayload.resolved.roughnessClearcoatUnorm2x16), float2(0.2f, 0.8f)));
    gContractResults[13] = uint(payloadPairMatches(
        unpackUnorm2x16ToFloat(rayPayload.resolved.clearcoatRoughnessSheenColorXUnorm2x16), float2(1.0f, 0.125f)));
    gContractResults[14] = uint(
        payloadPairMatches(unpackUnorm2x16ToFloat(rayPayload.resolved.sheenColorYZUnorm2x16), float2(0.375f, 0.625f)));
    gContractResults[15] = uint(payloadPairMatches(
        unpackUnorm2x16ToFloat(rayPayload.resolved.sheenRoughnessTransmissionUnorm2x16), float2(0.0f, 0.875f)));

    float unormTolerance = 2.0f / 65535.0f;
    gContractResults[16] =
        uint(abs(decodedMaterial.anisotropyStrength - 0.0f) <= unormTolerance &&
             abs(decodedMaterial.metallic - 1.0f) <= unormTolerance &&
             abs(decodedMaterial.roughness - 0.2f) <= unormTolerance &&
             abs(decodedMaterial.layers.clearcoat - 0.8f) <= unormTolerance &&
             abs(decodedMaterial.layers.clearcoatRoughness - 1.0f) <= unormTolerance &&
             all(abs(decodedMaterial.layers.sheenColor - float3(0.125f, 0.375f, 0.625f)) <= float3(unormTolerance)) &&
             decodedMaterial.layers.sheenRoughness == kMaterialMinRoughness &&
             abs(decodedMaterial.layers.transmission - 0.875f) <= unormTolerance);
    gContractResults[17] = uint(
        rayPayload.hit && decodedMaterial.materialIndex == material.materialIndex &&
        decodedMaterial.instanceMetadataIndex == material.instanceMetadataIndex &&
        decodedMaterial.geometryIndex == material.geometryIndex && all(decodedMaterial.position == material.position) &&
        all(decodedMaterial.baseColor == material.baseColor) && all(decodedMaterial.emissive == material.emissive) &&
        decodedMaterial.layers.transmissionIor == material.layers.transmissionIor &&
        decodedMaterial.layers.transmissionMode == RtTransmissionMode.volume &&
        decodedMaterial.incidentMediumIor == 1.25f && decodedMaterial.exteriorMediumIor == 1.75f &&
        all(decodedScatter.diffuseWeight == scatter.diffuseWeight) &&
        all(decodedScatter.specularWeight == scatter.specularWeight) && decodedScatter.etaRatio == scatter.etaRatio);
    gContractResults[18] = verifyMaterialFilterPacketAdvance();
}
