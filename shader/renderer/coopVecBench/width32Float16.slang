module width32Float16;
import common;
import include.coopVecBench.mlp;

[[vk::binding(0, 0)]]
ByteAddressBuffer gParameters;

[[vk::binding(1, 0)]]
RWStructuredBuffer<float> gResults;

[[vk::push_constant]]
ConstantBuffer<CoopVecBenchPushConstants> gBench;

[shader("compute")]
[require(spvCooperativeVectorNV)]
[numthreads(64, 1, 1)]
void width32Float16Main(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    coopVecBenchRun<32>(gParameters, gResults, gBench, dispatchThreadID.x, CoopVecComponentType.Float16);
}
