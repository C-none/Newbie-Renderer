module fragment;
import common;
import renderer.normalBuffer.abi;

[shader("fragment")]
float4 fragmentMain(NormalBufferVertexOutput input) : SV_Target0
{
    float3 normal = normalize(input.worldNormal);
    uint normalTextureId = gPushConstants.normalTextureMeta.x;
    if (normalTextureId != 0u)
    {
        float3 tangent = normalize(input.worldTangent - normal * dot(normal, input.worldTangent));
        float3 bitangent = normalize(cross(normal, tangent) * input.tangentSign);
        float2 sourceUv = gPushConstants.normalTextureMeta.y == 1u ? input.texCoord1 : input.texCoord0;
        float2 normalUv = float2(
            dot(gPushConstants.normalUvLinear.xy, sourceUv),
            dot(gPushConstants.normalUvLinear.zw, sourceUv)) +
            gPushConstants.normalUvOffsetScale.xy;
        float3 tangentNormal = gSceneTextures[normalTextureId].Sample(normalUv).xyz * 2.0f - 1.0f;
        tangentNormal.xy *= gPushConstants.normalUvOffsetScale.z;
        normal = normalize(
            tangent * tangentNormal.x +
            bitangent * tangentNormal.y +
            normal * tangentNormal.z);
    }

    float3 encodedNormal = normal * 0.5f + 0.5f;
    return float4(encodedNormal, 1.0f);
}
