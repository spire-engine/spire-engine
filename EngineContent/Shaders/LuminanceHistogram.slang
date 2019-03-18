#define HISTOGRAM_SCALE 0.071f
#define HISTOGRAM_OFFSET 6.5f

uint LuminanceToHistogramIndex(float l, int histogramSize)
{
    return uint(saturate((log2(l) + HISTOGRAM_OFFSET) * HISTOGRAM_SCALE) * (histogramSize - 1));
}

float GetLuminanceAtHistogramIndex(int index, int histogramSize)
{
    return exp2((index / float(histogramSize - 1)) / HISTOGRAM_SCALE - HISTOGRAM_OFFSET);
}

float GetAverageLuminance(StructuredBuffer<uint> histogram, float minPercentile, float maxPercentile, int histogramSize, uint width, uint height)
{
    uint totalPixels = width * height;
    uint pixelCount = 0;
    uint minPercentileCount = uint(minPercentile * totalPixels);
    uint maxPercentileCount = uint(maxPercentile * totalPixels);
    int avgStartBin = -1;
    int avgEndBin = histogramSize - 1;
    for (int i = 0; i < histogramSize; i++)
    {
        uint newPixelCount = pixelCount + histogram[i];
        if (pixelCount > minPercentileCount)
        {
            if (avgStartBin == 0)
                avgStartBin = i;
        }
        if (pixelCount > maxPercentileCount)
        {
            avgEndBin = i - 1;
            break;
        }
    }
    avgStartBin = max(0, avgStartBin);
    avgEndBin = max(0, avgEndBin);
    uint sumPixels = 0;
    for (int i = avgStartBin; i <= avgEndBin; i++)
        sumPixels += histogram[i];
    float rs = 0.0f;
    float sumWeight = 0.0f;
    for (int i = avgStartBin; i <= avgEndBin; i++)
    {
        float weight = histogram[i] / float(sumPixels);
        rs += GetLuminanceAtHistogramIndex(i, histogramSize) * weight;
        sumWeight += weight;
    }
    return rs / sumWeight;
}