module vertex;
import common;
import renderer.normalBuffer.abi;

[shader("vertex")]
NormalBufferVertexOutput vertexMain(NormalBufferVertexInput input)
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

    NormalBufferVertexOutput output;
    output.position = mul(gFrame.viewProjection, worldPosition);
    output.worldNormal = worldNormal;
    output.worldTangent = worldTangent;
    output.tangentSign = input.tangent.w;
    output.texCoord0 = input.texCoord0;
    output.texCoord1 = input.texCoord1;
    return output;
}
