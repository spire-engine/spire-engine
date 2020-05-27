// This approximates the error function, needed for the gaussian integral
float4 erf(float4 x)
{
	float4 s = sign(x); float4 a = abs(x);
	x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
	x *= x;
	return s - s / (x * x);
}
// Return the mask for the shadow of a box from lower to upper
float boxShadow(float2 lower, float2 upper, float2 point, float sigma)
{
	float4 query = float4(point - lower, point - upper);
	float4 integral = 0.5 + 0.5 * erf(query * (sqrt(0.5) / sigma));
	return (integral.z - integral.x) * (integral.w - integral.y);
}

struct UberUIShaderParams
{
	float4x4 orthoMatrix;
	StructuredBuffer<uint4> uniformInput;
	StructuredBuffer<uint> textContent;
};

ParameterBlock<UberUIShaderParams> params;

struct VertexIn
{
    float2 vertPos : POSITION;
    float2 vertUV : TEXCOORD0;
	int primId : PRIMID;
};

struct VertexOut
{
    float4 projCoord : SV_POSITION;
    float2 vertUV : TEXCOORD;
	float2 vertPos : VERTPOS;
	int primId : PRIMID;
};

VertexOut vs_main(VertexIn vin)
{
	VertexOut rs;
	rs.projCoord = mul(params.orthoMatrix, float4(vin.vertPos, 0.0, 1.0));
	rs.vertUV = vin.vertUV;
	rs.primId = vin.primId;
	rs.vertPos = vin.vertPos;
	return rs;
}

float4 ps_main(VertexOut vin) : SV_Target
{
	float4 result = float4(0.0);
	uint4 params0 = params.uniformInput[int(vin.primId) * 2];
	uint4 params1 = params.uniformInput[int(vin.primId) * 2 + 1];
	int clipBoundX = int(params0.x & 65535);
	int clipBoundY = int(params0.x >> 16);
	int clipBoundX1 = int(params0.y & 65535);
	int clipBoundY1 = int(params0.y >> 16);
	int shaderType = int(params0.z);
	uint pColor = params0.w;
	float4 inputColor = float4(float(pColor&255), float((pColor>>8)&255), float((pColor>>16)&255), float((pColor>>24)&255)) * (1.0/255.0);
	float2 vert_pos = vin.vertPos;
	if (shaderType == 0) // solid color
	{
		if (vert_pos.x < clipBoundX) discard;
		if (vert_pos.y < clipBoundY) discard;
		if (vert_pos.x > clipBoundX1) discard;
		if (vert_pos.y > clipBoundY1) discard;
		result = inputColor;
	}
	else if (shaderType == 1) // text
	{
		if (vert_pos.x < clipBoundX) discard;
		if (vert_pos.y < clipBoundY) discard;
		if (vert_pos.x > clipBoundX1) discard;
		if (vert_pos.y > clipBoundY1) discard;
			
		int textWidth = int(params1.x);
		int textHeight = int(params1.y);
		int startAddr = int(params1.z);
			
		int2 fetchPos = int2(vin.vertUV * int2(textWidth, textHeight));
		int relAddr = fetchPos.y * textWidth + fetchPos.x;
		int ptr = startAddr + (relAddr >> 1);  // in bytes
		uint word = params.textContent[ptr >> 2];     // fetch the word at ptr
		uint ptrMod = (ptr & 3);
		word >>= (ptrMod<<3);                  // we are in the right byte now
		int bitPtr = relAddr & 1;              // two pixels per byte
		word >>= (bitPtr << 2);                // shift to get the correct half
		float alpha = float(word & 15) * (1.0/15.0);  // extract alpha
		result = inputColor;
		result.w *= alpha;
	}
	else if (shaderType == 2)
	{
		if (vert_pos.x > clipBoundX && vert_pos.x < clipBoundX1 && vert_pos.y > clipBoundY && vert_pos.y < clipBoundY1)
			discard;
		float2 origin; float2 size;
		origin.x = float(params1.x & 65535);
		origin.y = float(params1.x >> 16);
		size.x = float(params1.y & 65535);
		size.y = float(params1.y >> 16);
		float shadowSize = float(params1.z);
		float shadow = boxShadow(origin, origin+size, vert_pos, shadowSize);
		result = float4(inputColor.xyz, inputColor.w*shadow);
	}
	else
		discard;
					
	return result;
}