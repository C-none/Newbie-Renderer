struct NormalViewPushConstants
{
    float4x4 viewProjection;
    float4 cameraWorldAndDrawCount;
    uint4 drawMeta;
    float4 worldTranslationAndPadding;
};

[[vk::push_constant]]
ConstantBuffer<NormalViewPushConstants> gFrame;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };

    uint drawCount = max((uint)gFrame.cameraWorldAndDrawCount.w, 1u);
    float lane = (float(gFrame.drawMeta.x) + 0.5f) / float(drawCount);
    float jitter = (float((gFrame.drawMeta.y ^ (gFrame.drawMeta.z * 1664525u)) & 1023u) / 1023.0f) * 0.2f - 0.1f;
    float2 center = float2(lerp(-0.8f, 0.8f, lane), jitter + gFrame.worldTranslationAndPadding.y * 0.02f);
    float2 position = positions[vertexId] * float2(0.25f / float(drawCount), 0.35f) + center;

    VSOutput output;
    output.position = float4(position, 0.0, 1.0);
    float3 worldSeed = normalize(abs(gFrame.worldTranslationAndPadding.xyz) + 0.001f);
    output.normal = normalize(float3(position, 1.0) + worldSeed * 0.35f);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float3 encoded = normalize(input.normal) * 0.5 + 0.5;
    float bindlessMix = frac(float(gFrame.drawMeta.y * 31u + gFrame.drawMeta.z * 17u) * 0.0001f);
    float drawLane = float(gFrame.drawMeta.x + 1u) / max(gFrame.cameraWorldAndDrawCount.w, 1.0f);
    float cameraMix = saturate(length(gFrame.cameraWorldAndDrawCount.xyz) * 0.02f);
    float3 palette = float3(bindlessMix, drawLane, cameraMix);
    encoded = lerp(encoded, encoded.zxy, saturate(abs(gFrame.viewProjection[0][0]) * 0.08f));
    encoded = lerp(encoded, palette, 0.35f);
    return float4(encoded, 1.0);
}
