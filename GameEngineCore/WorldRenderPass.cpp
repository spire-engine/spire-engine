#include "WorldRenderPass.h"
#include "Material.h"
#include "Engine.h"
using namespace CoreLib;

namespace GameEngine
{
	void WorldRenderPass::Bind()
	{
		sharedRes->pipelineManager.BindEntryPoint(vertShader, fragShader, renderTargetLayout.Ptr(), &fixedFunctionStates);
	}

	CoreLib::RefPtr<WorldPassRenderTask> WorldRenderPass::CreateInstance(RenderOutput * output, bool clearOutput)
	{
		CoreLib::RefPtr<WorldPassRenderTask> result = new WorldPassRenderTask();
#ifdef _DEBUG
		if (renderPassId == -1)
			throw CoreLib::InvalidProgramException("RenderPass must be registered before calling CreateInstance().");
#endif

		auto &rs = *result;
		rs.viewport.X = rs.viewport.Y = 0;
		rs.pass = this;
		rs.renderPassId = renderPassId;
		rs.renderOutput = output;
		rs.fixedFunctionStates = &fixedFunctionStates;
		rs.clearOutput = clearOutput;
		return result;
	}

	int WorldRenderPass::GetShaderId()
	{
		return fragShader->Id;
	}

	AsyncCommandBuffer * WorldRenderPass::AllocCommandBuffer()
	{
		if (poolAllocPtr == commandBufferPool.Count())
		{
			commandBufferPool.Add(new AsyncCommandBuffer(hwRenderer));
		}
		return commandBufferPool[poolAllocPtr++].Ptr();
	}

	void WorldRenderPass::Create(Renderer * renderer)
	{
		renderTargetLayout = CreateRenderTargetLayout();
        vertShader = Engine::GetShaderCompiler()->LoadShaderEntryPoint(GetShaderFileName(), "vs_main");
        fragShader = Engine::GetShaderCompiler()->LoadShaderEntryPoint(GetShaderFileName(), "ps_main");
        SetPipelineStates(fixedFunctionStates);
		renderPassId = renderer->RegisterWorldRenderPass(GetShaderId());
	}
	WorldRenderPass::~WorldRenderPass()
	{
	}
}

