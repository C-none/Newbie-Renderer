module vertex;
import common;
import renderer.normalBuffer.abi;

float3x3 rowVectorInverseTranspose(float3x3 value, float determinantValue)
{
    // For p' = p * L, normals use n' = n * transpose(inverse(L)) to remain perpendicular to transformed tangents.
    float3 cofactorRow0 = cross(value[1], value[2]);
    float3 cofactorRow1 = cross(value[2], value[0]);
    float3 cofactorRow2 = cross(value[0], value[1]);
    return float3x3(cofactorRow0, cofactorRow1, cofactorRow2) * (1.0f / determinantValue);
}

[shader("vertex")]
NormalBufferVertexOutput vertexMain(NormalBufferVertexInput input)
{
    float3 worldPosition = mul(float4(input.position, 1.0f), gPushConstants.model);
    float3x3 modelLinear = float3x3(gPushConstants.model[0], gPushConstants.model[1], gPushConstants.model[2]);
    float modelLinearDeterminant = determinant(modelLinear);
    float modelHandedness = modelLinearDeterminant < 0.0f ? -1.0f : 1.0f;
    float3x3 normalMatrix = rowVectorInverseTranspose(modelLinear, modelLinearDeterminant);
    float3 worldNormal = normalize(mul(input.normal, normalMatrix));
    float3 worldTangent = normalize(mul(input.tangent.xyz, modelLinear));

    return NormalBufferVertexOutput(mul(float4(worldPosition, 1.0f), gFrame.viewProjection), worldNormal,
                                    input.texCoord0, input.texCoord1, worldTangent,
                                    input.tangent.w * modelHandedness);
}
