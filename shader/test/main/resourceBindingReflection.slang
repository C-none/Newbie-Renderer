module resourceBindingReflection;

struct CbData
{
    uint bias;
    float scale;
};

[[vk::push_constant]]
ConstantBuffer<CbData> pushData;

ConstantBuffer<CbData> cbData;
Texture2D<float4> tex2d;
RWTexture2D<float4> rwTex2d;
SamplerState linearSampler;
Buffer<float4> typedBuffer;
RWBuffer<float4> rwTypedBuffer;
ByteAddressBuffer rawBuffer;
RWByteAddressBuffer rwRawBuffer;
RWStructuredBuffer<uint> outBuf;

[shader("compute")]
[numthreads(1, 1, 1)]
void csBindings(uint3 tid : SV_DispatchThreadID)
{
    float4 sampled = tex2d.SampleLevel(linearSampler, float2(0.5, 0.5), 0.0);
    float4 typedValue = typedBuffer[0] + sampled;
    rwTypedBuffer[0] = typedValue;

    uint rawValue = rawBuffer.Load(0);
    rwRawBuffer.Store(0, rawValue + cbData.bias + pushData.bias + tid.x);
    rwTex2d[int2(0, 0)] = rwTypedBuffer[0] * cbData.scale;

    outBuf[0] = asuint(rwTypedBuffer[0].x) + rawValue;
}
