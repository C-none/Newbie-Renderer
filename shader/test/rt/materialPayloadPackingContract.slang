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
    RandomSequence reference = RandomSequence(0x13579bdfu, 0x2468ace0u);
    RandomSequence advanced = RandomSequence(reference.sampleIndex, reference.sampleSeed);

    float4 discarded0 = reference.rand4();
    float4 discarded1 = reference.rand4();
    float4 discarded2 = reference.rand4();
    float4 discarded3 = reference.rand4();
    advanced.advanceFourRand4Packets();

    uint finiteMask =
        uint(all(isfinite(discarded0))) & uint(all(isfinite(discarded1))) & uint(all(isfinite(discarded2))) &
        uint(all(isfinite(discarded3)));
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

    MaterialLayerStack layers = MaterialLayerStack(
        0.8f, 1.0f, safePayloadDirection(float3(2.0f, -3.0f, 1.0f), fallbackY), float3(0.125f, 0.375f, 0.625f),
        0.0f, 0.875f, RtTransmissionMode.volume, 1.625f);
    ResolvedMaterialPayload material = ResolvedMaterialPayload(
        0x10203040u, RtMaterialLayerFlag.none, AlphaMode.opaque, float3(1.25f, -2.5f, 3.75f),
        safePayloadDirection(float3(1.0f, 2.0f, 3.0f), fallbackY),
        safePayloadDirection(float3(-2.0f, 1.0f, 3.0f), fallbackY),
        safePayloadDirection(float3(1.0f, 2.0f, -1.0f), float3(1.0f, 0.0f, 0.0f)), 0.0f,
        float4(0.125f, 0.25f, 0.5f, 0.75f), 1.0f, 0.2f, float3(0.5f, 1.5f, 2.5f), layers, 1.0f, 1.0f,
        0x50607080u, 0x90abcdefu);
    material.dielectricF0 = float3(0.0625f, 0.1875f, 0.3125f);
    material.dielectricF90 = 0.4375f;

    ResolvedMaterialScatter scatter = ResolvedMaterialScatter(
        ResolvedMaterialFlag.none, safePayloadDirection(float3(-3.0f, -1.0f, -2.0f), fallbackY), float3(0.0f),
        float3(0.125f, 0.25f, 0.375f), float3(0.5f, 0.625f, 0.75f), 0.0f, 0.0f, 0.875f);

    RandomSequence filterSequence = RandomSequence(0x12345678u, 0x9abcdef0u);
    float4 scatterRandomValues = float4(0.0625f, 0.1875f, 0.3125f, 0.4375f);
    MaterialRayPayload rayPayload = MaterialRayPayload(scatterRandomValues, filterSequence, 1.25f, 1.75f);
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

    gContractResults[11] = uint(
        payloadPairMatches(unpackUnorm2x16ToFloat(rayPayload.resolved.anisotropyStrengthMetallicUnorm2x16),
                           float2(0.0f, 1.0f)) &&
        payloadPairMatches(unpackUnorm2x16ToFloat(rayPayload.resolved.baseColorRGUnorm2x16), material.baseColor.rg) &&
        payloadPairMatches(unpackUnorm2x16ToFloat(rayPayload.resolved.baseColorBAUnorm2x16), material.baseColor.ba) &&
        payloadPairMatches(unpackUnorm2x16ToFloat(rayPayload.resolved.dielectricF0XAndF90Unorm2x16),
                           float2(material.dielectricF0.x, material.dielectricF90)) &&
        payloadPairMatches(unpackUnorm2x16ToFloat(rayPayload.resolved.dielectricF0YZUnorm2x16),
                           material.dielectricF0.yz));
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
        all(abs(decodedMaterial.baseColor - material.baseColor) <= float4(unormTolerance)) &&
        all(abs(decodedMaterial.dielectricF0 - material.dielectricF0) <= float3(unormTolerance)) &&
        abs(decodedMaterial.dielectricF90 - material.dielectricF90) <= unormTolerance &&
        all(decodedMaterial.emissive == material.emissive) &&
        decodedMaterial.layers.transmissionIor == material.layers.transmissionIor &&
        decodedMaterial.layers.transmissionMode == RtTransmissionMode.volume &&
        decodedMaterial.incidentMediumIor == 1.25f && decodedMaterial.exteriorMediumIor == 1.75f &&
        all(decodedScatter.diffuseWeight == scatter.diffuseWeight) &&
        all(decodedScatter.specularWeight == scatter.specularWeight) && decodedScatter.etaRatio == scatter.etaRatio);
    gContractResults[18] = verifyMaterialFilterPacketAdvance();

    gContractResults[19] = uint(abs(sheenDirectionalAlbedo(1.0f, 0.1f) - 0.7205024f) < 0.015f);
    gContractResults[20] = uint(abs(sheenDirectionalAlbedo(0.6f, 0.5f) - 0.2347469f) < 0.015f);
    gContractResults[21] = uint(abs(sheenDirectionalAlbedo(0.1f, 0.1f) - 0.5218497f) < 0.015f);
    gContractResults[22] = uint(abs(sheenDirectionalAlbedo(0.045f, 0.1f) - 0.2183071f) < 0.025f);

    ResolvedMaterialPayload zeroIorTransmission;
    zeroIorTransmission.layerFlags = RtMaterialLayerFlag(9u);
    zeroIorTransmission.baseColor = float4(1.0f);
    zeroIorTransmission.roughness = 0.5f;
    zeroIorTransmission.dielectricF0 = float3(0.25f);
    zeroIorTransmission.dielectricF90 = 0.5f;
    zeroIorTransmission.layers.transmission = 1.0f;
    zeroIorTransmission.layers.transmissionMode = RtTransmissionMode.thin;
    zeroIorTransmission.layers.transmissionIor = 0.0f;

    BaseSurfaceBsdfLobe<RtMaterialLayerFlag(9u)> transmissiveBase;
    float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    float3 transmissionDirection = -viewDirection;
    gContractResults[23] = uint(transmissiveBase.hasActiveTransmission(zeroIorTransmission));

    ResolvedMaterialBsdfComponents transmissionEvaluation =
        transmissiveBase.evaluate(zeroIorTransmission, viewDirection, transmissionDirection);
    float transmissionPdf = transmissiveBase.pdf(zeroIorTransmission, viewDirection, transmissionDirection);
    gContractResults[24] = uint(all(isfinite(transmissionEvaluation.specularProjected)) &&
                                any(transmissionEvaluation.specularProjected > float3(0.0f)) &&
                                isfinite(transmissionPdf) && transmissionPdf > 0.0f);

    ResolvedMaterialScatter transmissionScatter =
        transmissiveBase.sample(zeroIorTransmission, viewDirection, float3(0.9f, 0.0f, 0.0f));
    gContractResults[25] = uint(transmissionScatter.valid &&
                                transmissionScatter.kind == ResolvedMaterialScatterKind.transmission &&
                                dot(transmissionScatter.direction, transmissionDirection) > 0.999f &&
                                transmissionScatter.pdf > 0.0f);

    ResolvedMaterialPayload zeroIorVolume = zeroIorTransmission;
    zeroIorVolume.layers.transmissionMode = RtTransmissionMode.volume;
    float volumeDiffuseWeight;
    float volumeInterfaceWeight;
    transmissiveBase.selectionWeights(zeroIorVolume, viewDirection, volumeDiffuseWeight, volumeInterfaceWeight);
    gContractResults[26] = uint(!transmissiveBase.hasActiveTransmission(zeroIorVolume) &&
                                isfinite(volumeDiffuseWeight) && volumeDiffuseWeight > 0.0f &&
                                isfinite(volumeInterfaceWeight) && volumeInterfaceWeight > 0.0f);
}
