module abi;
import common;

public struct NormalBufferPushConstants
{
    public float4 modelRow0;
    public float4 modelRow1;
    public float4 modelRow2;
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
    public float4 position : SV_Position;
    public float3 worldNormal : NORMAL;
    public float2 texCoord0 : TEXCOORD0;
    public float2 texCoord1 : TEXCOORD1;
    public float3 worldTangent : TEXCOORD2;
    public float tangentSign : TEXCOORD3;
};
