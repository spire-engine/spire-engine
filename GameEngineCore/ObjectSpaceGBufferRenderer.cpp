#include "ObjectSpaceGBufferRenderer.h"
#include "RenderContext.h"
#include "ViewResource.h"
#include "RenderProcedure.h"
#include "Actor.h"
#include "Material.h"
#include "Engine.h"
#include "StandardViewUniforms.h"

using namespace CoreLib;

namespace GameEngine
{
    class IRenderProcedure;
    class Renderer;

    class ObjectSpaceGBufferRendererImpl : public ObjectSpaceGBufferRenderer
    {
    private:
        HardwareRenderer * hwRenderer;
        ShaderSet shaders;
        RendererService * rendererService;
        ShaderEntryPoint* vsEntryPoint, *psEntryPoint;
        PipelineContext pipeContext;
        RenderStat renderStats;
        RefPtr<CommandBuffer> cmdBuffer, layoutTransferCmdBuffer1, layoutTransferCmdBuffer2;
        ModuleInstance viewParams;
        DeviceMemory uniformMemory;
        StandardViewUniforms viewUniform;
    public:
        ~ObjectSpaceGBufferRendererImpl()
        {
            viewParams = ModuleInstance();
        }
        virtual void Init(HardwareRenderer * hw, RendererService * service, CoreLib::String shaderFileName) override
        {
            hwRenderer = hw;
            rendererService = service;
            vsEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderFileName, "vs_main");
            psEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderFileName, "ps_main");
            pipeContext.Init(hwRenderer, &renderStats);
            cmdBuffer = hwRenderer->CreateCommandBuffer();
            layoutTransferCmdBuffer1 = hwRenderer->CreateCommandBuffer();
            layoutTransferCmdBuffer2 = hwRenderer->CreateCommandBuffer();

            viewUniform.CameraPos.SetZero();
            VectorMath::Matrix4::CreateIdentityMatrix(viewUniform.InvViewProjTransform);
            viewUniform.InvViewTransform = viewUniform.InvViewProjTransform;
            viewUniform.ViewProjectionTransform = viewUniform.InvViewProjTransform;
            viewUniform.ViewTransform = viewUniform.InvViewProjTransform;
            viewUniform.Time = 0.0f;

            auto sharedRes = Engine::Instance()->GetRenderer()->GetSharedResource();
            uniformMemory.Init(hw, BufferUsage::UniformBuffer, true, 22, sharedRes->hardwareRenderer->UniformBufferAlignment());
            sharedRes->CreateModuleInstance(viewParams, Engine::GetShaderCompiler()->LoadSystemTypeSymbol("ViewParams"), &uniformMemory);
            for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
            {
                auto descSet = viewParams.GetDescriptorSet(i);
                descSet->BeginUpdate();
                descSet->Update(1, sharedRes->textureSampler.Ptr());
                descSet->EndUpdate();
            }
            viewParams.SetUniformData(&viewUniform, sizeof(viewUniform));
        }
        virtual void RenderObjectSpaceMap(ArrayView<Texture2D*> dest, ArrayView<StorageFormat> attachmentFormats, Actor * actor, int width, int height) override
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

            auto destTextures = From(dest).Select([](auto x) {return (Texture*)x; }).ToList();
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
            fixStates.DepthCompareFunc = CompareFunc::Disabled;
            fixStates.BlendMode = BlendMode::Replace;
            fixStates.ConsevativeRasterization = true;
            pipeContext.BindEntryPoint(vsEntryPoint, psEntryPoint, renderTargetLayout.Ptr(), &fixStates);
            pipeContext.PushModuleInstance(&viewParams);
            
            layoutTransferCmdBuffer1->BeginRecording();
            layoutTransferCmdBuffer1->TransferLayout(destTextures.GetArrayView(),
                TextureLayoutTransfer::UndefinedToRenderAttachment);
            layoutTransferCmdBuffer1->EndRecording();
            hwRenderer->ExecuteNonRenderCommandBuffers(layoutTransferCmdBuffer1.Ptr());
            
            cmdBuffer->BeginRecording(frameBuffer.Ptr());
            cmdBuffer->SetViewport(0, 0, width, height);
            cmdBuffer->ClearAttachments(frameBuffer.Ptr());
            auto processDrawable = [&](Drawable* drawable)
            {
                cmdBuffer->BindVertexBuffer(drawable->GetMesh()->GetVertexBuffer(), drawable->GetMesh()->vertexBufferOffset);
                cmdBuffer->BindIndexBuffer(drawable->GetMesh()->GetIndexBuffer(), drawable->GetMesh()->indexBufferOffset);

                DescriptorSetBindingArray bindings;
                pipeContext.PushModuleInstance(&drawable->GetMaterial()->MaterialModule);
                pipeContext.PushModuleInstance(drawable->GetTransformModule());
                auto pipeline = pipeContext.GetPipeline(&drawable->GetVertexFormat(), drawable->GetPrimitiveType());
                cmdBuffer->BindPipeline(pipeline->pipeline.Ptr());
                pipeContext.GetBindings(bindings);
                for (int i = 0; i < bindings.Count(); i++)
                    cmdBuffer->BindDescriptorSet(i, bindings[i]);
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
            layoutTransferCmdBuffer2->BeginRecording();
            layoutTransferCmdBuffer2->TransferLayout(destTextures.GetArrayView(),
                TextureLayoutTransfer::RenderAttachmentToSample);
            layoutTransferCmdBuffer2->EndRecording();
            hwRenderer->ExecuteNonRenderCommandBuffers(layoutTransferCmdBuffer2.Ptr());
            hwRenderer->Wait();
        }
    };

    ObjectSpaceGBufferRenderer* CreateObjectSpaceGBufferRenderer()
    {
        return new ObjectSpaceGBufferRendererImpl();
    }
}