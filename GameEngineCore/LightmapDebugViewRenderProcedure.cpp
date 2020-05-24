#include "Engine.h"
#include "WorldRenderPass.h"
#include "PostRenderPass.h"
#include "Renderer.h"
#include "RenderPassRegistry.h"
#include "FrustumCulling.h"
#include "RenderProcedure.h"
#include "StandardViewUniforms.h"

using namespace VectorMath;

namespace GameEngine
{
    class LightmapDebugViewUniforms
    {
    public:
        VectorMath::Matrix4 viewProjMatrix;
    };

    class LightmapDebugViewRenderProcedure : public IRenderProcedure
    {
    private:
        RendererSharedResource * sharedRes = nullptr;
        ViewResource * viewRes = nullptr;

        RefPtr<WorldRenderPass> forwardRenderPass;
        RefPtr<WorldRenderPass> customDepthRenderPass;
        RefPtr<PostRenderPass> editorOutlinePass;

        RenderOutput * forwardBaseOutput = nullptr;
        RenderOutput * customDepthOutput = nullptr;

        LightmapDebugViewUniforms viewUniform;

        RefPtr<WorldPassRenderTask> forwardBaseInstance, customDepthPassInstance;

        DeviceMemory renderPassUniformMemory;
        SharedModuleInstances sharedModules;
        ModuleInstance lightmapViewParams, standardViewParams;
        DrawableSink sink;
        List<Drawable*> reorderBuffer, drawableBuffer;
        RefPtr<Buffer> lightmapSizeBuffer, lightmapColorBuffer;
        DeviceLightmapSet* lightmapSet = nullptr;
    public:
        ~LightmapDebugViewRenderProcedure()
        {
            if (forwardBaseOutput)
                viewRes->DestroyRenderOutput(forwardBaseOutput);
        }
        virtual RenderTarget* GetOutput() override
        {
            if (Engine::Instance()->GetEngineMode() == EngineMode::Editor)
                return viewRes->LoadSharedRenderTarget("editorColor", StorageFormat::RGBA_8).Ptr();
            else
                return viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16, 1.0f, 1024, 1024, true).Ptr();
        }
        virtual CoreLib::String GetName() override
        {
            return "Lightmap Visualization";
        }
        virtual void UpdateSharedResourceBinding() override
        {
            for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
            {
                auto descSet = standardViewParams.GetDescriptorSet(i);
                descSet->BeginUpdate();
                descSet->Update(1, sharedRes->textureSampler.Ptr());
                descSet->EndUpdate();
            }
        }
        virtual void UpdateSceneResourceBinding(SceneResource* sceneRes) override
        {
            lightmapSet = sceneRes->deviceLightmapSet.Ptr();
            if (lightmapSet)
            {
                auto texArray = lightmapSet->GetTextureArrayView();
                int maxLayers = 0;
                Array<VectorMath::Vec2i, 16> sizes;
                for (auto & t : texArray)
                {
                    if (t)
                    {
                        int w = 0, h = 0, l = 0;
                        dynamic_cast<Texture2DArray*>(t)->GetSize(w, h, l);
                        maxLayers = Math::Max(maxLayers, l);
                        sizes.Add(VectorMath::Vec2i::Create(w, h));
                    }
                    else
                        sizes.Add(VectorMath::Vec2i::Create(1, 1));
                }
                auto lightmapSizeBufferStructInfo = BufferStructureInfo(sizeof(uint32_t) * 2, texArray.Count());
                lightmapSizeBuffer = sceneRes->hardwareRenderer->CreateBuffer(BufferUsage::StorageBuffer,
                    sizeof(uint32_t) * 2 * texArray.Count(), &lightmapSizeBufferStructInfo);
                auto lightmapColorBufferStructInfo = BufferStructureInfo(sizeof(float) * 4, maxLayers);
                lightmapColorBuffer = sceneRes->hardwareRenderer->CreateBuffer(
                    BufferUsage::StorageBuffer, sizeof(float) * 4 * maxLayers, &lightmapColorBufferStructInfo);
                lightmapSizeBuffer->SetData(sizes.Buffer(), (int)(sizes.Count() * sizeof(VectorMath::Vec2i)));
                Random random;
                List<VectorMath::Vec4> colors;
                colors.SetSize(maxLayers);
                for (int i = 0; i < maxLayers; i++)
                {
                    colors[i] = VectorMath::Vec4::Create(random.NextFloat()*0.5f + 0.3f, random.NextFloat()*0.5f + 0.3f, random.NextFloat()*0.5f + 0.3f, 1.0f);
                }
                lightmapColorBuffer->SetData(colors.Buffer(), (int)(colors.Count() * sizeof(VectorMath::Vec4)));
                for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
                {
                    auto descSet = lightmapViewParams.GetDescriptorSet(i);
                    descSet->BeginUpdate();
                    descSet->Update(1, lightmapSizeBuffer.Ptr());
                    descSet->Update(2, lightmapColorBuffer.Ptr());
                    descSet->EndUpdate();
                }
            }
        }

        virtual void Init(Renderer * renderer, ViewResource * pViewRes) override
        {
            viewRes = pViewRes;
            sharedRes = renderer->GetSharedResource();

            forwardRenderPass = CreateLightmapDebugViewRenderPass();
            forwardRenderPass->Init(renderer);

            customDepthRenderPass = CreateCustomDepthRenderPass();
            customDepthRenderPass->Init(renderer);

            forwardBaseOutput = viewRes->CreateRenderOutput(
                forwardRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16, 1.0f, 1024, 1024, true),
                viewRes->LoadSharedRenderTarget("depthBuffer", DepthBufferFormat)
            );
            customDepthOutput = viewRes->CreateRenderOutput(
                customDepthRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("customDepthBuffer", DepthBufferFormat));

            renderPassUniformMemory.Init(sharedRes->hardwareRenderer.Ptr(), BufferUsage::UniformBuffer, true, 22, sharedRes->hardwareRenderer->UniformBufferAlignment(), nullptr);
            sharedRes->CreateModuleInstance(lightmapViewParams, Engine::GetShaderCompiler()->LoadTypeSymbol("LightmapVisualizationPass.slang", "LightmapVisualizationViewParams"), &renderPassUniformMemory);
            sharedRes->CreateModuleInstance(standardViewParams, Engine::GetShaderCompiler()->LoadSystemTypeSymbol("ViewParams"), &renderPassUniformMemory);
            UpdateSharedResourceBinding();
            sharedModules.View = &standardViewParams;

            if (Engine::Instance()->GetEngineMode() == EngineMode::Editor)
            {
                editorOutlinePass = CreateOutlinePostRenderPass(viewRes);
                editorOutlinePass->SetSource(MakeArray(
                    PostPassSource("litColor", StorageFormat::RGBA_F16),
                    PostPassSource("customDepthBuffer", DepthBufferFormat),
                    PostPassSource("editorColor", StorageFormat::RGBA_8)
                ).GetArrayView());
                editorOutlinePass->Init(renderer);
            }
            auto lightmapSizeBufferStructInfo = BufferStructureInfo(sizeof(uint32_t) * 2, 2);
            lightmapSizeBuffer = sharedRes->hardwareRenderer->CreateBuffer(
                BufferUsage::StorageBuffer, 16, &lightmapSizeBufferStructInfo);
            lightmapColorBuffer = lightmapSizeBuffer;
            for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
            {
                auto descSet = lightmapViewParams.GetDescriptorSet(i);
                descSet->BeginUpdate();
                descSet->Update(1, lightmapSizeBuffer.Ptr());
                descSet->Update(2, lightmapColorBuffer.Ptr());
                descSet->EndUpdate();
            }
        }

        ArrayView<Drawable*> GetDrawable(DrawableSink * objSink, CullFrustum cf, bool isCustomDepth)
        {
            drawableBuffer.Clear();
            for (auto obj : objSink->GetDrawables(true))
            {
                if (isCustomDepth && !obj->RenderCustomDepth)
                    continue;
                if (cf.IsBoxInFrustum(obj->Bounds))
                    drawableBuffer.Add(obj);
            }
            for (auto obj : objSink->GetDrawables(false))
            {
                if (isCustomDepth && !obj->RenderCustomDepth)
                    continue;
                if (cf.IsBoxInFrustum(obj->Bounds))
                    drawableBuffer.Add(obj);
            }
            return drawableBuffer.GetArrayView();
        }

        virtual void Run(const RenderProcedureParameters & params) override
        {
            int w = 0, h = 0;
            auto hardwareRenderer = params.renderer->GetHardwareRenderer();
            hardwareRenderer->BeginJobSubmission();

            forwardRenderPass->ResetInstancePool();
            forwardBaseOutput->GetSize(w, h);
            forwardBaseInstance = forwardRenderPass->CreateInstance(forwardBaseOutput, true);

            customDepthRenderPass->ResetInstancePool();
            customDepthPassInstance = customDepthRenderPass->CreateInstance(customDepthOutput, true);
            float aspect = w / (float)h;
            Matrix4 mainProjMatrix;
            Matrix4::CreatePerspectiveMatrixFromViewAngle(mainProjMatrix,
                params.view.FOV, aspect,
                params.view.ZNear, params.view.ZFar, ClipSpaceType::ZeroToOne);
            Matrix4::Multiply(viewUniform.viewProjMatrix, mainProjMatrix, params.view.Transform);
  
            StandardViewUniforms standardViewUniforms;
            standardViewUniforms.CameraPos = params.view.Position;
            standardViewUniforms.ViewTransform = params.view.Transform;
            Matrix4 invProjMatrix;
            mainProjMatrix.Inverse(invProjMatrix);
            Matrix4::Multiply(standardViewUniforms.ViewProjectionTransform, mainProjMatrix, standardViewUniforms.ViewTransform);
            standardViewUniforms.ViewTransform.Inverse(standardViewUniforms.InvViewTransform);
            standardViewUniforms.ViewProjectionTransform.Inverse(standardViewUniforms.InvViewProjTransform);
            standardViewUniforms.Time = Engine::Instance()->GetTime();


            GetDrawablesParameter getDrawableParam;
            getDrawableParam.CameraDir = params.view.GetDirection();
            getDrawableParam.IsEditorMode = params.isEditorMode;
            getDrawableParam.IsBaking = true;
            getDrawableParam.CameraPos = params.view.Position;
            getDrawableParam.rendererService = params.rendererService;
            getDrawableParam.sink = &sink;

            sink.Clear();

            CoreLib::Graphics::BBox levelBounds;
            // initialize bounds to a small extent to prevent error
            levelBounds.Min = Vec3::Create(-10.0f);
            levelBounds.Max = Vec3::Create(10.0f);
            
            for (auto & actor : params.level->Actors)
            {
                levelBounds.Union(actor.Value->Bounds);
                int lastTransparentDrawableCount = sink.GetDrawables(true).Count();
                int lastOpaqueDrawableCount = sink.GetDrawables(false).Count();

                // obtain drawables from actor
                actor.Value->GetDrawables(getDrawableParam);

                // if a LightmapSet is available, update drawable's lightmapIndex uniform parameter (do a CPU--GPU memory transfer if needed)
                if (lightmapSet)
                {
                    uint32_t lightmapIndex = lightmapSet->GetDeviceLightmapId(actor.Value.Ptr());
                    auto transparentDrawables = sink.GetDrawables(true);
                    for (int i = lastTransparentDrawableCount; i < transparentDrawables.Count(); i++)
                    {
                        transparentDrawables.Buffer()[i]->UpdateLightmapIndex(lightmapIndex);
                    }
                    auto opaqueDrawables = sink.GetDrawables(false);
                    for (int i = lastOpaqueDrawableCount; i < opaqueDrawables.Count(); i++)
                    {
                        opaqueDrawables.Buffer()[i]->UpdateLightmapIndex(lightmapIndex);
                    }
                }
            }

            // collect light data and render shadow maps
            standardViewParams.SetUniformData(&standardViewUniforms, (int)sizeof(standardViewUniforms));
            lightmapViewParams.SetUniformData(&viewUniform, (int)sizeof(viewUniform));
            auto cameraCullFrustum = CullFrustum(params.view.GetFrustum(aspect));

            // custom depth pass
            Array<Texture*, 8> textures;
            customDepthOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            customDepthRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&standardViewParams);
            customDepthPassInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, cameraCullFrustum, true));
            sharedRes->pipelineManager.PopModuleInstance();
            customDepthPassInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);

            // execute visualization pass
            forwardBaseOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            forwardRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&lightmapViewParams);
            forwardBaseInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, cameraCullFrustum, false));
            sharedRes->pipelineManager.PopModuleInstance();
            forwardBaseInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);

            if (Engine::Instance()->GetEngineMode() == EngineMode::Editor)
            {
                forwardBaseOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
                editorOutlinePass->CreateInstance(sharedModules)->Execute(hardwareRenderer, *params.renderStats,
                    PipelineBarriers::MemoryAndImage);
            }
            hardwareRenderer->EndJobSubmission(nullptr);
        }
    };

    IRenderProcedure * CreateLightmapDebugViewRenderProcedure()
    {
        return new LightmapDebugViewRenderProcedure();
    }
}