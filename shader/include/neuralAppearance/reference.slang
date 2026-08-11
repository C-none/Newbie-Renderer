module reference;
import common;
import include.neuralAppearance.fixture;

public ResolvedMaterialPayload neuralAppearanceFixtureMaterial(float2 uv)
{
    NeuralAppearanceFixture fixture = neuralAppearanceFixture(uv);

    ResolvedMaterialPayload material = {};
    material.layerFlags = RtMaterialLayerFlag.baseSurface | RtMaterialLayerFlag.anisotropicBaseLobe;
    material.geometryNormal = fixture.geometryNormal;
    material.shadingNormal = fixture.shadingNormal;
    material.anisotropyTangent = fixture.anisotropyTangent;
    material.anisotropyStrength = fixture.anisotropyStrength;
    material.baseColor = float4(saturate(fixture.f0), 1.0f);
    material.metallic = 1.0f;
    material.roughness = max(kMaterialMinRoughness, saturate(fixture.roughness));
    return material;
}

public float3 neuralAppearanceReferenceSpecularProjected(
    float2 uv, float3 viewDirection, float3 lightDirection)
{
    ResolvedMaterialPayload material = neuralAppearanceFixtureMaterial(uv);
    BaseSurfaceBsdfLobe<
        RtMaterialLayerFlag.baseSurface | RtMaterialLayerFlag.anisotropicBaseLobe> baseSurface;
    return baseSurface.evaluate(material, viewDirection, lightDirection).specularProjected;
}
