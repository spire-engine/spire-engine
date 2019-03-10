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
        PipelineContext pipeContext[3];
        RenderStat renderStats;
        RefPtr<CommandBuffer> cmdBuffer, layoutTransferCmdBuffer1, layoutTransferCmdBuffer2;
        ModuleInstance viewParams, globalMemoryParams;
        DeviceMemory uniformMemory;
        DeviceMemory globalMemory;
        StandardViewUniforms viewUniform;
        RefPtr<Fence> fence;
    public:
        ~ObjectSpaceGBufferRendererImpl()
        {
            viewParams = ModuleInstance();
            globalMemoryParams = ModuleInstance();
        }
        virtual void Init(HardwareRenderer * hw, RendererService * service, CoreLib::String shaderFileName) override
        {
            hwRenderer = hw;
            rendererService = service;
            fence = hw->CreateFence();
            fence->Reset();
            vsEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderFileName, "vs_main");
            psEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderFileName, "ps_main");
            for (auto &pctx : pipeContext)
                pctx.Init(hwRenderer, &renderStats);
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

            globalMemory.Init(hw, BufferUsage::StorageBuffer, false, 26, sharedRes->hardwareRenderer->StorageBufferAlignment());
            sharedRes->CreateModuleInstance(globalMemoryParams, Engine::GetShaderCompiler()->LoadSystemTypeSymbol("LightmapGBufferGlobalParams"), &uniformMemory);
            for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
            {
                auto descSet = globalMemoryParams.GetDescriptorSet(i);
                descSet->BeginUpdate();
                descSet->Update(0, globalMemory.GetBuffer());
                descSet->EndUpdate();
            }
        }
        virtual void RenderObjectSpaceMap(ArrayView<Texture2D*> dest, ArrayView<StorageFormat> attachmentFormats, Actor * actor, int width, int height) override
        {
            List<AttachmentLayout> attachments;
            RenderAttachments renderAttachments;
            for (int i = 0; i < attachmentFormats.Count(); i++)
            {
                AttachmentLayout layout;
                layout.ImageFormat = attachmentFormats[i];
                if (attachmentFormats[i] == StorageFormat::Depth32)
                    layout.Usage = TextureUsage::DepthAttachment;
                else
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
            
            layoutTransferCmdBuffer1->BeginRecording();
            layoutTransferCmdBuffer1->TransferLayout(destTextures.GetArrayView(),
                TextureLayoutTransfer::UndefinedToRenderAttachment);
            layoutTransferCmdBuffer1->EndRecording();
            hwRenderer->ExecuteNonRenderCommandBuffers(layoutTransferCmdBuffer1.Ptr(), nullptr);
            
            cmdBuffer->BeginRecording(frameBuffer.Ptr());
            cmdBuffer->SetViewport(0, 0, width, height);
            cmdBuffer->ClearAttachments(frameBuffer.Ptr());
            
            for (int k = 0; k < 3; k++)
            {
                FixedFunctionPipelineStates fixStates;
                fixStates.CullMode = CullMode::Disabled;
                fixStates.DepthCompareFunc = CompareFunc::Less;
                fixStates.BlendMode = BlendMode::Replace;
                fixStates.ConsevativeRasterization = false;
                switch (k)
                {
                case 0:
                    fixStates.PolygonFillMode = PolygonMode::Fill;
                    break;
                case 1:
                    fixStates.PolygonFillMode = PolygonMode::Line;
                    break;
                case 2:
                    fixStates.PolygonFillMode = PolygonMode::Point;
                    break;
                }
                pipeContext[k].BindEntryPoint(vsEntryPoint, psEntryPoint, renderTargetLayout.Ptr(), &fixStates);
                pipeContext[k].PushModuleInstance(&viewParams);
                pipeContext[k].PushModuleInstance(&globalMemoryParams);

                auto processDrawable = [&](Drawable* drawable)
                {
                    cmdBuffer->BindVertexBuffer(drawable->GetMesh()->GetVertexBuffer(), drawable->GetMesh()->vertexBufferOffset);
                    cmdBuffer->BindIndexBuffer(drawable->GetMesh()->GetIndexBuffer(), drawable->GetMesh()->indexBufferOffset);

                    DescriptorSetBindingArray bindings;
                    pipeContext[k].PushModuleInstance(&drawable->GetMaterial()->MaterialModule);
                    pipeContext[k].PushModuleInstance(drawable->GetTransformModule());
                    auto pipeline = pipeContext[k].GetPipeline(&drawable->GetVertexFormat(), drawable->GetPrimitiveType());
                    cmdBuffer->BindPipeline(pipeline->pipeline.Ptr());
                    pipeContext[k].GetBindings(bindings);
                    for (int i = 0; i < bindings.Count(); i++)
                        cmdBuffer->BindDescriptorSet(i, bindings[i]);
                    auto range = drawable->GetElementRange();
                    cmdBuffer->DrawIndexed(range.StartIndex, range.Count);
                    pipeContext[k].PopModuleInstance();
                    pipeContext[k].PopModuleInstance();
                };
                for (auto drawable : sink.GetDrawables(false))
                {
                    processDrawable(drawable);
                }
                for (auto drawable : sink.GetDrawables(true))
                {
                    processDrawable(drawable);
                }
            }

            cmdBuffer->EndRecording();
            hwRenderer->ExecuteRenderPass(frameBuffer.Ptr(), MakeArrayView(cmdBuffer.Ptr()), nullptr);
            layoutTransferCmdBuffer2->BeginRecording();
            layoutTransferCmdBuffer2->TransferLayout(destTextures.GetArrayView(),
                TextureLayoutTransfer::RenderAttachmentToSample);
            layoutTransferCmdBuffer2->EndRecording();

            hwRenderer->ExecuteNonRenderCommandBuffers(layoutTransferCmdBuffer2.Ptr(), fence.Ptr());
            fence->Wait();
            fence->Reset();
        }
    };

    ObjectSpaceGBufferRenderer* CreateObjectSpaceGBufferRenderer()
    {
        return new ObjectSpaceGBufferRendererImpl();
    }
}