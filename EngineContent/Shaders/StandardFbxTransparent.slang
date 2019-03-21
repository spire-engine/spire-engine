import ShaderLib;
[Transparent]
struct StandardFbxTransparentMaterial : IMaterial
{
    typedef PbrMaterialPattern<NoSelfShadow> MaterialPattern;
    Texture2D albedoMap;
    Texture2D specularMap;
    Texture2D normalMap;
    float pRoughness;
    float pMetallic;
    float pOpacity;
	float3 getDisplacement<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        return float3(0.0);
    }
   	MaterialPattern evalPattern<TVertAttribs : IVertexAttribs>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        MaterialPattern rs;
        rs.init();
        float2 vertUV = vertAttribs.getUV(0);
        rs.normal = normalMap.Sample(viewParams.textureSampler, vertUV).xyz * 2.0 - 1.0;
        rs.roughness = pRoughness;
        rs.metallic = pMetallic;
        rs.specular = specularMap.Sample(viewParams.textureSampler, vertUV).x;
        rs.albedo = albedoMap.Sample(viewParams.textureSampler, vertUV).xyz;
        rs.opacity = pOpacity;
        return rs;
    }
};