#ifndef GAME_ENGINE_SSAO_H
#define GAME_ENGINE_SSAO_H

#include "RenderContext.h"

namespace GameEngine
{
	class SSAOImpl;

	class SSAO
	{
	private:
		SSAOImpl * impl;
	public:
		SSAO();
		~SSAO();
		void Init(RendererSharedResource * sharedRes, const char * depthSource);
		void Resize(int w, int h);
		void RegisterWork();
		char * GetResultName();
	};
}

#endif