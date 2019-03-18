struct ClearHistogramParams
{
    int histogramSize;
    RWStructuredBuffer<uint> result;
};
ParameterBlock<ClearHistogramParams> params;
[numthreads(128, 1, 1)]
void cs_ClearHistogram(uint3 threadId : SV_DispatchThreadID)
{
    if (threadId.x < params.histogramSize)
        params.result[threadId.x] = 0;
}
