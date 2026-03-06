module descriptorMerge;

StructuredBuffer<uint> globalIn;
RWStructuredBuffer<uint> globalOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void csMerge(uint3 tid : SV_DispatchThreadID, RWStructuredBuffer<uint> entryOut)
{
    uint value = globalIn[0] + tid.x;
    entryOut[0] = value;
    globalOut[0] = value;
}
