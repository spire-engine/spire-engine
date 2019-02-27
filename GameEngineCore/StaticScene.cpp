#include "StaticScene.h"
#include "Bvh.h"
#include "Level.h"
#include "CoreLib/Graphics/BBox.h"
#include "StaticMeshActor.h"

namespace GameEngine
{
    using namespace VectorMath;
    using namespace CoreLib;
    struct StaticFace
    {
        Vec3 verts[3];
        Vec2 uvs[3];
        Actor* actor;
    };
    
    class MeshBvhEvaluator
    {
    public:
        static const int ElementsPerNode = 8;
        inline float EvalCost(int n1, float a1, int n2, float a2, float area)
        {
            return 0.125f + ((float)n1*a1 + (float)n2*a2) / area;
        }
    };

    // Trace Triangle
    class MeshTracer
    {
    public:
        inline bool Trace(StaticSceneTracingResult & inter, const StaticFace & face, const Ray & ray, float & t) const
        {
            Vec3 e1 = face.verts[1] - face.verts[0];
            Vec3 e2 = face.verts[2] - face.verts[0];
            Vec3 s1 = Vec3::Cross(ray.Dir, e2);
            float  invd = 1.0f / (Vec3::Dot(s1, e1));
            Vec3 d = ray.Origin - face.verts[0];
            float  b1 = Vec3::Dot(d, s1) * invd;
            Vec3 s2 = Vec3::Cross(d, e1);
            float  b2 = Vec3::Dot(ray.Dir, s2) * invd;
            float temp = Vec3::Dot(e2, s2) * invd;

            if (b1 < 0.f || b1 > 1.f || b2 < 0.f || b1 + b2 > 1.f || temp < 0.f || temp > ray.tMax)
            {
                return false;
            }
            else
            {
                t = inter.T = temp;
                inter.IsHit = true;
                inter.Actor = face.actor;
                inter.UV = face.uvs[0] * b1 + face.uvs[1] * b2 + face.uvs[2] * (1.0f - b1 - b2);
                return true;
            }
        }
    };

    Vec3 SafeRcp(Vec3 vin)
    {
        Vec3 rs;
        if (vin.x > 1e-10f || vin.x < -1e-10f) rs.x = 1.0f / vin.x; else rs.x = 0.0f;
        if (vin.y > 1e-10f || vin.y < -1e-10f) rs.y = 1.0f / vin.y; else rs.y = 0.0f;
        if (vin.z > 1e-10f || vin.z < -1e-10f) rs.z = 1.0f / vin.z; else rs.z = 0.0f;
        return rs;
    }

    class StaticSceneImpl : public StaticScene
    {
    public:
        Bvh<StaticFace> bvh;
        virtual StaticSceneTracingResult TraceRay(const Ray & ray) override
        {
            StaticSceneTracingResult result;
            MeshTracer tracer;
            Vec3 rcpDir = SafeRcp(ray.Dir);
            TraverseBvh<StaticFace, MeshTracer, StaticSceneTracingResult, false>(tracer, result, bvh, ray, rcpDir);
            return result;
        }
    };

    void AddMeshInstance(List<StaticFace>& faces, Mesh * mesh, Matrix4 localTransform, Actor * actor)
    {
        int uvChannelId = mesh->GetVertexFormat().GetUVChannelCount() - 1;
        for (int i = 0; i < mesh->Indices.Count() / 3; i++)
        {
            StaticFace f;
            f.actor = actor;
            for (int j = 0; j < 3; j++)
            {
                int vid = mesh->Indices[i * 3 + j];
                f.uvs[j] = mesh->GetVertexUV(vid, uvChannelId);
                f.verts[j] = localTransform.Transform(Vec4::Create(mesh->GetVertexPosition(vid), 1.0f)).xyz();
            }
            faces.Add(f);
        }
    }

    StaticScene* BuildStaticScene(Level* level)
    {
        StaticSceneImpl* scene = new StaticSceneImpl();
        List<StaticFace> faces;
        for (auto actor : level->Actors)
        {
            if (auto smActor = actor.Value.As<StaticMeshActor>())
            {
                AddMeshInstance(faces, smActor->Mesh, smActor->LocalTransform.GetValue(), smActor.Ptr());
            }
        }
        Bvh_Build<StaticFace> bvhBuild;
        MeshBvhEvaluator costEvaluator;
        List<BuildData<StaticFace>> elements;
        elements.SetSize(faces.Count());
        for (int i = 0; i < faces.Count(); i++)
        {
            elements[i].Bounds.Init();
            for (int j = 0; j < 3; j++)
                elements[i].Bounds.Union(faces[i].verts[j]);
            elements[i].Element = faces.Buffer() + i;
            elements[i].Center = (elements[i].Bounds.Min + elements[i].Bounds.Max) * 0.5f;
        }
        ConstructBvh(bvhBuild, elements.Buffer(), elements.Count(), costEvaluator);
        scene->bvh.FromBuild(bvhBuild);
        return scene;
    }
}