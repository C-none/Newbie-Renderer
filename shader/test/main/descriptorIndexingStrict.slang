module descriptorIndexingStrict;

SamplerState linearSampler;
Texture2D<float4> colorTextures[4];
RWStructuredBuffer<uint> outBuf;

[shader("compute")]
[numthreads(1, 1, 1)]
void csDescriptorArray(uint3 tid : SV_DispatchThreadID)
{
    float4 sampled = colorTextures[0].SampleLevel(linearSampler, float2(0.5, 0.5), 0.0);
    outBuf[0] = asuint(sampled.x) + tid.x;
}
