struct SolidColorMaterial : IMaterial
{
    vec3 solidColor;
    typedef PbrMaterialPattern<NoSelfShadow> MaterialPattern;
    float3 getDisplacement<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        return float3(0.0);
    }
    MaterialPattern evalPattern<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        MaterialPattern rs;
        rs.init();
        rs.albedo = solidColor;
        rs.normal = float3(0.0, 0.0, 1.0);
        rs.roughness = 0.7;
        rs.metallic = 0.4;
        rs.specular = 0.3;
        rs.opacity = 1.0;
        rs.ao = 1.0;
        rs.isDoubleSided = false;
        return rs;
    }
}