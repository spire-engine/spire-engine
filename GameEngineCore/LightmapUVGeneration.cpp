#include "LightmapUVGeneration.h"
#include "Mesh.h"
#include "Rasterizer.h"
#include "CoreLib/Imaging/Bitmap.h"

using namespace VectorMath;

namespace GameEngine
{
    const int QuantizeResolution = 8192;
    const int RasterizeResolution = 2048;
    const int PackResolution = 1024;
    const float UNINITIALIZED_UV = -1024.0f;
    struct Face
    {
        int Id;
        float uvSurfaceArea;
        Vec3 edges[3];  //edge equations (A,B,C)
        Vec2i quantizedUVs[3];
        Vec2 verts[3];
        ProjectedTriangle ptri;
        void Init(Mesh* mesh, int index)
        {
            Id = index;
            int mod3[] = { 0, 1, 2, 0 };
            for (int i = 0; i<3;i++)
            {
                verts[i] = mesh->GetVertexUV(mesh->Indices[index * 3 + i], 0);
            };
            for (int i = 0; i < 3; i++)
            {
                quantizedUVs[i] = Vec2i::Create((int)(verts[i].x * QuantizeResolution), (int)(verts[i].y * QuantizeResolution));
                Vec2 e = verts[mod3[i + 1]] - verts[i];
                Vec2 n = Vec2::Create(-e.y, e.x);
                float len = n.Length();
                if (len > 1e-5f)
                {
                    n *= (1.0f / len);
                }
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
            Rasterizer::SetupTriangle(ptri, verts[0], verts[1], verts[2], RasterizeResolution, RasterizeResolution);
        }
        float GetSurfaceArea(Mesh * mesh)
        {
            Vec3 vertPos[] =
            {
                mesh->GetVertexPosition(mesh->Indices[Id * 3]),
                mesh->GetVertexPosition(mesh->Indices[Id * 3 + 1]),
                mesh->GetVertexPosition(mesh->Indices[Id * 3 + 2])
            };
            Vec3 e1 = vertPos[1] - vertPos[0];
            Vec3 e2 = vertPos[2] - vertPos[0];
            Vec3 n = Vec3::Cross(e1, e2);
            Vec3 n1 = Vec3::Cross(e1, n);
            if (n1.Length2() > 0.0f)
            {
                n1 = n1.Normalize();
                float d = -Vec3::Dot(n1, vertPos[0]);
                return 0.5f * fabs(Vec3::Dot(n1, vertPos[2]) + d) * e1.Length();
            }
            else
                return 0.0f;
        }
    };

    struct FaceGroup
    {
        List<int> faces;
        Canvas canvas;
        FaceGroup()
            : canvas(RasterizeResolution, RasterizeResolution)
        {
        }
    };

    struct Chart
    {
        List<int> faces;
        float surfaceArea = 0.0f;
        float packScale = 1.0f;
        Vec2 size, innerSize, packOrigin;
    };
    
    struct LightmapUVGenerationContext
    {
        Mesh * mesh;
        Mesh * meshOut;
        List<Face> faces;
        List<FaceGroup> groups;
        List<Chart> charts;
        Dictionary<Vec2i, List<int>> faceList;

        void BuildCharts()
        {
            IntSet insertedFaces;
            IntSet queuedFaces;
            queuedFaces.SetMax(faces.Count());

            for (auto & g : groups)
            {
                IntSet groupFaceSet;
                for (auto & f : g.faces)
                    groupFaceSet.Add(f);
                for (auto & f : g.faces)
                {
                    if (!insertedFaces.Contains(f))
                    {
                        Chart chart;
                        List<int> queue;
                        chart.faces.Clear();
                        queue.Add(f);
                        queuedFaces.Clear();
                        queuedFaces.Add(f);

                        int qptr = 0;
                        while (qptr < queue.Count())
                        {
                            int fid = queue[qptr++];
                            if (!insertedFaces.Contains(fid) && groupFaceSet.Contains(fid))
                            {
                                insertedFaces.Add(fid);
                                chart.faces.Add(fid);
                            }
                            for (auto qv : faces[fid].quantizedUVs)
                            {
                                auto & nlist = faceList[qv].GetValue();
                                for (auto neighborId : nlist)
                                {
                                    if (!queuedFaces.Contains(neighborId) && groupFaceSet.Contains(neighborId))
                                    {
                                        queue.Add(neighborId);
                                        queuedFaces.Add(neighborId);
                                    }
                                }
                            }
                        }
                        charts.Add(chart);
                    }
                }
            }
            for (auto & chart : charts)
            {
                // normalize uv to [0,1]
                Vec2 minUV = Vec2::Create(1e9f, 1e9f), maxUV = Vec2::Create(-1e9f, -1e9f);
                for (auto & f : chart.faces)
                {
                    auto & face = faces[f];
                    for (int i = 0; i < 3; i++)
                    {
                        Vec2 uv = face.verts[i];
                        if (uv.x < minUV.x) minUV.x = uv.x;
                        if (uv.y < minUV.y) minUV.y = uv.y;
                        if (uv.x > maxUV.x) maxUV.x = uv.x;
                        if (uv.y > maxUV.y) maxUV.y = uv.y;
                    }
                }
                chart.innerSize = maxUV - minUV;
                Vec2 invSize = Vec2::Create(1.0f / chart.innerSize.x, 1.0f / chart.innerSize.y);
                for (auto & f : chart.faces)
                {
                    auto & face = faces[f];
                    for (int i = 0; i < 3; i++)
                    {
                        Vec2 uv = face.verts[i];
                        face.verts[i] = (uv - minUV) * invSize;
                    }
                    chart.surfaceArea += face.GetSurfaceArea(mesh);
                }
            }
        }

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

                if (s0 >= -1e-3f && s1 >= -1e-3f && s2 >= -1e-3f)
                {
                    hasConnection = (s0 <= 1e-3f) || (s1 <= 1e-3f) || (s2 <= 1e-3f);
                    return false;
                }
            }
            return true;
        }
        void AddFaceToGroup(int faceId, int groupId)
        {
            Rasterizer::Rasterize(groups[groupId].canvas, faces[faceId].ptri);
            groups[groupId].faces.Add(faceId);
        }
        template<typename SetPixelFunc>
        void RasterizeLine(VectorMath::Vec2 s0, VectorMath::Vec2 s1, const SetPixelFunc & setPixel)
        {
            int x0 = (int)(s0.x * RasterizeResolution);
            int y0 = (int)(s0.y * RasterizeResolution);
            int x1 = (int)(s1.x * RasterizeResolution);
            int y1 = (int)(s1.y * RasterizeResolution);

            int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int err = (dx > dy ? dx : -dy) / 2, e2;
            for (;;)
            {
                if (setPixel(x0, y0)) return;
                if (x0 == x1 && y0 == y1) break;
                e2 = err;
                if (e2 > -dx) { err -= dy; x0 += sx; }
                if (e2 < dy) { err += dx; y0 += sy; }
            }
        }

        void FaceGroupOverlapTest(FaceGroup & g, Face & f, bool & hasOverlap, bool & hasConnection)
        {
            hasConnection = false;
            int overlaps = Rasterizer::CountOverlap(g.canvas, f.ptri);
            if (overlaps > 16)
                hasOverlap = true;
            else
            {
                return;
                for (int i = 0; i < 3; i++)
                {
                    Vec2 v0 = f.verts[i];
                    Vec2 v1 = f.verts[(i + 1) % 3];
                    RasterizeLine(v0, v1, [&](int x, int y)
                    {
                        for (int ox = x - 1; ox <= x + 1; ox++)
                        {
                            int cx = Math::Clamp(ox, 0, RasterizeResolution - 1);

                            for (int oy = y - 1; oy <= y + 1; oy++)
                            {
                                int cy = Math::Clamp(oy, 0, RasterizeResolution - 1);
                                if (g.canvas.bitmap.Contains(cy*RasterizeResolution + cx))
                                {
                                    hasConnection = true;
                                    return true;
                                }
                            }
                        }
                        return false;
                    });
                    if (hasConnection)
                        return;
                }
            }
#if 0
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
#endif
        }
        void InsertFace(int faceId)
        {
            // find a group to insert the face into

            bool inserted = false;
            struct Candidate 
            {
                int groupId; bool hasConnection;
            };
            List<Candidate> candidates;
            for (int i = 0; i < groups.Count(); i++)
            {
                bool hasOverlap = false;
                bool hasConnection = false;
                FaceGroupOverlapTest(groups[i], faces[faceId], hasOverlap, hasConnection);
                if (!hasOverlap)
                {
                    candidates.Add(Candidate{ i, hasConnection });
                }
            }
            for (auto c : candidates)
            {
                if (c.hasConnection)
                {
                    AddFaceToGroup(faceId, c.groupId);
                    inserted = true;
                }
            }
            if (!inserted && candidates.Count())
            {
                AddFaceToGroup(faceId, candidates.First().groupId);
            }
            else
            {
                // create a new group and insert the face into it
                groups.Add(FaceGroup());
                AddFaceToGroup(faceId, groups.Count() - 1);
            }
        }

        void ClassifyFaces() // classify faces into non-overlapping groups
        {
            IntSet insertedFaces;
            IntSet queuedFaces;
            queuedFaces.SetMax(faces.Count());
            insertedFaces.SetMax(faces.Count());
            List<int> queue;
            for (int f = 0; f < faces.Count(); f++)
            {
                if (!insertedFaces.Contains(f))
                {
                    queue.Clear();
                    queue.Add(f);
                    queuedFaces.Add(f);
                    int qptr = 0;
                    while (qptr < queue.Count())
                    {
                        int fid = queue[qptr++];
                        if (insertedFaces.Contains(fid)) continue;
                        InsertFace(fid);
                        insertedFaces.Add(fid);
                        for (auto qv : faces[fid].quantizedUVs)
                        {
                            auto & nlist = faceList[qv].GetValue();
                            for (auto neighborId : nlist)
                            {
                                if (!queuedFaces.Contains(neighborId))
                                {
                                    queue.Add(neighborId);
                                    queuedFaces.Add(neighborId);
                                }
                            }
                        }
                    }
                }
            }
        }

        void BuildFaces()
        {
            faces.SetSize(mesh->Indices.Count() / 3);
            for (int i = 0; i < faces.Count(); i++)
            {
                faces[i].Init(mesh, i);
                for (int j = 0; j < 3; j++)
                {
                    auto quv = faces[i].quantizedUVs[j];
                    if (auto list = faceList.TryGetValue(quv))
                    {
                        list->Add(i);
                    }
                    else
                    {
                        List<int> tlist;
                        tlist.Add(i);
                        faceList[quv] = tlist;
                    }
                }
            }
        }
        bool RectangleOverlap(Vec2 origin0, Vec2 size0, Vec2 origin1, Vec2 size1)
        {
            if (origin1.x + size1.x <= origin0.x + 1e-4f || origin0.x + size0.x <= origin1.x + 1e-4f)
                return false;
            if (origin1.y + size1.y <= origin0.y + 1e-4f || origin0.y + size0.y <= origin1.y + 1e-4f)
                return false;
            return true;
        }
        Vec2 PackCharts(float uvPadding)
        {
            List<Vec2> placementPoints;
            placementPoints.Add(Vec2::Create(0.0f));
            Vec2 curMax = Vec2::Create(0.0f, 0.0f);
            charts.Sort([](Chart& c1, Chart & c2) {return c1.surfaceArea > c2.surfaceArea; });
            float refSize = charts[0].innerSize.x;
            for (int ptr = 0; ptr < charts.Count(); ptr++)
            {
                auto & chart = charts[ptr];
                chart.size = chart.innerSize + Vec2::Create(refSize * uvPadding);
                Vec2 size = chart.size;
                float minArea = 1e20f;
                int bestPlacementPoint = -1;
                Vec2 bestExtent = curMax;
                for (int i = 0; i < placementPoints.Count(); i++)
                {
                    // can we really place here?
                    bool occupied = false;
                    for (int tptr = 0; tptr < ptr; tptr++)
                    {
                        auto thatOrigin = charts[tptr].packOrigin;
                        auto thatSize = charts[tptr].size;
                        auto thisOrigin = placementPoints[i];
                        if (RectangleOverlap(thatOrigin, thatSize, thisOrigin, size))
                        {
                            occupied = true;
                            break;
                        }
                    }
                    if (occupied)
                        continue;
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
                chart.packOrigin = p;
                Vec2 newPoint = p + Vec2::Create(size.x, 0.0f);
                Vec2 newPoint1 = p + Vec2::Create(0.0f, size.y);
                placementPoints[bestPlacementPoint] = newPoint;
                placementPoints.Add(newPoint1);
                curMax = bestExtent;
            }
            
            // visualize boxes
            /*CoreLib::Imaging::BitmapF bmp(RasterizeResolution, RasterizeResolution);
            auto pixels = bmp.GetPixels();
            auto invExt = Vec2::Create(1.0f / curMax.x, 1.0f / curMax.y);
            for (int i = 0; i < RasterizeResolution * RasterizeResolution; i++) pixels[i].SetZero();
            for (auto & chart : charts)
            {
                auto setPixel = [&](int x, int y) 
                {
                    if (x < RasterizeResolution && y < RasterizeResolution)
                    {
                        pixels[y * RasterizeResolution + x] = Vec4::Create(1.0f, 1.0f, 1.0f, 1.0f);
                    }
                    return false;
                };
                RasterizeLine(chart.packOrigin*invExt, (chart.packOrigin + chart.size)*invExt, setPixel);
                RasterizeLine((chart.packOrigin + Vec2::Create(chart.size.x, 0.0f)) *invExt, (chart.packOrigin + Vec2::Create(0.0f, chart.size.y))*invExt, setPixel);
            }
            bmp.GetImageRef().SaveAsBmpFile("debug.bmp");*/
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

        void CopyVertex(int dst, int src)
        {
            auto mvf = mesh->GetVertexFormat();
            meshOut->SetVertexPosition(dst, mesh->GetVertexPosition(src));
            if (mvf.HasTangent())
                meshOut->SetVertexTangentFrame(dst, mesh->GetVertexTangentFrame(src));
            if (mvf.HasSkinning())
            {
                Array<int, 8> boneIds;
                Array<float, 8> boneWeights;
                mesh->GetVertexSkinningBinding(src, boneIds, boneWeights);
                meshOut->SetVertexSkinningBinding(dst, boneIds.GetArrayView(), boneWeights.GetArrayView());
            }
            for (int j = 0; j < mvf.GetColorChannelCount(); j++)
                meshOut->SetVertexColor(dst, j, mesh->GetVertexColor(src, j));

            meshOut->SetVertexUV(dst, 0, mesh->GetVertexUV(src, 0));
        }

        void RenormalizeUVs(Vec2 extentSize)
        {
            Vec2 invExtentSize = Vec2::Create(1.0f / extentSize.x, 1.0f / extentSize.y);
            for (auto & chart : charts)
            {
                auto size = chart.size;
                auto innerOffset = chart.packOrigin + (chart.size - chart.innerSize)*0.5f;
                if (size.x * size.y > 0.0f)
                {
                    for (auto f : chart.faces)
                    {
                        auto & face = faces[f];
                        for (int i = 0; i < 3; i++)
                        {
                            auto uv = face.verts[i];
                            uv.x = (uv.x * chart.innerSize.x + innerOffset.x) * invExtentSize.x;
                            uv.y = (uv.y * chart.innerSize.y + innerOffset.y) * invExtentSize.y;
                            auto existingUV = meshOut->GetVertexUV(meshOut->Indices[face.Id * 3 + i], 1);
                            if (existingUV.x != UNINITIALIZED_UV && (fabs(existingUV.x - uv.x) > 1e-4f || fabs(existingUV.y - uv.y)>1e-4f) )
                            {
                                // we need to add new vertex
                                int vid = mesh->Indices[face.Id * 3 + i];
                                meshOut->AllocVertexBuffer(meshOut->GetVertexCount() + 1);
                                CopyVertex(meshOut->GetVertexCount() - 1, vid);
                                meshOut->SetVertexUV(meshOut->GetVertexCount() - 1, 1, uv);
                                meshOut->Indices[face.Id * 3 + i] = meshOut->GetVertexCount() - 1;
                            }
                            else
                                meshOut->SetVertexUV(mesh->Indices[face.Id * 3 + i], 1, uv);
                        }
                    }
                }
            }
        }

        void GenerateUniqueUV(Mesh * pMeshIn, Mesh* pMeshOut, float uvPadding)
        {
            mesh = pMeshIn;
            meshOut = pMeshOut;
            BuildFaces();
            ClassifyFaces();
            BuildCharts();
            // determine the scale of each chart
            // so that each group can have the same resolution
            //float maxSurfaceArea = From(charts).Max([](const Chart& c) {return c.surfaceArea; }).surfaceArea;
            for (auto & chart : charts)
            {
                chart.packScale = 1.0f; // chart.surfaceArea / maxSurfaceArea;
                chart.innerSize *= chart.packScale;
            }
            Vec2 extentSize = PackCharts(uvPadding);

            auto mvf = mesh->GetVertexFormat();
            MeshVertexFormat newVertexFormat = MeshVertexFormat(mvf.GetColorChannelCount(), 2, mvf.HasTangent(), mvf.HasSkinning());
            meshOut->SetVertexFormat(newVertexFormat);
            meshOut->AllocVertexBuffer(mesh->GetVertexCount());
            meshOut->Indices = mesh->Indices;
            meshOut->Bounds = mesh->Bounds;
            meshOut->ElementRanges = mesh->ElementRanges;
            for (int i = 0; i < meshOut->GetVertexCount(); i++)
            {
                CopyVertex(i, i);
                meshOut->SetVertexUV(i, 1, Vec2::Create(UNINITIALIZED_UV, UNINITIALIZED_UV));
            }
            RenormalizeUVs(extentSize);
        }
    };

    void GenerateLightmapUV(Mesh* meshOut, Mesh* meshIn, float padding)
    {
        LightmapUVGenerationContext ctx;
        ctx.GenerateUniqueUV(meshIn, meshOut, padding);
    }
}