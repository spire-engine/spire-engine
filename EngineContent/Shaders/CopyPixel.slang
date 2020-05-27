struct VertexIn
{
    float2 vertPos : POSITION;
    float2 vertUV : TEXCOORD0;
};

struct VertexOut
{
    float4 vertPos : SV_POSITION;
    float2 vertUV : TEXCOORD0;
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
float4 ps_main(VertexOut vin) : SV_Target
{
    float4 rs = params.srcTex.Sample(params.nearestSampler, vin.vertUV);
    if (isnan(rs.x) || rs.x < 0.0 || isnan(rs.y) || rs.y < 0.0f || isnan(rs.z) || rs.z < 0.0f)
        return float4(0.0);
    else
        return rs;
}