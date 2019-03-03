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

ParameterBlock<SPECIALIZATION_TYPE_0> gView;
ParameterBlock<SPECIALIZATION_TYPE_1> gMaterial;
ParameterBlock<SPECIALIZATION_TYPE_2> gWorldTransform;

struct VSOutput
{
	float4 projPos : SV_POSITION;
    float4 color;
};

VSOutput vs_main(SPECIALIZATION_TYPE_3 vertexIn)
{
	VSOutput rs;
    rs.color = vertexIn.getColorSet().getColor(0);
    rs.projPos = mul(gView.viewProjectionTransform, float4(vertexIn.getPos(), 1.0));
    return rs;
}

float4 ps_main(VSOutput vsOut)
{
    return vsOut.color;
}