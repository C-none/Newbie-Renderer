module abi;

public struct UiPushConstants
{
    public float2 scale;
    public float2 translate;
    public uint textureIndex;
    public uint3 padding;
};

[[vk::binding(0, 1)]]
public Sampler2D<float4> gUiTextures[];

[[vk::push_constant]]
public ConstantBuffer<UiPushConstants> gUiPush;

public struct UiVertexInput
{
    public float2 position : POSITION;
    public float2 uv : TEXCOORD0;
    public float4 color : COLOR0;
};

public struct UiVertexOutput
{
    public float4 position : SV_Position;
    public float2 uv : TEXCOORD0;
    public float4 color : COLOR0;
};
