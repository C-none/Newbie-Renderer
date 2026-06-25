module descriptorMerge;
import common;

StructuredBuffer<uint> globalIn;
RWStructuredBuffer<uint> globalOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void csMerge(uint3 tid : SV_DispatchThreadID)
{
    uint value = globalIn[0] + tid.x;
    globalOut[0] = value;
}
