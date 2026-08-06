module vertex;
import common;
import renderer.normalBuffer.abi;

float3x3 inverseTranspose(float3x3 value, float determinant)
{
    float3 cofactorRow0 = cross(value[1], value[2]);
    float3 cofactorRow1 = cross(value[2], value[0]);
    float3 cofactorRow2 = cross(value[0], value[1]);
    return float3x3(cofactorRow0, cofactorRow1, cofactorRow2) * (1.0f / determinant);
}

[shader("vertex")]
NormalBufferVertexOutput vertexMain(NormalBufferVertexInput input)
{
    float4 localPosition = float4(input.position, 1.0f);
    float4 worldPosition =
        float4(dot(gPushConstants.modelRow0, localPosition), dot(gPushConstants.modelRow1, localPosition),
               dot(gPushConstants.modelRow2, localPosition), 1.0f);

    float3x3 modelLinear =
        float3x3(gPushConstants.modelRow0.xyz, gPushConstants.modelRow1.xyz, gPushConstants.modelRow2.xyz);
    float modelLinearDeterminant = determinant(modelLinear);
    float modelHandedness = modelLinearDeterminant < 0.0f ? -1.0f : 1.0f;
    float3x3 normalMatrix = inverseTranspose(modelLinear, modelLinearDeterminant);
    float3 worldNormal = normalize(mul(normalMatrix, input.normal));
    float3 worldTangent = normalize(mul(modelLinear, input.tangent.xyz));

    NormalBufferVertexOutput output;
    output.position = mul(gFrame.viewProjection, worldPosition);
    output.worldNormal = worldNormal;
    output.worldTangent = worldTangent;
    output.tangentSign = input.tangent.w * modelHandedness;
    output.texCoord0 = input.texCoord0;
    output.texCoord1 = input.texCoord1;
    return output;
}
