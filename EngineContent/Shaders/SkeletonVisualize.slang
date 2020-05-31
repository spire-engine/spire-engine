import ShaderLib;


struct SkeletonVisualizeMaterial : IMaterial
{
    float3 solidColor;
    float3 highlightColor;
    float alpha;
    int highlightId;
    typedef PbrMaterialPattern<NoSelfShadow> MaterialPattern;
	float3 getDisplacement<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        return float3(0.0);
    }
   	MaterialPattern evalPattern<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        MaterialPattern rs;
        rs.albedo = solidColor;
        rs.normal = float3(0.0, 0.0, 1.0);
        rs.specular = 0.5;
        rs.roughness = 0.7;
        rs.metallic = 0.4;
        rs.isDoubleSided = false;
        rs.opacity = 1.0;
        for (int i = 0; i < 4; i++)
        {
            uint boneId = vertAttribs.getBoneId(i);
            float boneWeight = vertAttribs.getBoneWeight(i);
            if (boneId == 255) continue;
            if (boneId == highlightId)
            {
                rs.opacity = lerp(0.4, 1.0, (sin(viewParams.time * 4.0)*0.5 + 0.5)) * alpha * boneWeight;
                break;
            }   
        }
        rs.ao = 1.0;
        return rs;
    }
};