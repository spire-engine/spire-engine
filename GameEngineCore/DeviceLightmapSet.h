#ifndef GAME_ENGINE_DEVICE_LIGHT_MAP_SET_H
#define GAME_ENGINE_DEVICE_LIGHT_MAP_SET_H

#include "CoreLib/Basic.h"
#include "HardwareRenderer.h"
#include "LightmapSet.h"

namespace GameEngine
{
    static const int MaxDeviceLightmapResolution = 2048;

    class DeviceLightmapSet : public CoreLib::RefObject
    {
    private:
        CoreLib::List<Texture2DArray*> textureArrays;
        CoreLib::Dictionary<Actor*, uint32_t> deviceLightmapIds;
    public:
        static const uint32_t InvalidDeviceLightmapId = 0xFFFFFFFF;
        void Init(HardwareRenderer* hwRenderer, LightmapSet & lightmapSet);
        CoreLib::ArrayView<Texture*> GetTextureArrayView()
        {
            return CoreLib::ArrayView<Texture*>((Texture**)textureArrays.Buffer(), textureArrays.Count());
        }
        uint32_t GetDeviceLightmapId(Actor* actor)
        {
            uint32_t result = InvalidDeviceLightmapId;
            deviceLightmapIds.TryGetValue(actor, result);
            return result;
        }
        ~DeviceLightmapSet()
        {
            for (auto tex : textureArrays)
                delete tex;
            textureArrays.Clear();
        }
    };
}

#endif