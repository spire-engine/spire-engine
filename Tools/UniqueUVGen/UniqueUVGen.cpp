
#include "LightmapUVGeneration.h"
#include "Mesh.h"
#include "CoreLib/Imaging/Bitmap.h"

using namespace CoreLib;
using namespace GameEngine;

void SetPixel(CoreLib::Imaging::BitmapF & img, int x, int y)
{
    int offset = img.GetWidth() * y + x;
    auto pixels = img.GetPixels();
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


int main(int argc, const char ** argv)
{
    if (argc < 1)
        printf("usage: filename");
    Mesh mIn;
    Mesh mOut;
    mIn.LoadFromFile(argv[1]);
    GenerateLightmapUV(&mOut, &mIn, 0.01f);
    // draw new uv
    CoreLib::Imaging::BitmapF image(1024, 1024);
    auto pixels = image.GetPixels();
    memset(pixels, 0, image.GetWidth()*image.GetHeight() * sizeof(VectorMath::Vec4));
    for (int f = 0; f < mOut.GetVertexCount() / 3; f++)
    {
        VectorMath::Vec2 verts[] = 
        {
            mOut.GetVertexUV(f * 3, 1),
            mOut.GetVertexUV(f * 3 + 1, 1),
            mOut.GetVertexUV(f * 3 + 2, 1)
        };
        int w = image.GetWidth() - 1;
        int h = image.GetHeight() - 1;
        for (int i = 0; i < 3; i++)
            DrawLine(image, (int)(verts[i].x * h), (int)(verts[i].y * w), (int)(verts[(i+1)%3].x * w), (int)(verts[(i+1)%3].y * h));
    }
    image.GetImageRef().SaveAsBmpFile(CoreLib::String(argv[1]) + ".bmp");
}