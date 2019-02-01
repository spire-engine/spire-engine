#include "LightmapUVGeneration.h"
#include "Mesh.h"

using namespace VectorMath;

namespace GameEngine
{
    struct Face
    {
        int Id;
        float uvSurfaceArea;
        Vec3 edges[3];  //edge equations (A,B,C)
        void Init(Mesh* mesh, int index)
        {
            Id = index;
            int mod3[] = { 0, 1, 2, 0 };
            Vec2 verts[] = 
            {
                    mesh->GetVertexUV(mesh->Indices[index * 3], 0),
                    mesh->GetVertexUV(mesh->Indices[index * 3 + 1], 0),
                    mesh->GetVertexUV(mesh->Indices[index * 3 + 2], 0),

            };
            for (int i = 0; i < 3; i++)
            {
                Vec2 e = verts[i + 1] - verts[i];
                Vec2 n = Vec2::Create(-e.y, e.x);
                float c = -Vec2::Dot(n, verts[i]);
                edges[i] = Vec3::Create(n.x, n.y, c);
            }

            // test winding direction
            float s = edges[0].x * verts[2].x + edges[0].y * verts[2].y + edges[0].z;
            if (s > 0.0f)
            {
                // all vertices should be on the "negative" side of its opposing edge
                // if not, negate all edge equations to make it so
                for (int i = 0; i < 3; i++)
                    edges[i] = -edges[i];
            }

            // compute uv surface area
            uvSurfaceArea = 0.0f;
            Vec2 e0 = Vec2::Create(edges[0].x, edges[0].y);
            float l = e0.Length();
            if (l > 0.0f)
            {
                e0.x /= l; e0.y /= l;
                uvSurfaceArea = 0.5f * l * fabs(e0.x * (verts[2].x - verts[0].x) + e0.y * (verts[2].y - verts[0].y));
            }
        }
        float GetSurfaceArea(Mesh * mesh)
        {
            Vec3 verts[] =
            {
                mesh->GetVertexPosition(mesh->Indices[Id * 3]),
                mesh->GetVertexPosition(mesh->Indices[Id * 3 + 1]),
                mesh->GetVertexPosition(mesh->Indices[Id * 3 + 2])
            };
            Vec3 e1 = verts[1] - verts[0];
            Vec3 e2 = verts[2] - verts[0];
            Vec3 n = Vec3::Cross(e1, e2);
            Vec3 n1 = Vec3::Cross(e1, n);
            if (n1.Length2() > 0.0f)
            {
                n1 = n1.Normalize();
                float d = -Vec3::Dot(n1, verts[0]);
                return 0.5f * fabs(Vec3::Dot(n1, verts[2]) + d) * e1.Length();
            }
            else
                return 0.0f;
        }
    };

    struct FaceGroup
    {
        float surfaceArea;
        float uvSurfaceArea;
        List<int> faces;
        Vec2 minUV, maxUV;
        Vec2 packOrigin;
        float packScale;
    };
    struct LightmapUVGenerationContext
    {
        Mesh * mesh;
        Mesh * meshOut;
        List<Face> faces;
        List<FaceGroup> groups;
        bool FaceOverlaps(Face& f0, Face& f1, bool & hasConnection)
        {
            hasConnection = false;
            for (int e = 0; e < 3; e++)
            {
                Vec3 edge = f0.edges[e];
                Vec2 v = mesh->GetVertexUV(mesh->Indices[f1.Id * 3 + 0], 0);
                float s0 = edge.x * v.x + edge.y * v.y + edge.z;

                v = mesh->GetVertexUV(mesh->Indices[f1.Id * 3 + 1], 0);
                float s1 = edge.x * v.x + edge.y * v.y + edge.z;

                v = mesh->GetVertexUV(mesh->Indices[f1.Id * 3 + 2], 0);
                float s2 = edge.x * v.x + edge.y * v.y + edge.z;

                if (s0 >= -1e-5f && s1 >= -1e-5f && s2 >= -1e-5f)
                {
                    hasConnection = (s0 <= 1e-5f) || (s1 <= 1e-5f) || (s2 <= 1e-5f);
                    return false;
                }
            }
            return true;
        }
        void AddFaceToGroup(int faceId, int groupId)
        {
            groups[groupId].faces.Add(faceId);
        }
        void FaceGroupOverlapTest(FaceGroup & g, Face & f, bool & hasOverlap, bool & hasConnection)
        {
            for (int i = 0; i < g.faces.Count(); i++)
            {
                auto & face = faces[g.faces[i]];
                bool connects = false;
                if (FaceOverlaps(face, f, connects))
                {
                    hasOverlap = true;
                    return;
                }
                if (connects)
                    hasConnection = true;
            }
        }
        void InsertFace(int faceId)
        {
            // find a group to insert the face into
            bool inserted = false;
            for (int i = 0; i < groups.Count(); i++)
            {
                bool hasOverlap = false;
                bool hasConnection = false;
                FaceGroupOverlapTest(groups[i], faces[i], hasOverlap, hasConnection);
                if (!hasOverlap && (hasConnection || i == groups.Count() - 1))
                {
                    // add to group i
                    AddFaceToGroup(faceId, i);
                    inserted = true;
                }
            }
            if (!inserted)
            {
                // create a new group and insert the face into it
                groups.Add(FaceGroup());
                AddFaceToGroup(faceId, groups.Count() - 1);
            }
        }

        void ClassifyFaces() // classify faces into non-overlapping groups
        {
            for (int f = 0; f < faces.Count(); f++)
            {
                InsertFace(f);
            }
        }

        void BuildFaces()
        {
            faces.SetSize(mesh->Indices.Count() / 3);
            for (int i = 0; i < faces.Count(); i++)
            {
                faces[i].Init(mesh, i);
            }
        }

        Vec2 PackGroups()
        {
            List<Vec2> placementPoints;
            placementPoints.Add(Vec2::Create(0.0f));
            Vec2 curMax = Vec2::Create(0.0f, 0.0f);
            groups.Sort([](FaceGroup& f1, FaceGroup & f2) {return f1.uvSurfaceArea*f1.packScale > f2.uvSurfaceArea*f2.packScale; });
            for (auto & g : groups)
            {
                Vec2 size = (g.maxUV - g.minUV) * g.packScale;
                float minArea = 1e20f;
                int bestPlacementPoint = -1;
                Vec2 bestExtent = curMax;
                for (int i = 0; i < placementPoints.Count(); i++)
                {
                    // find the new extent as a result of placing the group at this placement point
                    Vec2 vmax = placementPoints[i] + size;
                    Vec2 newMax = Vec2::Create(Math::Max(curMax.x, vmax.x), Math::Max(curMax.y, vmax.y));
                    float squareSize = Math::Max(newMax.x, newMax.y);
                    float area = squareSize * squareSize;
                    if (area < minArea)
                    {
                        minArea = area;
                        bestPlacementPoint = i;
                        bestExtent = Vec2::Create(squareSize, squareSize);
                    }
                }
                Vec2 p = placementPoints[bestPlacementPoint];
                g.packOrigin = p;
                Vec2 newPoint = p + Vec2::Create(size.x, 0.0f);
                Vec2 newPoint1 = p + Vec2::Create(0.0f, size.y);
                placementPoints[bestPlacementPoint] = newPoint;
                placementPoints.Add(newPoint1);
                curMax = bestExtent;
            }
            return curMax;
        }

        void SetZeroUV(FaceGroup & g)
        {
            for (auto f : g.faces)
            {
                meshOut->SetVertexUV(faces[f].Id * 3, 1, Vec2::Create(0.0f));
                meshOut->SetVertexUV(faces[f].Id * 3 + 1, 1, Vec2::Create(0.0f));
                meshOut->SetVertexUV(faces[f].Id * 3 + 2, 1, Vec2::Create(0.0f));
            }
        }

        void RenormalizeUVs(Vec2 extentSize, float uvPadding)
        {
            if (extentSize.x < 1e-5f || extentSize.y < 1e-5f)
            {
                for (auto & g : groups)
                    SetZeroUV(g);
                return;
            }
            Vec2 invExtentSize = Vec2::Create(1.0f / extentSize.x, 1.0f / extentSize.y);
            for (auto & g : groups)
            {
                auto size = (g.maxUV - g.minUV);
                if (size.x * size.y > 0.0f)
                {
                    auto invSize = Vec2::Create(1.0f / size.x, 1.0f / size.y);
                    auto packSize = size * g.packScale;
                    auto innerSize = packSize * uvPadding;
                    auto innerOffset = g.packOrigin + Vec2::Create(uvPadding*0.5f, uvPadding*0.5f);
                    for (auto f : g.faces)
                    {
                        auto & face = faces[f];
                        for (int i = 0; i < 3; i++)
                        {
                            auto uv = mesh->GetVertexUV(face.Id * 3 + i, 0);
                            uv -= g.minUV;
                            uv.x *= (invSize.x * innerSize.y + innerOffset.x) * invExtentSize.x;
                            uv.y *= (invSize.y * innerSize.y + innerOffset.y) * invExtentSize.y;
                            meshOut->SetVertexUV(face.Id * 3 + i, 1, uv);
                        }
                    }
                }
                else
                    SetZeroUV(g);
            }
        }

        void GenerateUniqueUV(Mesh * pMeshIn, Mesh* pMeshOut, float uvPadding)
        {
            mesh = pMeshIn;
            meshOut = pMeshOut;
            BuildFaces();
            ClassifyFaces();

            float maxRatio = -1e9f;
            for (auto & g : groups)
            {
                g.surfaceArea = 0.0f;
                g.uvSurfaceArea = 0.0f;
                g.minUV = Vec2::Create(1e9f, 1e9f);
                g.maxUV = Vec2::Create(-1e9f, -1e9f);
                for (auto f : g.faces)
                {
                    auto & face = faces[f];
                    g.surfaceArea += face.GetSurfaceArea(mesh);
                    g.uvSurfaceArea += face.uvSurfaceArea;
                    for (int i = 0; i < 3; i++)
                    {
                        Vec2 uv = mesh->GetVertexUV(face.Id * 3 + i, 0);
                        if (uv.x < g.minUV.x) g.minUV.x = uv.x;
                        if (uv.y < g.minUV.y) g.minUV.y = uv.y;
                        if (uv.x > g.maxUV.x) g.maxUV.x = uv.x;
                        if (uv.y > g.maxUV.y) g.maxUV.y = uv.y;
                    }
                }
                if (g.surfaceArea != 0.0f)
                {
                    float ratio = g.uvSurfaceArea / g.surfaceArea;
                    if (ratio > maxRatio)
                        maxRatio = ratio;
                }
            }
            if (maxRatio < 1e-4f)
                maxRatio = 1.0f;
            // determine the scale of each group
            // so that each group can have the same resolution
            for (auto & g : groups)
            {
                g.packScale = maxRatio / (g.uvSurfaceArea / g.surfaceArea);
            
            }
            Vec2 extentSize = PackGroups();

            auto mvf = mesh->GetVertexFormat();
            MeshVertexFormat newVertexFormat = MeshVertexFormat(mvf.GetColorChannelCount(), 2, mvf.HasTangent(), mvf.HasSkinning());
            meshOut->SetVertexFormat(newVertexFormat);
            meshOut->AllocVertexBuffer(mesh->GetVertexCount());
            meshOut->Indices = mesh->Indices;
            meshOut->Bounds = mesh->Bounds;
            meshOut->ElementRanges = mesh->ElementRanges;
            for (int i = 0; i < meshOut->GetVertexCount(); i++)
            {
                meshOut->SetVertexPosition(i, mesh->GetVertexPosition(i));
                if (mvf.HasTangent())
                    meshOut->SetVertexTangentFrame(i, mesh->GetVertexTangentFrame(i));
                if (mvf.HasSkinning())
                {
                    Array<int, 8> boneIds;
                    Array<float, 8> boneWeights;
                    mesh->GetVertexSkinningBinding(i, boneIds, boneWeights);
                    meshOut->SetVertexSkinningBinding(i, boneIds.GetArrayView(), boneWeights.GetArrayView());
                }
                for (int j = 0; j < mvf.GetColorChannelCount(); j++)
                    meshOut->SetVertexUV(i, j, mesh->GetVertexUV(i, j));

                meshOut->SetVertexUV(i, 0, mesh->GetVertexUV(i, 0));
            }
            RenormalizeUVs(extentSize, uvPadding);
        }
    };

    void GenerateLightmapUV(Mesh* meshOut, Mesh* meshIn, float padding)
    {
        LightmapUVGenerationContext ctx;
        ctx.GenerateUniqueUV(meshIn, meshOut, padding);
    }
}