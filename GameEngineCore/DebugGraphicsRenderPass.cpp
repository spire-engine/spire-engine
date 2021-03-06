#include "WorldRenderPass.h"
#include "Material.h"
#include "Mesh.h"
#include "CoreLib/LibIO.h"

using namespace CoreLib;
using namespace CoreLib::IO;

namespace GameEngine
{
    class DebugGraphicsRenderPass : public WorldRenderPass
    {
    public:
        const char * GetShaderFileName() override
        {
            return "DebugGraphicsPass.slang";
        }
        RenderTargetLayout * CreateRenderTargetLayout() override
        {
            return hwRenderer->CreateRenderTargetLayout(MakeArray(
                AttachmentLayout(TextureUsage::ColorAttachment, StorageFormat::RGBA_F16),
                AttachmentLayout(TextureUsage::DepthAttachment, DepthBufferFormat)).GetArrayView(), false);
        }
        virtual void SetPipelineStates(FixedFunctionPipelineStates & state) override
        {
            state.blendMode = BlendMode::AlphaBlend;
            state.DepthCompareFunc = CompareFunc::LessEqual;
            state.cullMode = CullMode::Disabled;
        }
        virtual const char * GetName() override
        {
            return "DebugGraphics";
        }
    };

    WorldRenderPass * CreateDebugGraphicsRenderPass()
    {
        return new DebugGraphicsRenderPass();
    }
}