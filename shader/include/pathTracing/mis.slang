implementing common;

public float pathTracingMisWeightedPdf(uint sampleCount, float pdf)
{
    if (sampleCount == 0u || pdf <= 0.0f)
    {
        return 0.0f;
    }

    return float(sampleCount) * pdf;
}

public float pathTracingMisBalanceHeuristicFromWeights(float selectedWeight, float otherWeight)
{
    float sum = selectedWeight + otherWeight;
    if (sum <= 0.0f)
    {
        return 0.0f;
    }

    return selectedWeight / sum;
}

public float pathTracingMisBalanceHeuristic(float selectedPdf, float otherPdf)
{
    return pathTracingMisBalanceHeuristicFromWeights(
        pathTracingMisWeightedPdf(1u, selectedPdf),
        pathTracingMisWeightedPdf(1u, otherPdf));
}

public float pathTracingMisBalanceHeuristic(
    uint selectedSampleCount,
    float selectedPdf,
    uint otherSampleCount,
    float otherPdf)
{
    return pathTracingMisBalanceHeuristicFromWeights(
        pathTracingMisWeightedPdf(selectedSampleCount, selectedPdf),
        pathTracingMisWeightedPdf(otherSampleCount, otherPdf));
}

public float pathTracingMisPowerHeuristicFromWeights(float selectedWeight, float otherWeight)
{
    float selectedWeight2 = selectedWeight * selectedWeight;
    float otherWeight2 = otherWeight * otherWeight;
    float sum = selectedWeight2 + otherWeight2;
    if (sum <= 0.0f)
    {
        return 0.0f;
    }

    return selectedWeight2 / sum;
}

public float pathTracingMisPowerHeuristic(float selectedPdf, float otherPdf)
{
    return pathTracingMisPowerHeuristicFromWeights(
        pathTracingMisWeightedPdf(1u, selectedPdf),
        pathTracingMisWeightedPdf(1u, otherPdf));
}

public float pathTracingMisPowerHeuristic(
    uint selectedSampleCount,
    float selectedPdf,
    uint otherSampleCount,
    float otherPdf)
{
    return pathTracingMisPowerHeuristicFromWeights(
        pathTracingMisWeightedPdf(selectedSampleCount, selectedPdf),
        pathTracingMisWeightedPdf(otherSampleCount, otherPdf));
}
