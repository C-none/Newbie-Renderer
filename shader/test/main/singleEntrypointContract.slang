module singleEntrypointContract;
import test.utils.utils;

[shader("compute")]
[numthreads(1, 1, 1)]
void csContract(uint3 tid : SV_DispatchThreadID, RWStructuredBuffer<uint> outBuf)
{
    uint value = kFlag + tid.x;
    outBuf[0] = value;
}
