module abi;
import common;

public struct NormalBufferPushConstants
{
    public row_major float4x3 model;
    public float4 normalUvLinear;
    public float4 normalUvOffsetScale;
    public uint4 normalTextureMeta;
};

[[vk::push_constant]]
public NormalBufferPushConstants gPushConstants;

public struct NormalBufferVertexInput
{
    public float3 position : POSITION;
    public float3 normal : NORMAL;
    public float4 tangent : TANGENT;
    public float2 texCoord0 : TEXCOORD0;
    public float2 texCoord1 : TEXCOORD1;
};

public struct NormalBufferVertexOutput
{
    public float4 position : SV_Position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    public float3 worldNormal : NORMAL = float3(0.0f, 0.0f, 1.0f);
    public float2 texCoord0 : TEXCOORD0 = float2(0.0f, 0.0f);
    public float2 texCoord1 : TEXCOORD1 = float2(0.0f, 0.0f);
    public float3 worldTangent : TEXCOORD2 = float3(1.0f, 0.0f, 0.0f);
    public float tangentSign : TEXCOORD3 = 1.0f;

    public __init(float4 positionValue, float3 worldNormalValue, float2 texCoord0Value, float2 texCoord1Value,
                  float3 worldTangentValue, float tangentSignValue)
    {
        position = positionValue;
        worldNormal = worldNormalValue;
        texCoord0 = texCoord0Value;
        texCoord1 = texCoord1Value;
        worldTangent = worldTangentValue;
        tangentSign = tangentSignValue;
    }
};
