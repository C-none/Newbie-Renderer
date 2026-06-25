module descriptorConflict;
import common;

RWStructuredBuffer<uint> sharedBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void csConflict(uint3 tid : SV_DispatchThreadID, RWStructuredBuffer<uint> sharedBuffer)
{
    sharedBuffer[0] = tid.x;
}
