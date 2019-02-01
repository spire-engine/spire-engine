#ifndef GAME_ENGINE_LIGHTMAP_UV_GENERATION
#define GAME_ENGINE_LIGHTMAP_UV_GENERATION

namespace GameEngine
{
    class Mesh;
    void GenerateLightmapUV(Mesh* meshOut, Mesh* meshIn, float uvPadding);
}

#endif