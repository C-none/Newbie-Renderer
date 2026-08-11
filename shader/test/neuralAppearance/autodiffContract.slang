module autodiffContract;
import common;

struct NeuralAppearanceAutodiffProbe : IDifferentiable
{
    float values[4] = {};

    [Differentiable]
    __init()
    {
    }

    [Differentiable]
    __init(float value0, float value1, float value2, float value3)
    {
        values[0] = value0;
        values[1] = value1;
        values[2] = value2;
        values[3] = value3;
    }
};

[Differentiable]
float neuralAppearanceAutodiffProbeLoss(NeuralAppearanceAutodiffProbe probe)
{
    return probe.values[0] * probe.values[1] + probe.values[2] * probe.values[3];
}

[[vk::binding(0, 2)]]
RWStructuredBuffer<float> gAutodiffResults;

[shader("compute")]
[numthreads(1, 1, 1)]
void autodiffContractMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    NeuralAppearanceAutodiffProbe probe =
        NeuralAppearanceAutodiffProbe(2.0f, 3.0f, 5.0f, 7.0f);

    DifferentialPair<NeuralAppearanceAutodiffProbe> pair = diffPair(probe);
    bwd_diff(neuralAppearanceAutodiffProbeLoss)(pair, 1.0f);

    gAutodiffResults[0] = pair.d.values[0];
    gAutodiffResults[1] = pair.d.values[1];
    gAutodiffResults[2] = pair.d.values[2];
    gAutodiffResults[3] = pair.d.values[3];
}
