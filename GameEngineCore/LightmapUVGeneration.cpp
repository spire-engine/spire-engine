#include "LightmapUVGeneration.h"
#include "DisjointSet.h"
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
        int chartId;
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
            auto e0 = verts[1] - verts[0];
            auto e1 = verts[2] - verts[0];
            uvSurfaceArea = 0.5f * (e0.x * e1.y - e0.y * e1.x);
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

    struct Chart
    {
        List<int> faces;
        float surfaceArea = 0.0f;
        float packScale = 1.0f;
        Vec2 size, innerSize, packOrigin;
        Vec2 minUV, maxUV;
    };


    float safeInv(float x)
    {
        if (fabs(x) < 1e-5f)
            return 0.0f;
        return 1.0f / x;
    }

    struct VertexOverlapList
    {
        List<List<int>> lists; // a list of overlapping vertex lists
        List<int> vertexListId;  // for each vertex, stores the list id it belongs to
        DisjointSet disjointSet;
        EnumerableDictionary<int, int> disjointSetIdToListId;

        bool PositionOverlaps(Vec3 p0, Vec3 p1)
        {
            return (p0 - p1).Length2() < 1e-6f;
        }
        List<int> & GetOverlappedIndices(int idx)
        {
            int setId = disjointSet.Find(idx);
            int listId = disjointSetIdToListId[setId]();
            return lists[listId];
        }
        void Build(Mesh& mesh)
        {
            mesh.UpdateBounds();
            // classify vertices in to a 256^3 grid
            EnumerableDictionary<int, List<int>> vertexGrid;
            auto invMeshSize = (mesh.Bounds.Max - mesh.Bounds.Min);
            invMeshSize = Vec3::Create(safeInv(invMeshSize.x), safeInv(invMeshSize.y), safeInv(invMeshSize.z));
            auto getVertexGridId = [&](int faceVert)
            {
                int vid = mesh.Indices[faceVert];
                auto p = mesh.GetVertexPosition(vid);
                auto op = (p - mesh.Bounds.Min);
                op *= invMeshSize;
                int ix = Math::Clamp((int)(op.x * 256.0f), 0, 255);
                int iy = Math::Clamp((int)(op.y * 256.0f), 0, 255);
                int iz = Math::Clamp((int)(op.z * 256.0f), 0, 255);
                int gridId = ix + (iy << 8) + (iz << 16);
                return gridId;
            };
            auto getNeighborGridId = [](int gridId, int gx, int gy, int gz)
            {
                int ix = gridId & 255;
                int iy = (gridId >> 8) & 255;
                int iz = (gridId >> 16) & 255;
                ix += gx;
                iy += gy;
                iz += gz;
                ix = Math::Clamp(ix, 0, 255);
                iy = Math::Clamp(iy, 0, 255);
                iz = Math::Clamp(iz, 0, 255);
                gridId = ix + (iy << 8) + (iz << 16);
                return gridId;
            };
            for (int i = 0; i < mesh.Indices.Count(); i++)
            {
                int gridId = getVertexGridId(i);
                auto list = vertexGrid.TryGetValue(gridId);
                if (!list)
                {
                    vertexGrid.Add(gridId, List<int>());
                    list = vertexGrid.TryGetValue(gridId);
                }
                list->Add(i);
            }
            disjointSet.Init(mesh.Indices.Count());
            for (int i = 0; i < mesh.Indices.Count(); i++)
            {
                // check if nearby vertices overlap
                int thisGridId = getVertexGridId(i);
                for (int gx = -1; gx <= 1; gx++)
                {
                    for (int gy = -1; gy <= 1; gy++)
                    {
                        for (int gz = -1; gz <= 1; gz++)
                        {
                            int gridId = getNeighborGridId(thisGridId, gx, gy, gz);
                            auto verts = vertexGrid.TryGetValue(gridId);
                            if (verts)
                            {
                                for (auto faceVert : *verts)
                                {
                                    if (PositionOverlaps(mesh.GetVertexPosition(mesh.Indices[i]), mesh.GetVertexPosition(mesh.Indices[faceVert])))
                                    {
                                        disjointSet.Union(i, faceVert);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            for (int i = 0; i < mesh.Indices.Count(); i++)
            {
                int disjointSetId = disjointSet.Find(i);
                int listId = -1;
                if (!disjointSetIdToListId.TryGetValue(disjointSetId, listId))
                {
                    listId = disjointSetIdToListId.Count();
                    disjointSetIdToListId.Add(disjointSetId, listId);
                }
            }
            lists.SetSize(disjointSetIdToListId.Count());
            for (int i = 0; i < mesh.Indices.Count(); i++)
            {
                int disjointSetId = disjointSet.Find(i);
                int listId = -1;
                disjointSetIdToListId.TryGetValue(disjointSetId, listId);
                lists[listId].Add(i);
            }
        }
    };
    
    struct LightmapUVGenerationContext
    {
        Mesh * mesh;
        Mesh * meshOut;
        List<Face> faces;
        List<Chart> charts;
        Dictionary<Vec2i, List<int>> faceList;
        VertexOverlapList overlapList;
        DisjointSet faceSets;
        bool VertexOverlaps(int v0, int v1)
        {
            int i0 = mesh->Indices[v0];
            int i1 = mesh->Indices[v1];
            return (mesh->GetVertexPosition(i0) - mesh->GetVertexPosition(i1)).Length2() < 1e-6f &&
                (mesh->GetVertexUV(i0, 0) - mesh->GetVertexUV(i1, 0)).Length() < 1e-4f;
        }
        void BuildCharts()
        {
            faceSets.Init(mesh->Indices.Count() / 3);
            overlapList.Build(*mesh);
            for (auto range : mesh->ElementRanges)
            {
                int rangeEnd = range.StartIndex + range.Count;
                for (int i = range.StartIndex; i < rangeEnd; i++)
                {
                    int faceI = i / 3;
                    auto & overlappingFaceVerts = overlapList.GetOverlappedIndices(i);
                    for (auto j : overlappingFaceVerts)
                    {
                        if (j > i && j < rangeEnd)
                        {
                            int faceJ = j / 3;
                            if (VertexOverlaps(i, j))
                            {
                                if (VertexOverlaps(faceI * 3 + (i + 2) % 3, faceJ * 3 + (j + 1) % 3) ||
                                    VertexOverlaps(faceI * 3 + (i + 1) % 3, faceJ * 3 + (j + 2) % 3))
                                {
                                    if (faces[faceI].uvSurfaceArea * faces[faceJ].uvSurfaceArea >= 0.0f)
                                    {
                                        faceSets.Union(faceI, faceJ);
                                    }
                                }
                            }
                        }
                    }

                }
            }
            EnumerableDictionary<int, int> faceSetIdToChartId;
            for (int i = 0; i < mesh->Indices.Count() / 3; i++)
            {
                int faceSetId = faceSets.Find(i);
                int chartId = -1;
                if (!faceSetIdToChartId.TryGetValue(faceSetId, chartId))
                {
                    chartId = faceSetIdToChartId.Count();
                    faceSetIdToChartId.Add(faceSetId, chartId);
                }
                faces[i].chartId = chartId;
            }
            charts.SetSize(faceSetIdToChartId.Count());
            for (int i = 0; i < mesh->Indices.Count() / 3; i++)
            {
                charts[faces[i].chartId].faces.Add(i);
            }
            for (auto & chart : charts)
            {
                // normalize uv to [0,1]
                chart.minUV = Vec2::Create(1e9f, 1e9f);
                chart.maxUV = Vec2::Create(-1e9f, -1e9f);
                for (auto & f : chart.faces)
                {
                    auto & face = faces[f];
                    for (int i = 0; i < 3; i++)
                    {
                        Vec2 uv = face.verts[i];
                        if (uv.x < chart.minUV.x) chart.minUV.x = uv.x;
                        if (uv.y < chart.minUV.y) chart.minUV.y = uv.y;
                        if (uv.x > chart.maxUV.x) chart.maxUV.x = uv.x;
                        if (uv.y > chart.maxUV.y) chart.maxUV.y = uv.y;
                    }
                }
                chart.innerSize = chart.maxUV - chart.minUV;
                Vec2 invSize = Vec2::Create(1.0f / chart.innerSize.x, 1.0f / chart.innerSize.y);
                for (auto & f : chart.faces)
                {
                    auto & face = faces[f];
                    for (int i = 0; i < 3; i++)
                    {
                        Vec2 uv = face.verts[i];
                        face.verts[i] = (uv - chart.minUV) * invSize;
                    }
                    chart.surfaceArea += face.GetSurfaceArea(mesh);
                }
            }
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

        void RasterizeChart(Canvas& bmp, Chart & chart)
        {
            for (auto f : chart.faces)
            {
                ProjectedTriangle tri;
                Vec2 sv[3];
                auto invSize = chart.maxUV - chart.minUV;
                invSize = Vec2::Create(safeInv(invSize.x), safeInv(invSize.y));
                for (int i = 0; i < 3; i++)
                {
                    sv[i] = (faces[f].verts[i] - chart.minUV) * invSize;
                    /*
                    // Compute equations of the planes through the two edges

                    float3 plane[2];

                    plane[0] = cross(currentPos.xyw - prevPos.xyw, prevPos.xyw);

                    plane[1] = cross(nextPos.xyw - currentPos.xyw, currentPos.xyw);



                    // Move the planes by the appropriate semidiagonal

                    plane[0].z -= dot(hPixel.xy, abs(plane[0].xy));

                    plane[1].z -= dot(hPixel.xy, abs(plane[1].xy));


                    */
                }
                SetupTriangle(tri, sv[0], sv[1], sv[2], bmp.width, bmp.height);
                Rasterizer::Rasterize(bmp, tri);
            }
        }

        bool PackCharts2(int textureSize, float scale, int paddingPixels)
        {
            // first, rasterize all charts
            List<Canvas> chartBitmaps;
            chartBitmaps.SetSize(charts.Count());
            for (int i = 0; i < charts.Count(); i++)
            {
                auto & chart = charts[i];
                int chartBitmapWidth = Math::Max(1, (int)(chart.innerSize.x * textureSize));
                int chartBitmapHeight = Math::Max(1, (int)(chart.innerSize.y * textureSize));
                Canvas bmp;
                bmp.(chartBitmapWidth, chartBitmapHeight);
                RasterizeChart(bmp, chart);
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
                                meshOut->GrowVertexBuffer(meshOut->GetVertexCount() + 1);
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