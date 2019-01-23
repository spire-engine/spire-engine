import ShaderLib;
import ViewParams;

struct SkeletalMeshMaterial : IMaterial
{
    typedef PbrMaterialPattern<NoSelfShadow> MaterialPattern;
    float3 getDisplacement<TVertexUVSet : IVertexUVSet, TVertexColorSet : IVertexColorSet>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        return float3(0.0);
    }
    MaterialPattern evalPattern<TVertexUVSet : IVertexUVSet, TVertexColorSet : IVertexColorSet>(VertexPositionInfo shadingPoint, TVertAttribs vertAttribs, ViewParams viewParams)
    {
        MaterialPattern rs;
        rs.albedo = float3(0.0, 0.2, 0.7);
        rs.normal = float3(0.0, 0.0, 1.0);
        rs.roughness = 0.4;
        rs.metallic = 0.3;
        rs.specular = 1.0;
        rs.opacity = 1.0;
        rs.ao = 1.0;
        rs.isDoubleSided = false;
        return rs;
    }
}
