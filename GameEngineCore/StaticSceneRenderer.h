#ifndef GAME_ENGINE_STATIC_SCENE_RENDERER_H
#define GAME_ENGINE_STATIC_SCENE_RENDERER_H

#include "CoreLib/VectorMath.h"
#include "CoreLib/Imaging/Bitmap.h"
#include "StaticScene.h"
#include "LightmapSet.h"

namespace GameEngine
{
    class StaticSceneRenderer : public CoreLib::RefObject
    {
    public:
        virtual void SetCamera(VectorMath::Matrix4 camTransform, float screenFov, int screenWidth, int screenHeight) = 0;
        virtual CoreLib::Imaging::BitmapF& Render(StaticScene* scene, CoreLib::List<RawObjectSpaceMap> & diffuseMaps, LightmapSet & lightMaps) = 0;
    };

    StaticSceneRenderer* CreateStaticSceneRenderer();
}

#endif