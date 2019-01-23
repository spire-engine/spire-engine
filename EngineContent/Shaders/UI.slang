// This approximates the error function, needed for the gaussian integral
vec4 erf(vec4 x)
{
	vec4 s = sign(x); vec4 a = abs(x);
	x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
	x *= x;
	return s - s / (x * x);
}
// Return the mask for the shadow of a box from lower to upper
float boxShadow(vec2 lower, vec2 upper, vec2 point, float sigma)
{
	vec4 query = vec4(point - lower, point - upper);
	vec4 integral = 0.5 + 0.5 * erf(query * (sqrt(0.5) / sigma));
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
    float2 vertPos;
    float2 vertUV;
	int primId;
};

struct VertexOut
{
    float4 projCoord : SV_POSITION;
    float2 vertUV;
	nointerpolate int primId;
};

VertexOut vs_main(VertexIn vin)
{
	VertexOut rs;
	rs.projCoord = params.orthoMatrix * vec4(vin.vertPos, 0.0, 1.0);
	rs.vertUV = vin.vertUV;
	rs.primId = vin.primId;
	return rs;
}

float4 ps_main(VertexOut vin)
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
	vec4 inputColor = vec4(float(pColor&255), float((pColor>>8)&255), float((pColor>>16)&255), float((pColor>>24)&255)) * (1.0/255.0);
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
			
		ivec2 fetchPos = ivec2(vert_uv * ivec2(textWidth, textHeight));
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
		vec2 origin; vec2 size;
		origin.x = float(params1.x & 65535);
		origin.y = float(params1.x >> 16);
		size.x = float(params1.y & 65535);
		size.y = float(params1.y >> 16);
		float shadowSize = float(params1.z);
		float shadow = boxShadow(origin, origin+size, vert_pos, shadowSize);
		result = vec4(inputColor.xyz, inputColor.w*shadow);
	}
	else
		discard;
					
	return result;
}