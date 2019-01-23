import ShaderLib;

#ifndef SPECIALIZATION_TYPE_0
#define SPECIALIZATION_TYPE_0 StaticMeshTransform
#endif

#ifndef SPECIALIZATION_TYPE_1
import DefaultMaterial;
#define SPECIALIZATION_TYPE_1 DefaultMaterial
#endif

#ifndef SPECIALIZATION_TYPE_2
#define SPECIALIZATION_TYPE_2 StandardVertexAttribs<SingleVertexUVSet, NoVertexColorSet, NoBoneWeightSet>
#endif

ParameterBlock<SPECIALIZATION_TYPE_0 > gWorldTransform;
ParameterBlock<SPECIALIZATION_TYPE_1 > gMaterial;
ParameterBlock<ViewParams> gView;
ParameterBlock<LightingEnvironment> gLighting;

struct VSOutput
{
	float4 projPos : SV_POSITION;
    float3 worldPos;
    float3 normal, tangent;
    float binormalSign;
    SPECIALIZATION_TYPE_2.ColorSet vertColorSet;
    SPECIALIZATION_TYPE_2.UVSet vertUVSet;
    nointerpolation SPECIALIZATION_TYPE_2.BoneWeightSet.PackedType vertBoneWeightSet;
};

VSOutput vs_main(SPECIALIZATION_TYPE_2 vertexIn)
{
	VSOutput rs;
    VertexPositionInfo vin;
    vin.vertPos = vertexIn.getPos();
    vin.tangentFrame = vertexIn.getTangentFrame();
    VertexPositionInfo worldPos = gWorldTransform.getWorldSpacePos(vin);
    rs.normal = worldPos.vertNormal;
    rs.tangent = worldPos.vertTangent;
    rs.binormalSign = worldPos.binormalSign;
    rs.vertColorSet = vertexIn.getColorSet();
    rs.vertUVSet = vertexIn.getUVSet();
    rs.vertBoneWeightSet = vertexIn.getBoneWeightSet().getPackedRepresentation();

    VertexAttribs<SPECIALIZATION_TYPE_2.UVSet, SPECIALIZATION_TYPE_2.ColorSet, SPECIALIZATION_TYPE_2.BoneWeightSet> vattribs;
    vattribs.uvSet = rs.vertUVSet;
    vattribs.colorSet = rs.vertColorSet;
    vattribs.boneWeightSet.setPackedRepresentation(rs.vertBoneWeightSet);
    float3 displacement = gMaterial.getDisplacement(worldPos, vattribs, viewParams);
    rs.worldPos = worldPos.vertPos + displacement;
    rs.projPos = gView.viewProjectionTransform * float4(rs.worldPos, 1.0);
    return rs;
}

float4 ps_main(VSOutput vsOut)
{
    VertexPositionInfo vin;
    vin.vertPos = vsOut.worldPos;
    vin.vertNormal = normalize(vsOut.normal);
    vin.vertTangent = normalize(vsOut.tangent);
    vin.vertBinormal = cross(vin.vertNormal, vin.vertTangent);
    vin.binormalSign = vin.binormalSign;
    VertexAttribs<SPECIALIZATION_TYPE_2.UVSet, SPECIALIZATION_TYPE_2.ColorSet, SPECIALIZATION_TYPE_2.BoneWeightSet> vattribs;
    vattribs.uvSet = vsOut.vertUVSet;
    vattribs.colorSet = vsOut.vertColorSet;
    vattribs.boneWeightSet.setPackedRepresentation(vsOut.vertBoneWeightSet);
    SPECIALIZATION_TYPE_1.MaterialPattern pattern = gMaterial.evalPattern(vin, vattribs, viewParams);
    return pattern.computeForwardLighting(gLighting, vin, viewParams);
}