import ShaderLib;
#ifdef IMPORT_MODULE_0
import IMPORT_MODULE_0;
#endif
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

#ifndef SPECIALIZATION_TYPE_0
#define SPECIALIZATION_TYPE_0 ViewParams 
#endif

#ifndef SPECIALIZATION_TYPE_1
import DefaultMaterial;
#define SPECIALIZATION_TYPE_1 DefaultMaterial
#endif

#ifndef SPECIALIZATION_TYPE_2
#define SPECIALIZATION_TYPE_2 StaticMeshTransform 
#endif

#ifndef SPECIALIZATION_TYPE_3
#define SPECIALIZATION_TYPE_3 StandardVertexAttribs<VertexUVSet1, StandardTangentFrame, VertexColorSet0, NoBoneWeightSet>
#endif

ParameterBlock<SPECIALIZATION_TYPE_0 > gView;
ParameterBlock<SPECIALIZATION_TYPE_1 > gMaterial;
ParameterBlock<SPECIALIZATION_TYPE_2 > gWorldTransform;

struct VSOutput
{
	float4 projPos : SV_POSITION;
    float3 worldPos : WORLDPOS;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float binormalSign : BINORMALSIGN;
    SPECIALIZATION_TYPE_3.ColorSet vertColorSet;
    SPECIALIZATION_TYPE_3.UVSet vertUVSet;
    SPECIALIZATION_TYPE_3.BoneWeightSet.PackedType vertBoneWeightSet : BONEWEIGHTSET;
};

VSOutput vs_main(SPECIALIZATION_TYPE_3 vertexIn)
{
	VSOutput rs;
    rs.vertColorSet = vertexIn.getColorSet();
    rs.vertUVSet = vertexIn.getUVSet();
    rs.vertBoneWeightSet = vertexIn.getBoneWeightSet().getPackedRepresentation();

    VertexAttribs<SPECIALIZATION_TYPE_3.UVSet, SPECIALIZATION_TYPE_3.ColorSet, SPECIALIZATION_TYPE_3.BoneWeightSet> vattribs;
    vattribs.uvSet = rs.vertUVSet;
    vattribs.colorSet = rs.vertColorSet;
    vattribs.boneWeightSet.setPackedRepresentation(rs.vertBoneWeightSet);

    VertexPositionInfo vin;
    vin.vertPos = vertexIn.getPos();
    vin.tangentFrame = vertexIn.getTangentFrame();
    VertexPositionInfo worldPos = gWorldTransform.getWorldSpacePos(vin, vattribs);
    rs.normal = worldPos.tangentFrame.vertNormal;
    rs.tangent = worldPos.tangentFrame.vertTangent;
    rs.binormalSign = worldPos.tangentFrame.binormalSign;
    
    float3 displacement = gMaterial.getDisplacement(worldPos, vattribs, gView);
    rs.worldPos = worldPos.vertPos + displacement;
    rs.projPos = mul(gView.viewProjectionTransform, float4(rs.worldPos, 1.0));
    return rs;
}

void ps_main(VSOutput vsOut)
{
    VertexPositionInfo vin;
    vin.vertPos = vsOut.worldPos;
    vin.tangentFrame.vertNormal = normalize(vsOut.normal);
    vin.tangentFrame.vertTangent = normalize(vsOut.tangent);
    vin.tangentFrame.vertBinormal = cross(vin.tangentFrame.vertNormal, vin.tangentFrame.vertTangent);
    vin.tangentFrame.binormalSign = vsOut.binormalSign;
    VertexAttribs<SPECIALIZATION_TYPE_3.UVSet, SPECIALIZATION_TYPE_3.ColorSet, SPECIALIZATION_TYPE_3.BoneWeightSet> vattribs;
    vattribs.uvSet = vsOut.vertUVSet;
    vattribs.colorSet = vsOut.vertColorSet;
    vattribs.boneWeightSet.setPackedRepresentation(vsOut.vertBoneWeightSet);
    SPECIALIZATION_TYPE_1.MaterialPattern pattern = gMaterial.evalPattern(vin, vattribs, gView);
    if (!pattern.shadowAlphaTest())
        discard;
}