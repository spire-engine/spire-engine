#ifndef GAME_ENGINE_LIGHTMAP_BAKER_H
#define GAME_ENGINE_LIGHTMAP_BAKER_H

#include "CoreLib/Basic.h"
#include "ObjectSpaceMapSet.h"

namespace GameEngine
{
    class Level;

    void BakeLightmaps(CoreLib::EnumerableDictionary<CoreLib::String, RawObjectSpaceMap>& lightmaps, Level* pLevel);
}

#endif