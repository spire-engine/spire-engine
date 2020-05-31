import ShaderLib;

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
    rs.vertUV = ProjCoordToUV(vin.vertUV);
    return rs;
}

struct PrefilterParams
{
	float4 origin;
	float4 s;
	float4 t;
	float4 r;
	float roughness;
	TextureCube envMap;
	SamplerState nearestSampler;
};
ParameterBlock<PrefilterParams> params;
float radicalInverse_VdC(uint bits)
{
	bits = (bits << 16) | (bits >> 16);
	bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
	bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
	bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
	bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 hammersley(uint i, uint N)
{
	return float2(float(i)/float(N), radicalInverse_VdC(i));
}

float3 importanceSampleGGX(float2 xi, float roughness, float3 N)
{
	float a = roughness * roughness;
	float phi = 2 * 3.1415926f * xi.x;
	float cosTheta = sqrt( (1-xi.y)/(1+(a*a-1)*xi.y));
	float sinTheta = sqrt(1-cosTheta*cosTheta);
	float3 H = float3(sinTheta * cos(phi), sinTheta*sin(phi), cosTheta);
	float3 upVector = abs(N.z) < 0.9 ? float3(0,0,1) : float3(1, 0 , 0);
	float3 tangentX = normalize(cross(upVector, N));
	float3 tangentY = cross(N, tangentX);
	return tangentX * H.x + tangentY * H.y + N * H.z;
}

float4 ps_main(VertexOut vin) : SV_Target
{
    float3 coord = params.origin.xyz + params.s.xyz * vin.vertUV.x * 2.0 + params.t.xyz * vin.vertUV.y * 2.0;
	float3 N = coord;
	float3 V = coord;
	float3 prefilteredColor = float3(0.0);
	int numSamples = 1024;
	float totalWeight = 0.001;
	for (int i = 0; i < numSamples; i++)
	{
		float2 xi = hammersley(uint(i), uint(numSamples));
		float3 h = importanceSampleGGX(xi, params.roughness, N);
		float3 L = h * (2.0*dot(V, h)) - V;
		float NoL = clamp(dot(N,L), 0.0f, 1.0f);
		if (NoL > 0)
		{
			prefilteredColor += params.envMap.Sample(params.nearestSampler, L).xyz * NoL;
			totalWeight += NoL;
		}            
	}
	return float4(prefilteredColor / totalWeight, 1.0);
}