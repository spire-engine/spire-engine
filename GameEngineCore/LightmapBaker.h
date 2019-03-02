#ifndef GAME_ENGINE_LIGHTMAP_BAKER_H
#define GAME_ENGINE_LIGHTMAP_BAKER_H

#include "CoreLib/Basic.h"
#include "LightmapSet.h"

namespace GameEngine
{
    class Level;
    struct LightmapBakingSettings
    {
        float ResolutionScale = 16.0f;
        int MinResolution = 8;
        int MaxResolution = 1024;
        int SampleCount = 16;
        float Epsilon = 1e-5f;
        float ShadowBias = 1e-2f;
    };
    void BakeLightmaps(LightmapSet& lightmaps, const LightmapBakingSettings & settings, Level* pLevel);
}

#endif