#include "WorldRenderPass.h"
#include "HardwareRenderer.h"

namespace GameEngine
{
    using namespace CoreLib;

    class CustomDepthRenderPass : public WorldRenderPass
    {
    protected:
        virtual void SetPipelineStates(FixedFunctionPipelineStates & states) override
        {
            states.DepthCompareFunc = CompareFunc::LessEqual;
        }
    public:
        virtual const char * GetShaderFileName() override
        {
            return "CustomDepthPass.slang";
        }
        virtual const char * GetName() override
        {
            return "CustomDepthPass";
        }
        RenderTargetLayout * CreateRenderTargetLayout() override
        {
            return hwRenderer->CreateRenderTargetLayout(MakeArray(
                AttachmentLayout(TextureUsage::SampledDepthAttachment, DepthBufferFormat)).GetArrayView(), true);
        }
    };

    WorldRenderPass * CreateCustomDepthRenderPass()
    {
        return new CustomDepthRenderPass();
    }
}

