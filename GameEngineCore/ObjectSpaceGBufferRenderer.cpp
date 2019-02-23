#include "RenderContext.h"
#include "ViewResource.h"
#include "RenderProcedure.h"
#include "Actor.h"
#include "Material.h"
#include "Engine.h"

using namespace CoreLib;
namespace GameEngine
{
    class IRenderProcedure;
    class Renderer;

    class ObjectSpaceGBufferRenderer : public CoreLib::RefObject
    {
    private:
        HardwareRenderer * hwRenderer;
        ShaderSet shaders;
        RendererService * rendererService;
        ShaderEntryPoint* vsEntryPoint, *psEntryPoint;
        PipelineContext pipeContext;
        RenderStat renderStats;
        RefPtr<CommandBuffer> cmdBuffer;
    public:
        ObjectSpaceGBufferRenderer(HardwareRenderer * hw, RendererService * service, CoreLib::String shaderFileName)
        {
            rendererService = service;
            vsEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderFileName, "vsMain");
            psEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderFileName, "psMain");
            pipeContext.Init(hwRenderer, &renderStats);
            cmdBuffer = hwRenderer->CreateCommandBuffer();
        }
        void RenderObjectSpaceMap(ArrayView<Texture2D*> dest, ArrayView<StorageFormat> attachmentFormats, Actor * actor)
        {
            List<AttachmentLayout> attachments;
            RenderAttachments renderAttachments;
            for (int i = 0; i < attachmentFormats.Count(); i++)
            {
                AttachmentLayout layout;
                layout.ImageFormat = attachmentFormats[i];
                layout.Usage = TextureUsage::ColorAttachment;
                attachments.Add(layout);
                renderAttachments.SetAttachment(i, dest[i]);
            }
            RefPtr<RenderTargetLayout> renderTargetLayout = hwRenderer->CreateRenderTargetLayout(attachments.GetArrayView());
            RefPtr<FrameBuffer> frameBuffer = renderTargetLayout->CreateFrameBuffer(renderAttachments);
            
            DrawableSink sink;
            GetDrawablesParameter param;
            param.sink = &sink;
            param.rendererService = rendererService;
            param.IsEditorMode = false;
            param.CameraDir = VectorMath::Vec3::Create(0.0f, 0.0f, -1.0f);
            param.CameraPos = actor->GetPosition();
            param.UseSkeleton = false;
            actor->GetDrawables(param);
            
            FixedFunctionPipelineStates fixStates;
            fixStates.CullMode = CullMode::Disabled;
            pipeContext.BindEntryPoint(vsEntryPoint, psEntryPoint, renderTargetLayout.Ptr(), &fixStates);
            cmdBuffer->BeginRecording(frameBuffer.Ptr());

            auto processDrawable = [&](Drawable* drawable)
            {
                auto pipeline = pipeContext.GetPipeline(&drawable->GetVertexFormat());
                cmdBuffer->BindPipeline(pipeline->pipeline.Ptr());
                DescriptorSetBindingArray bindings;
                pipeContext.PushModuleInstance(&drawable->GetMaterial()->MaterialModule);
                pipeContext.PushModuleInstance(drawable->GetTransformModule());
                pipeContext.GetBindings(bindings);
                for (int i = 0; i < bindings.Count(); i++)
                    cmdBuffer->BindDescriptorSet(i, bindings[i]);
                cmdBuffer->BindVertexBuffer(drawable->GetMesh()->GetVertexBuffer(), drawable->GetMesh()->vertexBufferOffset);
                cmdBuffer->BindIndexBuffer(drawable->GetMesh()->GetIndexBuffer(), drawable->GetMesh()->indexBufferOffset);
                auto range = drawable->GetElementRange();
                cmdBuffer->DrawIndexed(range.StartIndex, range.Count);
                pipeContext.PopModuleInstance();
                pipeContext.PopModuleInstance();
            };
            for (auto drawable : sink.GetDrawables(false))
            {
                processDrawable(drawable);
            }
            for (auto drawable : sink.GetDrawables(true))
            {
                processDrawable(drawable);
            }
            cmdBuffer->EndRecording();
            hwRenderer->ExecuteRenderPass(frameBuffer.Ptr(), MakeArrayView(cmdBuffer.Ptr()), nullptr);
            hwRenderer->Wait();
        }
    };
}