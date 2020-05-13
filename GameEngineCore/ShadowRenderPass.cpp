#include "WorldRenderPass.h"
#include "HardwareRenderer.h"

namespace GameEngine
{
	using namespace CoreLib;

	class ShadowRenderPass : public WorldRenderPass
	{
	protected:
		virtual void SetPipelineStates(FixedFunctionPipelineStates & states) override
		{
			states.DepthCompareFunc = CompareFunc::Less;
            states.cullMode = CullMode::Disabled;
			states.EnablePolygonOffset = true;
			states.PolygonOffsetUnits = 10.0f;
			states.PolygonOffsetFactor = 2.0f;
		}
	public:
		virtual const char * GetShaderFileName() override
		{
			return "ShadowPass.slang";
		}
		virtual const char * GetName() override
		{
			return "ShadowPass";
		}
		virtual RenderTargetLayout * CreateRenderTargetLayout() override
		{
			return sharedRes->shadowMapResources.shadowMapRenderTargetLayout.Ptr();
		}
	};

	WorldRenderPass * CreateShadowRenderPass()
	{
		return new ShadowRenderPass();
	}
}

