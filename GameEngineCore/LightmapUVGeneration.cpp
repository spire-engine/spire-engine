#include "LightmapUVGeneration.h"
#include "DisjointSet.h"
#include "Mesh.h"
#include "Rasterizer.h"
#include "CoreLib/Imaging/Bitmap.h"
#include "CoreLib/Threading.h"
using namespace VectorMath;

namespace GameEngine
{
    const float UNINITIALIZED_UV = -1024.0f;
    struct Face
    {
        int Id;
        int chartId;
        float uvSurfaceArea;
        Vec2 verts[3];
        void Init(Mesh* mesh, int index)
        {
            Id = index;
            int mod3[] = { 0, 1, 2, 0 };
            for (int i = 0; i < 3; i++)
            {
                verts[i] = mesh->GetVertexUV(mesh->Indices[index * 3 + i], 0);
            }
            // compute uv surface area
            auto e0 = verts[1] - verts[0];
            auto e1 = verts[2] - verts[0];
            uvSurfaceArea = 0.5f * (e0.x * e1.y - e0.y * e1.x);
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
            float l1 = e1.Length();
            float l2 = e2.Length();
            if (l1 < 1e-7f || l2 < 1e-7f)
                return 0.0f;
            e1 *= 1.0f / l1;
            e2 *= 1.0f / l2;
            float cosTheta = Vec3::Dot(e1, e2);
            float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
            return 0.5f * l1 * l2 * sinTheta;
        }
    };

    struct Chart
    {
        List<int> faces;
        float surfaceArea = 0.0f;
        Vec2 size, packOrigin;
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
        VertexOverlapList overlapList;
        DisjointSet faceSets;

        struct ChartPlacement
        {
            Vec2 position;
            Vec2 size;
        };

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
                chart.size = chart.maxUV - chart.minUV;
                Vec2 invSize = Vec2::Create(1.0f / chart.size.x, 1.0f / chart.size.y);
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

        void BuildFaces()
        {
            faces.SetSize(mesh->Indices.Count() / 3);
            for (int i = 0; i < faces.Count(); i++)
            {
                faces[i].Init(mesh, i);
            }
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
                    sv[i] = faces[f].verts[i];
                }
                Rasterizer::SetupTriangle(tri, sv[0], sv[1], sv[2], bmp.width, bmp.height);
                Rasterizer::Rasterize(bmp, tri);
            }
        }

        static const int CoarseReductionFactor = 4;
        struct HierarchicalBitmap
        {
            Canvas FineBitmap;
            Canvas CoarseBitmap;
            void BuildCoarseBitmap()
            {
                CoarseBitmap.Init((FineBitmap.width + CoarseReductionFactor - 1) / CoarseReductionFactor,
                    (FineBitmap.height + CoarseReductionFactor - 1) / CoarseReductionFactor);
                for (int i = 0; i < CoarseBitmap.height; i++)
                {
                    int y0 = i * CoarseReductionFactor;
                    int y1 = Math::Min(FineBitmap.height, y0 + CoarseReductionFactor);
                    for (int j = 0; j < CoarseBitmap.width; j++)
                    {
                        int x0 = j * CoarseReductionFactor;
                        int x1 = Math::Min(FineBitmap.width, x0 + CoarseReductionFactor);
                        for (int y = y0; y < y1; y++)
                            for (int x = x0; x < x1; x++)
                                if (FineBitmap.Get(x, y))
                                {
                                    CoarseBitmap.Set(j, i);
                                    goto nextJ;
                                }
                    nextJ:;
                    }
                }
            }
            void Set(int x, int y)
            {
                FineBitmap.Set(x, y);
                CoarseBitmap.Set(x / CoarseReductionFactor, y / CoarseReductionFactor);
            }
        };

        void WriteChartBitmask(HierarchicalBitmap & texture, HierarchicalBitmap & chartBitmap, int x, int y)
        {
            for (int i = 0; i < chartBitmap.FineBitmap.height; i++)
                for (int j = 0; j < chartBitmap.FineBitmap.width; j++)
                {
                    if (chartBitmap.FineBitmap.Get(j, i))
                    {
                        texture.Set(j + x, i + y);
                    }
                }
        }
        void DilateBitmap(Canvas & rs, Canvas& inBmp, int pixels)
        {
            Canvas tmp;
            rs.Init(inBmp.width + pixels * 2, inBmp.height + pixels * 2);
            for (int i = 0; i < inBmp.height; i++)
                for (int j = 0; j < inBmp.width; j++)
                    if (inBmp.Get(j, i))
                        rs.Set(j + pixels, i + pixels);
            tmp.Init(rs.width, rs.height);
            for (auto i = 0; i < tmp.height; i++)
                for (auto j = 0; j < tmp.width; j++)
                {
                    for (auto k = -pixels; k <= pixels; k++)
                        if (j + k >= 0 && j + k < tmp.width && rs.Get(j + k, i))
                        {
                            tmp.Set(j, i);
                            break;
                        }
                }
            for (auto i = 0; i < tmp.height; i++)
                for (auto j = 0; j < tmp.width; j++)
                {
                    for (auto k = -pixels; k <= pixels; k++)
                        if (i + k >= 0 && i + k < tmp.height && tmp.Get(j, i + k))
                        {
                            rs.Set(j, i);
                            break;
                        }
                }
        }

        bool ChartHasOverlap(HierarchicalBitmap& texture, HierarchicalBitmap& chartBmp, int x, int y)
        {
            int cx = x / CoarseReductionFactor;
            int cy = y / CoarseReductionFactor;
            for (int i = 0; i < chartBmp.CoarseBitmap.height; i++)
                for (int j = 0; j < chartBmp.CoarseBitmap.width; j++)
                {
                    if (chartBmp.CoarseBitmap.Get(j, i) && 
                        texture.CoarseBitmap.Get(j + cx, i + cy))
                    {
                        int fiEnd = Math::Min(chartBmp.FineBitmap.height, (i + 1) * CoarseReductionFactor);
                        int fjEnd = Math::Min(chartBmp.FineBitmap.width, (j + 1) * CoarseReductionFactor);

                        for (int fi = i * CoarseReductionFactor; fi < fiEnd; fi++)
                            for (int fj = j * CoarseReductionFactor; fj < fjEnd; fj++)
                                if (chartBmp.FineBitmap.Get(fj, fi) && texture.FineBitmap.Get(fj + x, fi + y))
                                    return true;
                    }
                }
            return false;
        }
        bool TryPackCharts(int textureSize, float scale, int paddingPixels, List<ChartPlacement> & chartPositions)
        {
            chartPositions.SetSize(charts.Count());
            // first, rasterize all charts
            List<HierarchicalBitmap> chartBitmaps;
            chartBitmaps.SetSize(charts.Count());

            for (int i = 0; i < charts.Count(); i++)
            {
                auto & chart = charts[i];
                if (chart.size.x * scale > 1.0f || chart.size.y * scale > 1.0f)
                    return false;
                int chartBitmapWidth = Math::Max(1, (int)(chart.size.x * textureSize * scale));
                int chartBitmapHeight = Math::Max(1, (int)(chart.size.y * textureSize * scale));
                Canvas bmp;
                bmp.Init(chartBitmapWidth, chartBitmapHeight);
                RasterizeChart(bmp, chart);
                DilateBitmap(chartBitmaps[i].FineBitmap, bmp, paddingPixels);
                chartBitmaps[i].BuildCoarseBitmap();
            }
            
            // try all placements
            HierarchicalBitmap texture;
            texture.FineBitmap.Init(textureSize, textureSize);
            texture.CoarseBitmap.Init(textureSize / CoarseReductionFactor, textureSize / CoarseReductionFactor);

            for (int i = 0; i < charts.Count(); i++)
            {
                bool placed = false;
                for (int x = 0; x < textureSize - chartBitmaps[i].FineBitmap.width; x += 4)
                {
                    for (int y = 0; y < textureSize - chartBitmaps[i].FineBitmap.height; y += 4)
                    {
                        // try placing chart at (x,y)
                        if (!ChartHasOverlap(texture, chartBitmaps[i], x, y))
                        {
                            WriteChartBitmask(texture, chartBitmaps[i], x, y);
                            placed = true;
                            chartPositions[i].position = Vec2::Create((float)(x + paddingPixels), (float)(y + paddingPixels));
                            chartPositions[i].size = Vec2::Create((float)(chartBitmaps[i].FineBitmap.width - paddingPixels * 2), (float)(chartBitmaps[i].FineBitmap.height - paddingPixels * 2));
                            break;
                        }
                    }
                    if (placed)
                        break;
                }
                if (!placed)
                {
                    return false;
                }
            }
            return true;
        }

        bool PackCharts2(int textureSize, int paddingPixels, float & scale)
        {
            charts.Sort([](Chart& c1, Chart& c2) { return c1.surfaceArea > c2.surfaceArea; });

            List<ChartPlacement> chartPositions;
            // sort charts by size
            scale = 1.0f;
            // determine initial scale such that the largest chart will scale to between 0-1
            for (auto & c : charts)
            {
                if (c.size.x > 1.0f)
                    scale = Math::Min(scale, 1.0f / c.size.x);
                if (c.size.y > 1.0f)
                    scale = Math::Min(scale, 1.0f / c.size.y);
            }
            float failScale = scale, succScale = 0.0f;
            while (scale > 1e-5f)
            {
                List<ChartPlacement> tmpChartPositions;
                if (TryPackCharts(textureSize, scale, paddingPixels, tmpChartPositions))
                {
                    succScale = scale;
                    chartPositions = tmpChartPositions;
                    break;
                }
                failScale = scale;
                scale = scale * 0.5f;
            }
            // find best scale
            if (scale < 1e-5)
                return false;
            for (int iter = 0; iter < 5;  iter++)
            {
                List<ChartPlacement> tmpChartPositions;
                float midScale = (succScale + failScale) * 0.5f;
                bool succ = TryPackCharts(textureSize, midScale, paddingPixels, tmpChartPositions);
                if (succ)
                {
                    succScale = midScale;
                    chartPositions = tmpChartPositions;
                }
                else
                    failScale = midScale;
            }
            if (chartPositions.Count())
            {
                for (int i = 0; i < chartPositions.Count(); i++)
                {
                    charts[i].packOrigin = chartPositions[i].position * (1.0f / textureSize);
                    charts[i].size = chartPositions[i].size * (1.0f / textureSize);
                }
                return true;
            }
            return false;
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

        void RenormalizeUVs()
        {
            for (auto & chart : charts)
            {
                auto size = chart.size;
                if (size.x * size.y > 0.0f)
                {
                    for (auto f : chart.faces)
                    {
                        auto & face = faces[f];
                        for (int i = 0; i < 3; i++)
                        {
                            auto uv = face.verts[i];
                            uv.x = (uv.x * chart.size.x + chart.packOrigin.x);
                            uv.y = (uv.y * chart.size.y + chart.packOrigin.y);
                            auto existingUV = meshOut->GetVertexUV(meshOut->Indices[face.Id * 3 + i], 1);
                            if (existingUV.x != UNINITIALIZED_UV && (fabs(existingUV.x - uv.x) > 1e-4f || fabs(existingUV.y - uv.y)>1e-4f) )
                            {
                                // we need to add new vertex
                                int vid = mesh->Indices[face.Id * 3 + i];
                                int nid = meshOut->GetVertexCount();
                                meshOut->GrowVertexBuffer(meshOut->GetVertexCount() + 1);
                                CopyVertex(nid, vid);
                                meshOut->SetVertexUV(nid, 1, uv);
                                meshOut->Indices[face.Id * 3 + i] = nid;
                            }
                            else
                                meshOut->SetVertexUV(mesh->Indices[face.Id * 3 + i], 1, uv);
                        }
                    }
                }
            }
        }

        bool GenerateUniqueUV(Mesh * pMeshIn, Mesh* pMeshOut, int textureSize, int paddingPixels)
        {
            mesh = pMeshIn;
            meshOut = pMeshOut;
            BuildFaces();

            float totalSurfaceArea = 0.0f;
            for (auto & f : faces)
                totalSurfaceArea += f.GetSurfaceArea(pMeshIn);
            pMeshOut->SetSurfaceArea(totalSurfaceArea);

            BuildCharts();

            float scale = 0.0f;
            bool succ = PackCharts2(textureSize, paddingPixels, scale);
            if (!succ)
                return false;

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
            RenormalizeUVs();
            
            return true;
        }
    };

    bool GenerateLightmapUV(Mesh* meshOut, Mesh* meshIn, int textureSize, int paddingPixels)
    {
        LightmapUVGenerationContext ctx;
        return ctx.GenerateUniqueUV(meshIn, meshOut, textureSize, paddingPixels);
    }
}