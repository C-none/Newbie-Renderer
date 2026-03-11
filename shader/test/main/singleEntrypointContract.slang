module singleEntrypointContract;
import test.utils.utils;

struct PushConstantData
{
    uint bias;
};

[[vk::push_constant]]
ConstantBuffer<PushConstantData> pushData;
RWStructuredBuffer<uint> outBuf;

[shader("compute")]
[numthreads(1, 1, 1)]
void csContract(uint3 tid : SV_DispatchThreadID)
{
    uint value = kFlag + tid.x + pushData.bias;
    outBuf[0] = value;
}
