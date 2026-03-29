module normalBuffer;

struct NormalBufferFrameUniforms
{
    column_major float4x4 viewProjection;
};

struct NormalBufferDrawPushConstants
{
    column_major float4x4 model;
    uint objectId;
    uint3 padding;
};

ConstantBuffer<NormalBufferFrameUniforms> gFrame;
[[vk::push_constant]]
ConstantBuffer<NormalBufferDrawPushConstants> gDraw;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    float4 worldPosition = mul(gDraw.model, float4(input.position, 1.0f));

    float3x3 normalMatrix = (float3x3)gDraw.model;
    float3 worldNormal = normalize(mul(normalMatrix, input.normal));

    VSOutput output;
    output.position = mul(gFrame.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float3 normal = input.worldNormal;
    if (dot(normal, normal) < 1.0e-8f)
    {
        float3 dpdx = ddx(input.worldPosition);
        float3 dpdy = ddy(input.worldPosition);
        normal = cross(dpdx, dpdy);
    }

    float3 encoded = normalize(normal) * 0.5f + 0.5f;
    return float4(encoded, 1.0f);
}
