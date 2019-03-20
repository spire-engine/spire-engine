struct SSAOParams
{
    float4x4 ProjMatrix, InvProjMatrix;
    int width, height;
    int blurRadius;
    float aoDistance;
    float aoRadius;
    float aoPower;
    Texture2D inputTexture;
    RWTexture2D outputTexture;
    StructuredBuffer<float4> randomDirections;  // 16x16x16
};

ParameterBlock<SSAOParams> params;

float DepthZToWorldZ(float z)
{
    return (params.InvProjMatrix[2][2] * z + params.InvProjMatrix[2][3]) / (params.InvProjMatrix[3][2] * z + params.InvProjMatrix[3][3]);
}

float3 ProjSpaceToWorldSpace(float3 p)
{
    p.x = p.x * 2.0f - 1.0f;
    p.y = p.y * 2.0f - 1.0f;
    float4 rs = mul(params.InvProjMatrix, float4(p, 1.0f));
    return rs.xyz / rs.w;
}

[numthreads(16, 16, 1)]
void cs_SSAO(uint3 threadId : SV_DispatchThreadID, uint3 localId : SV_GroupThreadID)
{
    float thisZ = params.inputTexture.Load(threadId).x;
    float invWidth = 1.0f / params.width;
    float invHeight = 1.0f / params.height;
    float2 invXY = float2(invWidth, invHeight);
    float3 thisWorldPos = ProjSpaceToWorldSpace(float3(threadId.xy * invXY, thisZ));
    float z_x1 = params.inputTexture.Load(threadId + int3(1, 0, 0)).x;
    float z_x2 = params.inputTexture.Load(threadId + int3(-1, 0, 0)).x;
    float3 viewPos_x;
    float flip = 1.0;
    if (abs(z_x1 - thisZ) < abs(z_x2 - thisZ))
        viewPos_x = float3((threadId.xy + int2(1, 0)) * invXY, z_x1);
    else
    {
        flip = -1.0f;
        viewPos_x = float3((threadId.xy + int2(-1, 0)) * invXY, z_x2);
    }

    float z_y1 = params.inputTexture.Load(threadId + int3(0, 1, 0)).x;
    float z_y2 = params.inputTexture.Load(threadId + int3(0, -1, 0)).x;
    float3 viewPos_y;
    if (abs(z_y1 - thisZ) < abs(z_y2 - thisZ))
        viewPos_y = float3((threadId.xy + int2(0, 1)) * invXY, z_y1);
    else
    {
        viewPos_y = float3((threadId.xy + int2(0, -1)) * invXY, z_y2);
        flip *= -1.0f;
    }
    float3 worldPos1 = ProjSpaceToWorldSpace(viewPos_x);
    float3 worldPos2 = ProjSpaceToWorldSpace(viewPos_y);
    float3 normal = normalize(cross(worldPos1 - thisWorldPos, worldPos2 - thisWorldPos)*flip);
    float3 tangent = normalize(worldPos1 - thisWorldPos);
    float3 binormal = cross(tangent, normal);
    float occlusion = 0.0f;
    for (int i = 0; i < 16; i++)
    {
        float3 dir = params.randomDirections[i * 256 + localId.y * 16 + localId.x].xyz;
        dir = normalize(dir.x * tangent + dir.y * normal + dir.z * binormal);
        for (int j = 1; j <= 8; j++)
        {
            float3 newPos = thisWorldPos + dir * (params.aoRadius * 0.125f * j);
            float4 projNewPos = mul(params.ProjMatrix, float4(newPos, 1.0));
            projNewPos.xyz *= 1.0f / projNewPos.w;
            float thatZ = params.inputTexture.Load(int3((projNewPos.xy*0.5+0.5)*float2(params.width, params.height), 0)).x;
            if (thatZ < projNewPos.z)
            {
                float thatZViewSpace = DepthZToWorldZ(thatZ);
                float zDist = abs(newPos.z - thatZViewSpace);
                newPos.z = thatZViewSpace;
                float thisOcclusion = clamp(1.0 - zDist / params.aoDistance, 0.0, 1.0);

                float dist = length(newPos - thisWorldPos);
                if (dist > params.aoRadius * 0.02f)
                    occlusion += clamp(lerp(thisOcclusion, 0.0f, dist/params.aoRadius), 0.0f, 1.0f);
                break;
            }
        }
    }
    occlusion /= 16;
    params.outputTexture[threadId.xy] = pow(float4(1.0f - occlusion), params.aoPower);
}

float gaussianKernel(int size, int i)
{
    float rsize = i / (float)size;
    return exp(-rsize * rsize*2.0f);
}

[numthreads(16, 16, 1)]
void cs_SSAOBlurX(uint3 threadId : SV_DispatchThreadID)
{
    int rad = params.blurRadius;
    float rs = 0.0f;
    float sumWeight = 0.0f;
    for (int i = max((int)threadId.x - rad, 0); i <= min((int)threadId.x + rad, params.width-1); i++)
    {
        int dist = abs(i - int(threadId.x));
        float kernel = gaussianKernel(rad, dist);
        float val = params.inputTexture.Load(uint3(i, threadId.y, 0)).x;
        rs += val * kernel;
        sumWeight += kernel;
    }
    params.outputTexture[threadId.xy] = float4(rs / sumWeight);
}

[numthreads(16, 16, 1)]
void cs_SSAOBlurY(uint3 threadId : SV_DispatchThreadID)
{
    int rad = params.blurRadius;
    float rs = 0.0f;
    float sumWeight = 0.0f;
    for (int i = int(threadId.y) - rad; i <= int(threadId.y) + rad; i++)
    {
        int idx = clamp(i, 0, params.height - 1);
        int dist = abs(idx - int(threadId.y));
        float kernel = gaussianKernel(rad, dist);
        float val = params.inputTexture.Load(uint3(threadId.x, idx, 0)).x;
        rs += val * kernel;
        sumWeight += kernel;
    }
    params.outputTexture[threadId.xy] = float4(rs / sumWeight);
}