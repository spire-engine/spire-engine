import ShaderLib;

#ifdef IMPORT_MODULE_1
import IMPORT_MODULE_1;
#endif
#ifdef IMPORT_MODULE_2
import IMPORT_MODULE_2;
#endif
#ifdef IMPORT_MODULE_3
import IMPORT_MODULE_3;
#endif
#ifdef IMPORT_MODULE_4
import IMPORT_MODULE_4;
#endif

struct LightmapVisualizationViewParams
{
    float4x4 viewProjectionTransform;
    StructuredBuffer<uint2> lightmapSizes;
    StructuredBuffer<float4> lightmapColors;
};

#ifndef SPECIALIZATION_TYPE_0
#define SPECIALIZATION_TYPE_0 LightmapVisualizationViewParams 
#endif

#ifndef SPECIALIZATION_TYPE_1
import DefaultMaterial;
#define SPECIALIZATION_TYPE_1 DefaultMaterial
#endif

#ifndef SPECIALIZATION_TYPE_2
#define SPECIALIZATION_TYPE_2 StaticMeshTransform 
#endif

#ifndef SPECIALIZATION_TYPE_3
#define SPECIALIZATION_TYPE_3 StandardVertexFormat<VertexUVSet1, StandardTangentFrame, VertexColorSet0, NoBoneWeightSet>
#endif

ParameterBlock<SPECIALIZATION_TYPE_0> gView;
ParameterBlock<SPECIALIZATION_TYPE_1> gMaterial;
ParameterBlock<SPECIALIZATION_TYPE_2> gWorldTransform;

struct VSOutput
{
	float4 projPos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput vs_main(SPECIALIZATION_TYPE_3 vertexIn)
{
	VSOutput rs;
    rs.uv = vertexIn.getUVSet().getUV(1);
    VertexAttribs<SPECIALIZATION_TYPE_3.UVSet, SPECIALIZATION_TYPE_3.ColorSet, SPECIALIZATION_TYPE_3.BoneWeightSet> vattribs;
    vattribs.uvSet = vertexIn.getUVSet();
    vattribs.colorSet = vertexIn.getColorSet();
    vattribs.boneWeightSet = vertexIn.getBoneWeightSet();
    VertexPositionInfo vin;
    vin.vertPos = vertexIn.getPos();
    vin.tangentFrame = vertexIn.getTangentFrame();
    VertexPositionInfo worldPos = gWorldTransform.getWorldSpacePos(vin, vattribs);
    rs.projPos = PlatformNDC(mul(gView.viewProjectionTransform, float4(worldPos.vertPos, 1.0)));
    return rs;
}

float4 ps_main(VSOutput vsOut) : SV_Target
{
    uint lightmapId = gWorldTransform.getLightmapId();
    if (lightmapId == 0xFFFFFFFF)
        return float4(0.3, 0.3, 0.3, 1.0);
    float2 lightmapUV = vsOut.uv;
    uint arrayId = lightmapId >> 24;
    uint2 lightmapSize = gView.lightmapSizes[arrayId];
    float3 color = gView.lightmapColors[lightmapId&0xFFFFFF].xyz;
    uint2 pix = uint2(lightmapUV * lightmapSize);
    bool isAccentColor = ((pix.x ^ pix.y) & 1) != 0;
    return isAccentColor ? float4(color, 1.0) : float4(1.0, 1.0, 1.0, 1.0);
}