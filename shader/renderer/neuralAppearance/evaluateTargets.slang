module evaluateTargets;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.reference;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceTargetPushConstants
{
    uint trainingStep;
    uint batchSize;
    uint trainingSeed;
};

// Two records per sample: the projected diffuse lobe followed by the projected
// specular lobe. Both are pure reflection because the reference template omits
// the transmission layer flag.
[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gSampleTargets;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceTargetPushConstants> gTraining;

// A fixed workgroup width keeps the batch size a host-side dispatch decision
// rather than a compile-time workgroup constraint.
[shader("compute")]
[numthreads(64, 1, 1)]
void evaluateTargetsMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint sampleIndex = dispatchThreadID.x;
    uint batchSize = min(gTraining.batchSize, kNeuralAppearanceBatchSize);
    if (sampleIndex >= batchSize)
    {
        return;
    }

    NeuralAppearanceTrainingSample sample = neuralAppearanceTrainingSample(
        gTraining.trainingStep, sampleIndex, gTraining.trainingSeed);
    NeuralAppearanceLobes target = neuralAppearanceReferenceLobes(
        neuralAppearanceFixtureSample(sample.materialSeed),
        sample.directions.viewDirection, sample.directions.lightDirection);

    gSampleTargets[sampleIndex * 2u + 0u] = float4(target.diffuse, 0.0f);
    gSampleTargets[sampleIndex * 2u + 1u] = float4(target.specular, 0.0f);
}
