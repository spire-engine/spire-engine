#ifndef GAME_ENGINE_LIGHT_PROBE_RENDERER_H
#define GAME_ENGINE_LIGHT_PROBE_RENDERER_H

#include "RenderContext.h"
#include "ViewResource.h"
#include "RenderProcedure.h"
#include "ShaderCompiler.h"

namespace GameEngine
{
	class IRenderProcedure;
	class Renderer;

	class LightProbeRenderer : public CoreLib::RefObject
	{
	private:
		Renderer * renderer;
		RendererService * renderService;
		IRenderProcedure* renderProc;
		ViewResource * viewRes = nullptr;
        ShaderSet copyShaderSet;
        ShaderSet prefilterShaderSet;
		CoreLib::RefPtr<TextureCube> tempEnv;
        CoreLib::RefPtr<Fence> fence;
	public:
		LightProbeRenderer(Renderer * renderer, RendererService * renderService, IRenderProcedure * pRenderProc, ViewResource * pViewRes);
		void RenderLightProbe(TextureCubeArray* dest, int id, Level * level, VectorMath::Vec3 position);
	};
}

#endif