struct ToneMappingPassParams
{
    float4 hdrExposure;
    Texture2D litColor;
    Texture3D colorLUT;
    SamplerState nearestSampler;
    SamplerState linearSampler;
    StructuredBuffer<float> adaptedLuminance;
};

ParameterBlock<ToneMappingPassParams> params;

static const float3x3 ACESInputMat = float3x3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
    );

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat = float3x3(
    1.60475, -0.53108, -0.07367,
    -0.10208, 1.10813, -0.00605,
    -0.00327, -0.07276, 1.07602
    );

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 transform(float3x3 m, float3 v)
{
    return float3(dot(m[0], v), dot(m[1], v), dot(m[2], v));
}

float3 ACESFitted(float3 color)
{
    color = transform(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = transform(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = clamp(color, float3(0.0), float3(1.0));

    return color;
}


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

float4 ps_main(VertexOut vin)
{
    float adaptedIllum = params.adaptedLuminance[0];
    float3 src = params.litColor.Sample(params.nearestSampler, vin.vertUV).xyz;
    float3 ldr = ACESFitted(src * params.hdrExposure.x / adaptedIllum);
    float3 mapped = params.colorLUT.Sample(params.linearSampler, ldr).xyz;
    return float4(mapped, 1.0);
}