#ifndef GAME_ENGINE_LIGHTMAP_UV_GENERATION
#define GAME_ENGINE_LIGHTMAP_UV_GENERATION

namespace GameEngine
{
    class Mesh;
    bool GenerateLightmapUV(Mesh* meshOut, Mesh* meshIn, int textureSize, int paddingPixels);
}

#endif