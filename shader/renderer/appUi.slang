module appUi;
import common;

struct UiPushConstants
{
    float2 scale;
    float2 translate;
    uint textureIndex;
    uint3 padding;
};

[[vk::binding(0, 1)]]
Sampler2D<float4> gUiTextures[];

[[vk::push_constant]]
ConstantBuffer<UiPushConstants> gUiPush;

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position * gUiPush.scale + gUiPush.translate, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    return input.color * gUiTextures[gUiPush.textureIndex].Sample(input.uv);
}
