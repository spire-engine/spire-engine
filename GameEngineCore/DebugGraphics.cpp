#include "DebugGraphics.h"
#include "Drawable.h"
#include "Mesh.h"
#include "MeshBuilder.h"
#include "RendererService.h"

namespace GameEngine
{
    using namespace CoreLib;

    class DebugGraphicsImpl : public DebugGraphics
    {
    private:
        bool changed = true;
        MeshBuilder trianglesBuilder;
        struct DebugLine
        {
            VectorMath::Vec3 v0, v1;
            VectorMath::Vec4 color;
        };
        struct DebugTriangle
        {
            VectorMath::Vec3 v0, v1, v2;
            VectorMath::Vec4 color;
        };
        List<DebugLine> lines;
        List<DebugTriangle> triangles;
        RefPtr<Drawable> linesDrawable;
        RefPtr<Drawable> trianglesDrawable;
        Array<Drawable*, 2> drawables;
    public:
        DebugGraphicsImpl()
        {
            
        }
        virtual void Clear() override
        {
            if (lines.Count())
            {
                lines.Clear();
                changed = true;
            }
            if (triangles.Count())
            {
                triangles.Clear();
                changed = true;
            }
        }
        virtual void AddLine(VectorMath::Vec4 color, VectorMath::Vec3 v0, VectorMath::Vec3 v1) override
        {
            changed = true;
            DebugLine line;
            line.color = color;
            line.v0 = v0;
            line.v1 = v1;
            lines.Add(line);
        }
        virtual void AddTriangle(VectorMath::Vec4 color, VectorMath::Vec3 v0, VectorMath::Vec3 v1, VectorMath::Vec3 v2) override
        {
            changed = true;
            DebugTriangle face;
            face.color = color;
            face.v0 = v0;
            face.v1 = v1;
            face.v2 = v2;
            triangles.Add(face);
        }
        virtual CoreLib::ArrayView<Drawable*> GetDrawables(RendererService * rendererService) override
        {
            if (changed)
            {
                linesDrawable = nullptr;
                drawables.Clear();
                MeshVertexFormat vfmt = MeshVertexFormat(1, 0, false, false);
                if (lines.Count())
                {
                    Mesh meshLines;
                    meshLines.SetPrimitiveType(PrimitiveType::Lines);
                    meshLines.Bounds.Init();
                    meshLines.SetVertexFormat(vfmt);
                    meshLines.AllocVertexBuffer(lines.Count() * 2);
                    meshLines.Indices.SetSize(lines.Count() * 2);
                    for (int i = 0; i < lines.Count(); i++)
                    {
                        meshLines.Bounds.Union(lines[i].v0);
                        meshLines.Bounds.Union(lines[i].v1);
                        meshLines.SetVertexPosition(i * 2, lines[i].v0);
                        meshLines.SetVertexColor(i * 2, 0, lines[i].color);
                        meshLines.SetVertexPosition(i * 2 + 1, lines[i].v1);
                        meshLines.SetVertexColor(i * 2 + 1, 0, lines[i].color);
                        meshLines.Indices[i * 2] = i * 2;
                        meshLines.Indices[i * 2 + 1] = i * 2 + 1;
                    }
                    MeshElementRange range;
                    range.StartIndex = 0;
                    range.Count = lines.Count() * 2;
                    meshLines.ElementRanges.Add(range);
                    linesDrawable = rendererService->CreateStaticDrawable(&meshLines, 0, nullptr, false);
                    drawables.Add(linesDrawable.Ptr());
                }
                trianglesDrawable = nullptr;
                if (triangles.Count())
                {
                    Mesh meshFaces;
                    meshFaces.SetPrimitiveType(PrimitiveType::Triangles);
                    meshFaces.Bounds.Init();
                    meshFaces.SetVertexFormat(vfmt);
                    meshFaces.AllocVertexBuffer(triangles.Count() * 3);
                    meshFaces.Indices.SetSize(triangles.Count() * 3);
                    for (int i = 0; i < triangles.Count(); i++)
                    {
                        meshFaces.Bounds.Union(triangles[i].v0);
                        meshFaces.Bounds.Union(triangles[i].v1);
                        meshFaces.Bounds.Union(triangles[i].v2);
                        meshFaces.SetVertexPosition(i * 3, triangles[i].v0);
                        meshFaces.SetVertexColor(i * 3, 0, triangles[i].color);
                        meshFaces.SetVertexPosition(i * 3 + 1, triangles[i].v1);
                        meshFaces.SetVertexColor(i * 3 + 1, 0, triangles[i].color);
                        meshFaces.SetVertexPosition(i * 3 + 2, triangles[i].v2);
                        meshFaces.SetVertexColor(i * 3 + 2, 0, triangles[i].color);
                        meshFaces.Indices[i * 3] = i * 3;
                        meshFaces.Indices[i * 3 + 1] = i * 3 + 1;
                        meshFaces.Indices[i * 3 + 2] = i * 3 + 2;
                    }
                    MeshElementRange range;
                    range.StartIndex = 0;
                    range.Count = triangles.Count() * 3;
                    meshFaces.ElementRanges.Add(range);
                    trianglesDrawable = rendererService->CreateStaticDrawable(&meshFaces, 0, nullptr, false);
                    drawables.Add(trianglesDrawable.Ptr());
                }
            }
            return drawables.GetArrayView();
        }
    };

    DebugGraphics * CreateDebugGraphics()
    {
        return new DebugGraphicsImpl();
    }
}