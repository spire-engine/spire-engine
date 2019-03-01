#ifndef GAME_ENGINE_OBJECT_SPACE_GBUFFER_RENDERER_H
#define GAME_ENGINE_OBJECT_SPACE_GBUFFER_RENDERER_H

#include "CoreLib/Basic.h"
#include "HardwareRenderer.h"
#include "RendererService.h"
#include "Actor.h"

namespace GameEngine
{
    class ObjectSpaceGBufferRenderer : public CoreLib::RefObject
    {
    public:
        virtual void Init(HardwareRenderer * hw, RendererService * service, CoreLib::String shaderFileName) = 0;
        virtual void RenderObjectSpaceMap(CoreLib::ArrayView<Texture2D*> dest, CoreLib::ArrayView<StorageFormat> attachmentFormats, Actor * actor, int width, int height) = 0;
    };

    ObjectSpaceGBufferRenderer* CreateObjectSpaceGBufferRenderer();
}

#endif
