#ifndef GAME_ENGINE_SSAO_H
#define GAME_ENGINE_SSAO_H

#include "CoreLib/VectorMath.h"

namespace GameEngine
{
    struct SSAOUniforms
    {
        VectorMath::Matrix4 ProjMatrix, InvProjMatrix;
        int width = 1024, height = 1024;
        int blurRadius = 7;
        float aoRadius = 50.0f, aoPower = 4.0f;
    };
}

#endif