module normalBuffer;
import common;

struct NormalBufferPushConstants
{
    column_major float4x4 model;
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
    float4 worldPosition = mul(gPushConstants.model, float4(input.position, 1.0f));

    float3x3 modelMatrix3x3 = float3x3(
        gPushConstants.model[0].xyz,
        gPushConstants.model[1].xyz,
        gPushConstants.model[2].xyz
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
