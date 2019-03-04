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
#define SPECIALIZATION_TYPE_1 LightmapGBufferGlobalParams
#endif

#ifndef SPECIALIZATION_TYPE_2
import DefaultMaterial;
#define SPECIALIZATION_TYPE_2 DefaultMaterial
#endif

#ifndef SPECIALIZATION_TYPE_3
#define SPECIALIZATION_TYPE_3 StaticMeshTransform 
#endif

#ifndef SPECIALIZATION_TYPE_4
#define SPECIALIZATION_TYPE_4 StandardVertexAttribs<VertexUVSet1, StandardTangentFrame, VertexColorSet0, NoBoneWeightSet>
#endif

typedef SPECIALIZATION_TYPE_2 TMaterial;
typedef SPECIALIZATION_TYPE_4 TVertex;

ParameterBlock<SPECIALIZATION_TYPE_0> gView;
ParameterBlock<SPECIALIZATION_TYPE_1> gGlobalMem;
ParameterBlock<SPECIALIZATION_TYPE_2> gMaterial;
ParameterBlock<SPECIALIZATION_TYPE_3> gWorldTransform;

struct VSOutput
{
	float4 projPos : SV_POSITION;
    float3 worldPos;
    float3 normal, tangent;
    float3 baryCentric;
    float binormalSign;
    TVertex.ColorSet vertColorSet;
    TVertex.UVSet vertUVSet;
    TVertex.BoneWeightSet.PackedType vertBoneWeightSet;
};

VSOutput vs_main(TVertex vertexIn, int vertexId : SV_VertexID)
{
	VSOutput rs;
    switch (vertexId % 3)
    {
    case 0:
        rs.baryCentric = float3(1.0f, 0.0f, 0.0f);
        break;
    case 1:
        rs.baryCentric = float3(0.0f, 1.0f, 0.0f);
        break;
    case 2:
        rs.baryCentric = float3(0.0f, 0.0f, 1.0f);
        break;
    }
    rs.vertColorSet = vertexIn.getColorSet();
    rs.vertUVSet = vertexIn.getUVSet();
    rs.vertBoneWeightSet = vertexIn.getBoneWeightSet().getPackedRepresentation();

    VertexAttribs<TVertex.UVSet, TVertex.ColorSet, TVertex.BoneWeightSet> vattribs;
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
    rs.projPos = float4(vertexIn.getUVSet().getUV(1) * 2.0 - 1.0, 0.0, 1.0);
    gGlobalMem.vertices[vertexId] = rs.worldPos;
    DeviceMemoryBarrier();
    return rs;
}

struct PsOut
{
	float4 diffuse : SV_Target0;
	float4 position : SV_Target1;
	float4 normal : SV_Target2;
    float depth : SV_Depth;
};

PsOut ps_main(VSOutput vsOut, int primId : SV_PrimitiveID)
{
    VertexPositionInfo vin;
    vin.vertPos = vsOut.worldPos;
    vin.tangentFrame.vertNormal = normalize(vsOut.normal);
    vin.tangentFrame.vertTangent = normalize(vsOut.tangent);
    vin.tangentFrame.vertBinormal = cross(vin.tangentFrame.vertTangent, vin.tangentFrame.vertNormal);
    vin.tangentFrame.binormalSign = vsOut.binormalSign;
    VertexAttribs<TVertex.UVSet, TVertex.ColorSet, TVertex.BoneWeightSet> vattribs;
    vattribs.uvSet = vsOut.vertUVSet;
    vattribs.colorSet = vsOut.vertColorSet;
    vattribs.boneWeightSet.setPackedRepresentation(vsOut.vertBoneWeightSet);
    TMaterial.MaterialPattern pattern = gMaterial.evalPattern(vin, vattribs, gView);

	PsOut rs;
	rs.diffuse = pattern.getDiffuse();
	rs.position = float4(vsOut.worldPos, 0.0);
	rs.normal = float4(vin.tangentFrame.vertNormal, 0.0);
    rs.depth = 0.0;
    if (vsOut.baryCentric.x < 0) rs.depth -= vsOut.baryCentric.x;
    if (vsOut.baryCentric.x > 1.0) rs.depth += 1.0-vsOut.baryCentric.x;
    if (vsOut.baryCentric.y < 0) rs.depth -= vsOut.baryCentric.y;
    if (vsOut.baryCentric.y > 1.0) rs.depth += 1.0-vsOut.baryCentric.y;
    if (vsOut.baryCentric.z < 0) rs.depth -= vsOut.baryCentric.z;
    if (vsOut.baryCentric.z > 1.0) rs.depth += 1.0-vsOut.baryCentric.z;
    if (rs.depth > 1.0) rs.depth = 1.0;
    rs.depth *= 0.95;

    float size = max(max(fwidth(vsOut.worldPos.x), fwidth(vsOut.worldPos.y)), fwidth(vsOut.worldPos.z));
    rs.position = float4(vsOut.worldPos, size);

    return rs;
}