#ifndef GAME_ENGINE_LIGHTMAP_SET_H
#define GAME_ENGINE_LIGHTMAP_SET_H

#include "CoreLib/Basic.h"
#include "ObjectSpaceMapSet.h"

namespace GameEngine
{
    class Actor;
    struct LightmapSet
    {
        CoreLib::List<RawObjectSpaceMap> Lightmaps;
        CoreLib::Dictionary<Actor*, int> ActorLightmapIds;
    };
}

#endif