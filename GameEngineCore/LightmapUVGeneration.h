#ifndef GAME_ENGINE_LIGHTMAP_UV_GENERATION
#define GAME_ENGINE_LIGHTMAP_UV_GENERATION

#include "Mesh.h"

namespace GameEngine
{
    void GenerateLightmapUV(Mesh* meshOut, Mesh* meshIn, float uvPadding);
}

#endif