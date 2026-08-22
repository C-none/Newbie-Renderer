module width32FloatE4M3;
import common;
import include.coopVecBench.mlp;

[[vk::binding(0, 0)]]
ByteAddressBuffer gParameters;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> gResults;

[[vk::push_constant]]
ConstantBuffer<CoopVecBenchPushConstants> gBench;

[shader("compute")]
[require(spvCooperativeVectorNV, spvFloat8EXT)]
[numthreads(64, 1, 1)]
void width32FloatE4M3Main(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    coopVecBenchRun<32>(gParameters, gResults, gBench, dispatchThreadID.x, CoopVecComponentType.FloatE4M3);
}
