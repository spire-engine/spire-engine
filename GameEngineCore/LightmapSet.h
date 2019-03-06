#ifndef GAME_ENGINE_LIGHTMAP_SET_H
#define GAME_ENGINE_LIGHTMAP_SET_H

#include "CoreLib/Basic.h"
#include "ObjectSpaceMapSet.h"

namespace GameEngine
{
    class Actor;
    class Level;

    struct LightmapSet
    {
        CoreLib::List<RawObjectSpaceMap> Lightmaps;
        CoreLib::Dictionary<Actor*, int> ActorLightmapIds;
        void SaveToFile(Level* level, CoreLib::String fileName);
        void LoadFromFile(Level* level, CoreLib::String fileName);
    };
}

#endif