module twoComputeReflection;

RWStructuredBuffer<uint> buffer1;
RWStructuredBuffer<uint> buffer2;

[shader("compute")]
[numthreads(1, 1, 1)]
void csShader1(uint3 tid : SV_DispatchThreadID)
{
    uint value = buffer1[0] + tid.x;
    buffer1[0] = value;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void csShader2(uint3 tid : SV_DispatchThreadID)
{
    uint value = buffer2[0] + tid.x;
    buffer2[0] = value;
}
