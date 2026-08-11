module sceneLightReflection;
import common;

[[vk::binding(8, 3)]]
RWStructuredBuffer<uint> outputValues;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId: SV_DispatchThreadID)
{
    uint value = gSceneLightHeader.lightCount + dispatchThreadId.x;
    if (gSceneLightHeader.lightCount > 0u)
    {
        value += gSceneLights[0u].meta.x;
        value += gSceneLightAliasTable[0u].meta.x;
        SceneLightContribution contribution =
            SceneLightContribution(float3(0.0f, 1.0f, 0.0f), float3(0.0f));
        if (evaluateSceneLightAt(0u, float3(0.0f), contribution))
        {
            value += contribution.radiance.x > 0.0f ? 1u : 0u;
        }
        SceneLightSample sample = SceneLightSample(0u, 0.0f);
        if (sampleSceneLightAlias(float2(0.0f, 0.0f), sample))
        {
            value += sample.pdf > 0.0f ? 1u : 0u;
        }
    }
    outputValues[0u] = value;
}
