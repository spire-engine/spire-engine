#ifndef GAME_ENGINE_RENDER_PROCEDURE_H
#define GAME_ENGINE_RENDER_PROCEDURE_H

#include "RenderContext.h"

namespace GameEngine
{
	class RendererService;

	struct RenderProcedureParameters
	{
		Renderer * renderer;
		RenderStat * renderStats;
		View view;
		Level * level;
		RendererService * rendererService;
		bool isEditorMode = false;
	};

	class IRenderProcedure : public CoreLib::RefObject
	{
	public:
		virtual void Init(Renderer * renderer, ViewResource * pViewRes) = 0;
		virtual void UpdateSharedResourceBinding() = 0;
        virtual void UpdateSceneResourceBinding(SceneResource* sceneRes) = 0;
		virtual void Run(const RenderProcedureParameters & params) = 0;
		virtual RenderTarget* GetOutput() = 0;
        virtual CoreLib::String GetName() = 0;
	};

    IRenderProcedure * CreateLightmapDebugViewRenderProcedure();
    IRenderProcedure * CreateLightProbeRenderProcedure();
    IRenderProcedure * CreateWorldNormalViewRenderProcedure();
	IRenderProcedure * CreateStandardRenderProcedure(bool toneMapping, bool useEnvMap);
}

#endif