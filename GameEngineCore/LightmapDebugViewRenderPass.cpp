#include "WorldRenderPass.h"
#include "Material.h"
#include "Mesh.h"
#include "CoreLib/LibIO.h"

using namespace CoreLib;
using namespace CoreLib::IO;

namespace GameEngine
{
    class LightmapDebugViewRenderPass : public WorldRenderPass
    {
    public:
        const char * GetShaderFileName() override
        {
            return "LightmapVisualizationPass.slang";
        }
        RenderTargetLayout * CreateRenderTargetLayout() override
        {
            return hwRenderer->CreateRenderTargetLayout(MakeArray(
                AttachmentLayout(TextureUsage::ColorAttachment, StorageFormat::RGBA_F16),
                AttachmentLayout(TextureUsage::DepthAttachment, DepthBufferFormat)).GetArrayView(), true);
        }
        virtual void SetPipelineStates(FixedFunctionPipelineStates & state)
        {
            state.blendMode = BlendMode::AlphaBlend;
            state.DepthCompareFunc = CompareFunc::LessEqual;
        }
        virtual char * GetName() override
        {
            return "LightmapDebugView";
        }
    };

    WorldRenderPass * CreateLightmapDebugViewRenderPass()
    {
        return new LightmapDebugViewRenderPass();
    }
}