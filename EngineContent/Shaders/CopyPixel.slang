struct VertexIn
{
    float2 vertPos;
    float2 vertUV;
};

struct VertexOut
{
    float4 vertPos : SV_POSITION;
    float2 vertUV;
};

VertexOut vs_main(VertexIn vin)
{
    VertexOut rs;
    rs.vertPos = float4(vin.vertPos, 0.0, 1.0);
    rs.vertUV = vin.vertUV;
    return rs;
}
struct CopyParams
{
	Texture2D srcTex;
	SamplerState nearestSampler;
}
ParameterBlock<CopyParams> params;
float4 ps_main(VertexOut vin) 
{
    return params.srcTex.Sample(params.nearestSampler, vin.vertUV);
}