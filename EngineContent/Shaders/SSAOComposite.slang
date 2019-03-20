struct SSAOCompositeParams
{
    Texture2D<float> aoTexture;
    RWTexture2D<float4> outputTexture;
};

ParameterBlock<SSAOCompositeParams> params;

[numthreads(16, 16, 1)]
void cs_SSAOComposite(uint3 threadId : SV_DispatchThreadID)
{
    float4 src = params.outputTexture[threadId.xy];
    float ao = params.aoTexture.Load(threadId);
    params.outputTexture[threadId.xy] = src * ao;
}