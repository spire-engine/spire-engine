import LuminanceHistogram;

struct EyeAdaptationParams
{
    int histogramSize;
    int width, height;
    int frameId;
    float2 adaptSpeed;  // x = up, y = down
    float deltaTime;
    float minLuminance, maxLuminance;
    StructuredBuffer<uint> histogram;
    RWStructuredBuffer<float> adaptedLuminance;
};

ParameterBlock<EyeAdaptationParams> params;

[numthreads(1, 1, 1)]
void cs_EyeAdaptation( uint3 DTid : SV_DispatchThreadID )
{
    float avgLuminance = GetAverageLuminance(params.histogram, 0.8, 0.95, params.histogramSize, params.width, params.height);
    if (params.frameId == 0)
        params.adaptedLuminance[0] = avgLuminance;
    else
    {
        float lastLuminance = params.adaptedLuminance[0];
        float speed = avgLuminance > lastLuminance ? params.adaptSpeed.x : params.adaptSpeed.y;
        params.adaptedLuminance[0] = clamp(lastLuminance + (avgLuminance - lastLuminance) * (1.0f - exp(-params.deltaTime * speed)), params.minLuminance, params.maxLuminance);
    }
}