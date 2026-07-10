implementing common;

public float misWeightedPdf(uint sampleCount, float pdf)
{
    if (sampleCount == 0u || pdf <= 0.0f)
    {
        return 0.0f;
    }

    return float(sampleCount) * pdf;
}

public float misBalanceHeuristicFromWeights(float selectedWeight, float otherWeight)
{
    float sum = selectedWeight + otherWeight;
    if (sum <= 0.0f)
    {
        return 0.0f;
    }

    return selectedWeight / sum;
}

public float misBalanceHeuristic(float selectedPdf, float otherPdf)
{
    return misBalanceHeuristicFromWeights(
        misWeightedPdf(1u, selectedPdf),
        misWeightedPdf(1u, otherPdf));
}

public float misBalanceHeuristic(
    uint selectedSampleCount,
    float selectedPdf,
    uint otherSampleCount,
    float otherPdf)
{
    return misBalanceHeuristicFromWeights(
        misWeightedPdf(selectedSampleCount, selectedPdf),
        misWeightedPdf(otherSampleCount, otherPdf));
}

public float misPowerHeuristicFromWeights(float selectedWeight, float otherWeight)
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

public float misPowerHeuristic(float selectedPdf, float otherPdf)
{
    return misPowerHeuristicFromWeights(
        misWeightedPdf(1u, selectedPdf),
        misWeightedPdf(1u, otherPdf));
}

public float misPowerHeuristic(
    uint selectedSampleCount,
    float selectedPdf,
    uint otherSampleCount,
    float otherPdf)
{
    return misPowerHeuristicFromWeights(
        misWeightedPdf(selectedSampleCount, selectedPdf),
        misWeightedPdf(otherSampleCount, otherPdf));
}
