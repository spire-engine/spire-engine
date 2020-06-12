struct SSAOCompositeParams
{
    Texture2D<float> aoTexture;
    RWTexture2D<float4> outputTexture;
    SamplerState samplerState;
};

ParameterBlock<SSAOCompositeParams> params;

[numthreads(16, 16, 1)]
void cs_SSAOComposite(uint3 threadId : SV_DispatchThreadID)
{
    uint w, h;
    params.outputTexture.GetDimensions(w, h);
    float2 scale = float2(1.0/w, 1.0/h);
    float4 src = params.outputTexture[threadId.xy];
    float ao = params.aoTexture.SampleLevel(params.samplerState, threadId.xy*scale, 0, 0);
    params.outputTexture[threadId.xy] = src * ao;
}