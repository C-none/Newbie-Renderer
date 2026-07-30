module normalBuffer;
import common;

struct NormalBufferPushConstants
{
    float4 modelRow0;
    float4 modelRow1;
    float4 modelRow2;
    float4 normalUvLinear;
    float4 normalUvOffsetScale;
    uint4 normalTextureMeta;
};

[[vk::push_constant]]
NormalBufferPushConstants gPushConstants;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord0 : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 texCoord0 : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
    float3 worldTangent : TEXCOORD2;
    float tangentSign : TEXCOORD3;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    float4 localPosition = float4(input.position, 1.0f);
    float4 worldPosition = float4(
        dot(gPushConstants.modelRow0, localPosition),
        dot(gPushConstants.modelRow1, localPosition),
        dot(gPushConstants.modelRow2, localPosition),
        1.0f);

    float3x3 modelMatrix3x3 = float3x3(
        gPushConstants.modelRow0.xyz,
        gPushConstants.modelRow1.xyz,
        gPushConstants.modelRow2.xyz
    );
    float3x3 normalMatrix = transpose(modelMatrix3x3);
    float3 worldNormal = normalize(mul(normalMatrix, input.normal));
    float3 worldTangent = normalize(mul(normalMatrix, input.tangent.xyz));
    
    VSOutput output;
    output.position = mul(gFrame.viewProjection, worldPosition);
    output.worldNormal = worldNormal;
    output.worldTangent = worldTangent;
    output.tangentSign = input.tangent.w;
    output.texCoord0 = input.texCoord0;
    output.texCoord1 = input.texCoord1;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
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
