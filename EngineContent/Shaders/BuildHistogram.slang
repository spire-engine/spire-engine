import LuminanceHistogram;

struct HDRHistogramParams
{
    int histogramSize;
    int width, height;
    Texture2D inputTexture;
    RWStructuredBuffer<uint> result;
};
groupshared uint localHistogram[256];
ParameterBlock<HDRHistogramParams> params;

[numthreads(16, 16, 1)]
void cs_BuildHistogram(uint3 threadId : SV_DispatchThreadID, uint localId : SV_GroupIndex)
{
    if (localId < params.histogramSize)
        localHistogram[localId] = 0.0f;

    GroupMemoryBarrierWithGroupSync();

    if (threadId.x < params.width && threadId.y < params.height)
    {
        float3 data = params.inputTexture.Load(threadId).xyz;
        float l = 0.2126f*data.r + 0.7152f*data.g + 0.0722f*data.b;
        uint index = LuminanceToHistogramIndex(l, params.histogramSize);
        InterlockedAdd(localHistogram[index], 1);
    }
    GroupMemoryBarrierWithGroupSync();

    if (localId < params.histogramSize)
        InterlockedAdd(params.result[localId], localHistogram[localId]);
}