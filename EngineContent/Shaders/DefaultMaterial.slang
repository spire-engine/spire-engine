import ShaderLib;

struct DefaultMaterial : IMaterial
{
    typedef PbrMaterialPattern<NoSelfShadow> MaterialPattern;
	float3 getDisplacement<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        return float3(0.0);
    }
   	MaterialPattern evalPattern<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        MaterialPattern rs;
        rs.albedo = float3(0.9, 0.45, 0.05);
        rs.normal = float3(0.0, 0.0, 1.0);
        rs.specular = 0.5;
        rs.roughness = 0.7;
        rs.metallic = 0.4;
        rs.isDoubleSided = false;
        rs.opacity = 1.0;
        rs.ao = 1.0;
        return rs;
    }
}