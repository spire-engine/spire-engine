
#include "LightmapUVGeneration.h"
#include "Mesh.h"
#include "CoreLib/Imaging/Bitmap.h"

using namespace CoreLib;
using namespace GameEngine;

void SetPixel(CoreLib::Imaging::BitmapF & img, int x, int y)
{
    int offset = img.GetWidth() * y + x;
    auto pixels = img.GetPixels();
    if (x < 1024 && y < 1024)
        pixels[offset] = VectorMath::Vec4::Create(1.0f, 1.0f, 1.0f, 1.0f);
}

void DrawLine(CoreLib::Imaging::BitmapF & img, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;
    for (;;)
    {
        SetPixel(img, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

void VisualizeUV(Mesh& mesh, int channel, String fileName)
{
    // draw new uv
    CoreLib::Imaging::BitmapF image(1024, 1024);
    auto pixels = image.GetPixels();
    memset(pixels, 0, image.GetWidth()*image.GetHeight() * sizeof(VectorMath::Vec4));
    for (int f = 0; f < mesh.Indices.Count() / 3; f++)
    {
        VectorMath::Vec2 verts[] =
        {
            mesh.GetVertexUV(mesh.Indices[f * 3], channel),
            mesh.GetVertexUV(mesh.Indices[f * 3 + 1], channel),
            mesh.GetVertexUV(mesh.Indices[f * 3 + 2], channel)
        };
        int w = image.GetWidth() - 1;
        int h = image.GetHeight() - 1;
        for (int i = 0; i < 3; i++)
            DrawLine(image, (int)(verts[i].x * w), (int)(verts[i].y * h), (int)(verts[(i + 1) % 3].x * w), (int)(verts[(i + 1) % 3].y * h));
    }
    image.GetImageRef().SaveAsBmpFile(fileName);
}

void VisualizeTrianglePair(Mesh & mesh, int f0, int f1, String fileName)
{
    // draw new uv
    CoreLib::Imaging::BitmapF image(1024, 1024);
    auto pixels = image.GetPixels();
    memset(pixels, 0, image.GetWidth()*image.GetHeight() * sizeof(VectorMath::Vec4));
    int faces[] = { f0, f1 };
    VectorMath::Vec2 minUV = VectorMath::Vec2::Create(1e9f, 1e9f), maxUV = VectorMath::Vec2::Create(-1e9f, -1e9f);
    for (int f : faces)
    {
        VectorMath::Vec2 verts[] =
        {
            mesh.GetVertexUV(mesh.Indices[f * 3], 0),
            mesh.GetVertexUV(mesh.Indices[f * 3 + 1], 0),
            mesh.GetVertexUV(mesh.Indices[f * 3 + 2], 0)
        };
        for (auto v : verts)
        {
            if (v.x < minUV.x) minUV.x = v.x;
            if (v.x > maxUV.x) maxUV.x = v.x;
            if (v.y < minUV.y) minUV.y = v.y;
            if (v.y > maxUV.y) maxUV.y = v.y;
        }
    }
    VectorMath::Vec2 inv = VectorMath::Vec2::Create(1.0f / (maxUV.x - minUV.x), 1.0f / (maxUV.y - minUV.y));
    for (int f : faces)
    {
        VectorMath::Vec2 verts[] =
        {
            mesh.GetVertexUV(mesh.Indices[f * 3], 0),
            mesh.GetVertexUV(mesh.Indices[f * 3 + 1], 0),
            mesh.GetVertexUV(mesh.Indices[f * 3 + 2], 0)
        };
        int w = image.GetWidth() - 1;
        int h = image.GetHeight() - 1;
        for (int i = 0; i < 3; i++)
        {
            auto v = (verts[i] - minUV) * inv;
            auto v1 = (verts[(i + 1) % 3] - minUV) * inv;
            DrawLine(image, (int)(v.x * h), (int)(v.y * w), (int)(v1.x * w), (int)(v1.y * h));
        }
    }
    image.GetImageRef().SaveAsBmpFile(fileName);
}

int main(int argc, const char ** argv)
{
    if (argc < 1)
        printf("usage: filename");
    Mesh mIn;
    Mesh mOut;
    mIn.LoadFromFile(argv[1]);
    VisualizeUV(mIn, 0, String(argv[1]) + ".in.bmp");

    GenerateLightmapUV(&mOut, &mIn, 0.01f);
    VisualizeUV(mOut, 1, String(argv[1]) + ".out.bmp");
}