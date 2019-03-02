#ifndef GAME_ENGINE_DEBUG_GRAPHICS_H
#define GAME_ENGINE_DEBUG_GRAPHICS_H

#include "CoreLib/VectorMath.h"
#include "CoreLib/Basic.h"

namespace GameEngine
{
    class Drawable;

    class DebugGraphics : public CoreLib::RefObject
    {
    public:
        virtual void Clear() = 0;
        virtual void AddLine(VectorMath::Vec4 color, VectorMath::Vec3 v0, VectorMath::Vec3 v1) = 0;
        virtual void AddTriangle(VectorMath::Vec4 color, VectorMath::Vec3 v0, VectorMath::Vec3 v1, VectorMath::Vec3 v2) = 0;
        virtual CoreLib::ArrayView<Drawable*> GetDrawables() = 0;
    };

    DebugGraphics* CreateDebugGraphics();
}

#endif