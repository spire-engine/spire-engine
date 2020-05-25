struct OutlinePassParams
{
    float4 borderColor;
    float2 pixelSize;
    float width;
    Texture2D srcColor;
    Texture2D srcDepth;
    SamplerState nearestSampler;
};
ParameterBlock<OutlinePassParams> params;

struct VertexIn
{
    float2 vertPos : POSITION;
    float2 vertUV : TEXCOORD0;
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

int getVal(float2 uv)
{
    float val = params.srcDepth.Sample(params.nearestSampler, uv).x;
    return val == 1.0 ? 1 : 0;
}
float4 ps_main(VertexOut vin) : SV_Target
{
    float4 src = params.srcColor.Sample(params.nearestSampler, vin.vertUV);
    int thisVal = getVal(vin.vertUV);
    float4 bColor = float4(0.0);
    if (thisVal)
    {
        if (getVal(vin.vertUV - float2(params.pixelSize.x, 0.0) * params.width) == 0 ||
            getVal(vin.vertUV - float2(0.0, params.pixelSize.y) * params.width) == 0 ||
            getVal(vin.vertUV + float2(params.pixelSize.x, 0.0) * params.width) == 0 ||
            getVal(vin.vertUV + float2(0.0, params.pixelSize.y) * params.width) == 0)
            bColor = params.borderColor;
    }
    return src * (1 - bColor.w) + bColor * bColor.w;
}