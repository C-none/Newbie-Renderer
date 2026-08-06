module bindlessTwoTableContract;

[[vk::binding(0, 1)]]
Sampler2D<float4> gTableA[];

[[vk::binding(0, 2)]]
RWTexture2D<float4> gTableB[];

[shader("compute")]
[numthreads(2, 2, 1)]
void bindlessTwoTableMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    gTableB[0][int2(dispatchThreadID.xy)] = gTableA[0].SampleLevel(float2(0.25f, 0.25f), 0.0f);
}
