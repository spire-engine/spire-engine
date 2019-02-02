#ifndef GAME_ENGINE_RASTERIZER_H
#define GAME_ENGINE_RASTERIZER_H

#include "CoreLib/VectorMath.h"
#include "CoreLib/Basic.h"

namespace GameEngine
{
    class ProjectedTriangle
    {
    public:
        int X0, Y0;
        int X1, Y1;
        int X2, Y2;
        int A0, B0, A1, B1, A2, B2;
    };
    // sets up the triangle based on three post-projection clip space coordinates.
    // it computes the edges equation and perform back-face culling. 
    inline bool SetupTriangle(ProjectedTriangle & tri, VectorMath::Vec2 s0, VectorMath::Vec2 s1, VectorMath::Vec2 s2, int width, int height);
    struct Canvas
    {
        CoreLib::IntSet bitmap;
        int width, height;
        Canvas(int w, int h)
            : width(w), height(h)
        {
            bitmap.SetMax(w * h);
        }
    };
    class Rasterizer
    {
    public:
        static bool SetupTriangle(ProjectedTriangle & tri, VectorMath::Vec2 s0, VectorMath::Vec2 s1, VectorMath::Vec2 s2, int width, int height);
        static int CountOverlap(Canvas& canvas, ProjectedTriangle & tri);
        static void Rasterize(Canvas& canvas, ProjectedTriangle & tri);
    };
}

#endif