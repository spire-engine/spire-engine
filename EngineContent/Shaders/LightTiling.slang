import ShaderLib;

struct ClassifyLightTilesParam
{
	float4x4 invProjMatrix;
	float4x4 viewMatrix;
	int width, height;
	int lightCount, lightProbeCount;
	Texture2D<float> depthTexture;
	StructuredBuffer<Light> lights;
    StructuredBuffer<LightProbe> lightProbes;
	RWStructuredBuffer<uint> tiles;
};

#define TILE_RES_X 16
#define TILE_RES_Y 16
#define THREAD_GROUP_SIZE (TILE_RES_X*TILE_RES_Y)
#define LIGHT_LIST_MAX_SIZE 128

groupshared int lightIndexPtr;
groupshared uint lightList[LIGHT_LIST_MAX_SIZE];
groupshared uint sharedZMax;

ParameterBlock<ClassifyLightTilesParam> params;

[numthreads(TILE_RES_X, TILE_RES_Y, 1)]
void cs_BuildTiledLightList(uint3 globalId : SV_DispatchThreadID, uint3 localId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint localThreadIndex = localId.y * TILE_RES_X + localId.x;
    if (localThreadIndex == 0)
    {
        lightIndexPtr = 0;
		sharedZMax = 0.0;
    }

	// step 1: compute frustum side planes

    uint tx0 = TILE_RES_X * groupId.x;
    uint ty0 = TILE_RES_Y * groupId.y;
    uint tx1 = TILE_RES_X * (groupId.x + 1);
    uint ty1 = TILE_RES_Y * (groupId.y + 1);

	float4 corner0 = mul(params.invProjMatrix, float4(float2(tx0/(float)params.width, ty0/(float)params.height) * 2.0f - 1.0f, 1.0f, 1.0f));
	float4 corner1 = mul(params.invProjMatrix, float4(float2(tx1/(float)params.width, ty0/(float)params.height) * 2.0f - 1.0f, 1.0f, 1.0f));
	float4 corner2 = mul(params.invProjMatrix, float4(float2(tx1/(float)params.width, ty1/(float)params.height) * 2.0f - 1.0f, 1.0f, 1.0f));
	float4 corner3 = mul(params.invProjMatrix, float4(float2(tx0/(float)params.width, ty1/(float)params.height) * 2.0f - 1.0f, 1.0f, 1.0f));

	corner0 = float4(corner0.xyz/corner0.w, 1.0);
	corner1 = float4(corner1.xyz/corner1.w, 1.0);
	corner2 = float4(corner2.xyz/corner2.w, 1.0);
	corner3 = float4(corner3.xyz/corner3.w, 1.0);

    float3 plane0 = normalize(cross(corner0.xyz, corner1.xyz));
    float3 plane1 = normalize(cross(corner1.xyz, corner2.xyz));
    float3 plane2 = normalize(cross(corner2.xyz, corner3.xyz));
    float3 plane3 = normalize(cross(corner3.xyz, corner0.xyz));

    GroupMemoryBarrierWithGroupSync();

	// step 2: read depth buffer for zMin and zMax
	
    float curZ = params.depthTexture.Load(globalId);
	if (curZ != 0.0f)
	{
		uint originalZ;
		InterlockedMax(sharedZMax, asuint(curZ), originalZ);
	}
    GroupMemoryBarrierWithGroupSync();

	// step 3: classify lights
    
    float zMax = asfloat(sharedZMax);
    zMax = (params.invProjMatrix[2][2]*zMax + params.invProjMatrix[2][3]) / (params.invProjMatrix[3][2] * zMax + params.invProjMatrix[3][3]);
    float zMin = 0.0f;
    zMin = (params.invProjMatrix[2][2] * zMin + params.invProjMatrix[2][3]) / (params.invProjMatrix[3][2] * zMin + params.invProjMatrix[3][3]);
	for (uint i = localThreadIndex; i < params.lightCount; i += THREAD_GROUP_SIZE)
	{
		float3 viewSpaceLightPos = mul(params.viewMatrix, float4(params.lights[i].position, 1.0f)).xyz;
        uint lightType = params.lights[i].lightType_shadowMapId & 65535;
        float radius = params.lights[i].radius;
        bool intersect = true;
        if (radius > 0.0f)
        {
            intersect = intersect && dot(plane0, viewSpaceLightPos) >= -radius;
            intersect = intersect && dot(plane1, viewSpaceLightPos) >= -radius;
            intersect = intersect && dot(plane2, viewSpaceLightPos) >= -radius;
            intersect = intersect && dot(plane3, viewSpaceLightPos) >= -radius;
            intersect = intersect && (viewSpaceLightPos.z >= zMax - radius && viewSpaceLightPos.z <= zMin + radius);
        }
        if (intersect)
        {
            int ptr = 0;
            InterlockedAdd(lightIndexPtr, 1, ptr);
            if (ptr < LIGHT_LIST_MAX_SIZE - 1)
                lightList[ptr] = i;
        }
        
	}

    GroupMemoryBarrierWithGroupSync();

	// step 4: write to output buffer
	int numTilesX = (params.width + TILE_RES_X - 1) / TILE_RES_X;
	for (int i = localThreadIndex; i < min(lightIndexPtr, LIGHT_LIST_MAX_SIZE - 1); i+= THREAD_GROUP_SIZE)
		params.tiles[(groupId.y * numTilesX + groupId.x)*LIGHT_LIST_MAX_SIZE + i] = lightList[i];
	if (localThreadIndex == 0)
	{
        params.tiles[(groupId.y * numTilesX + groupId.x)*LIGHT_LIST_MAX_SIZE + min(lightIndexPtr, LIGHT_LIST_MAX_SIZE - 1)] = 0xFFFFFFFF;
	}
}