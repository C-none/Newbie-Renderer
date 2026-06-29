module normalBuffer;
import common;

struct NormalBufferPushConstants
{
    float4 modelRow0;
    float4 modelRow1;
    float4 modelRow2;
};

[[vk::push_constant]]
NormalBufferPushConstants gPushConstants;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
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
    
    VSOutput output;
    output.position = mul(gFrame.viewProjection, worldPosition);
    output.worldNormal = worldNormal;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    // Normalize the interpolated normal
    float3 normal = normalize(input.worldNormal);
    
    // Remap normal from [-1, 1] to [0, 1] for visualization
    float3 encodedNormal = normal * 0.5f + 0.5f;
    
    return float4(encodedNormal, 1.0f);
}
