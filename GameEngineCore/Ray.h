#ifndef GAME_ENGINE_RAY_H
#define GAME_ENGINE_RAY_H

#include <float.h>
#include "CoreLib/VectorMath.h"

namespace GameEngine
{
    struct Ray
    {
        VectorMath::Vec3 Origin;
        VectorMath::Vec3 Dir;
        float tMax = FLT_MAX;
    };
}

#endif