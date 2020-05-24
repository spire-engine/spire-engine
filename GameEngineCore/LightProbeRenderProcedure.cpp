#include "Engine.h"
#include "WorldRenderPass.h"
#include "PostRenderPass.h"
#include "Renderer.h"
#include "RenderPassRegistry.h"
#include "DirectionalLightActor.h"
#include "AtmosphereActor.h"
#include "FrustumCulling.h"
#include "RenderProcedure.h"
#include "StandardViewUniforms.h"
#include "LightingData.h"

using namespace VectorMath;

namespace GameEngine
{
    struct BuildTiledLightListUniforms
    {
        VectorMath::Matrix4 invProjMatrix;
        VectorMath::Matrix4 viewMatrix;
        int width, height;
        int lightCount, lightProbeCount;
    };

    class LightProbeRenderProcedure : public IRenderProcedure
    {
    private:
        RendererSharedResource * sharedRes = nullptr;
        ViewResource * viewRes = nullptr;

        RefPtr<WorldRenderPass> shadowRenderPass;
        RefPtr<WorldRenderPass> forwardRenderPass;
        RefPtr<WorldRenderPass> customDepthRenderPass;
        RefPtr<PostRenderPass> atmospherePass;

        RenderOutput * forwardBaseOutput = nullptr;
        RenderOutput * transparentAtmosphereOutput = nullptr;
        RenderOutput * preZOutput = nullptr;
        RenderOutput * preZTransparentOutput = nullptr;

        StandardViewUniforms viewUniform;

        RefPtr<WorldPassRenderTask> forwardBaseInstance, transparentPassInstance, 
            preZPassInstance, preZPassTransparentInstance;

        ComputeKernel* lightListBuildingComputeKernel;

        RefPtr<ComputeTaskInstance> lightListBuildingComputeTaskInstance;

        DeviceMemory renderPassUniformMemory;
        SharedModuleInstances sharedModules;
        ModuleInstance viewParams;
        CoreLib::List<ModuleInstance> shadowViewInstances;

        DrawableSink sink;

        List<Drawable*> reorderBuffer, drawableBuffer;
        LightingEnvironment lighting;
        AtmosphereParameters lastAtmosphereParams;
        bool useAtmosphere = false;
    public:
        ~LightProbeRenderProcedure()
        {
            if (forwardBaseOutput)
                viewRes->DestroyRenderOutput(forwardBaseOutput);
            if (transparentAtmosphereOutput)
                viewRes->DestroyRenderOutput(transparentAtmosphereOutput);
            lightListBuildingComputeTaskInstance = nullptr;
        }
        virtual CoreLib::String GetName() override
        {
            return "Light Probe Output";
        }
        virtual RenderTarget* GetOutput() override
        {
            if (useAtmosphere)
                return viewRes->LoadSharedRenderTarget("litAtmosphereColor", StorageFormat::RGBA_F16).Ptr();
            else
                return viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16, 1.0f, 1024, 1024, true).Ptr();
        }
        virtual void UpdateSceneResourceBinding(SceneResource* sceneRes) override
        {
            lighting.UpdateSceneResourceBinding(sceneRes);
        }

        virtual void UpdateSharedResourceBinding() override
        {
            for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
            {
                auto descSet = viewParams.GetDescriptorSet(i);
                descSet->BeginUpdate();
                descSet->Update(1, sharedRes->textureSampler.Ptr());
                descSet->EndUpdate();
            }
            lighting.UpdateSharedResourceBinding();
        }
        virtual void Init(Renderer * renderer, ViewResource * pViewRes) override
        {
            viewRes = pViewRes;
            sharedRes = renderer->GetSharedResource();
            shadowRenderPass = CreateShadowRenderPass();
            shadowRenderPass->Init(renderer);

            forwardRenderPass = CreateLightProbeRenderPass();
            forwardRenderPass->Init(renderer);

            customDepthRenderPass = CreateCustomDepthRenderPass();
            customDepthRenderPass->Init(renderer);

            forwardBaseOutput = viewRes->CreateRenderOutput(
                forwardRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16, 1.0f, 1024, 1024, true),
                viewRes->LoadSharedRenderTarget("depthBuffer", DepthBufferFormat)
            );
            transparentAtmosphereOutput = viewRes->CreateRenderOutput(
                forwardRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("litAtmosphereColor", StorageFormat::RGBA_F16),
                viewRes->LoadSharedRenderTarget("depthBuffer", DepthBufferFormat)
            );
            preZOutput = viewRes->CreateRenderOutput(
                customDepthRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("depthBuffer", DepthBufferFormat));
            preZTransparentOutput = viewRes->CreateRenderOutput(
                customDepthRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("depthBufferPreZTransparent", DepthBufferFormat));

            atmospherePass = CreateAtmospherePostRenderPass(viewRes);
            atmospherePass->SetSource(MakeArray(
                PostPassSource("litColor", StorageFormat::RGBA_F16),
                PostPassSource("depthBuffer", DepthBufferFormat),
                PostPassSource("litAtmosphereColor", StorageFormat::RGBA_F16)
            ).GetArrayView());
            atmospherePass->Init(renderer);

            // initialize forwardBasePassModule and lightingModule
            renderPassUniformMemory.Init(sharedRes->hardwareRenderer.Ptr(), BufferUsage::UniformBuffer, true, 22, sharedRes->hardwareRenderer->UniformBufferAlignment(), nullptr);
            sharedRes->CreateModuleInstance(viewParams, Engine::GetShaderCompiler()->LoadSystemTypeSymbol("ViewParams"), &renderPassUniformMemory);
            lighting.Init(*sharedRes, &renderPassUniformMemory, false);
            UpdateSharedResourceBinding();
            sharedModules.View = &viewParams;
            shadowViewInstances.Reserve(1024);

            lightListBuildingComputeKernel = renderer->GetComputeTaskManager()->LoadKernel("LightTiling.slang", "cs_BuildTiledLightList");
            lightListBuildingComputeTaskInstance = renderer->GetComputeTaskManager()->CreateComputeTaskInstance(lightListBuildingComputeKernel,
                sizeof(BuildTiledLightListUniforms), true);
        }
        enum class PassType
        {
            Shadow, CustomDepth, Main, Transparent
        };

        ArrayView<Drawable*> GetDrawable(DrawableSink * objSink, PassType pass, CullFrustum cf, bool append)
        {
            if (!append)
                drawableBuffer.Clear();
            for (auto obj : objSink->GetDrawables(pass == PassType::Transparent))
            {
                if (pass == PassType::Shadow && !obj->CastShadow)
                    continue;
                if (pass == PassType::CustomDepth && !obj->RenderCustomDepth)
                    continue;
                if (cf.IsBoxInFrustum(obj->Bounds))
                    drawableBuffer.Add(obj);
            }
            if (pass == PassType::CustomDepth)
            {
                for (auto obj : objSink->GetDrawables(true))
                {
                    if (!obj->RenderCustomDepth)
                        continue;
                    if (cf.IsBoxInFrustum(obj->Bounds))
                        drawableBuffer.Add(obj);
                }
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

            preZPassInstance = customDepthRenderPass->CreateInstance(preZOutput, true);
            preZPassTransparentInstance = customDepthRenderPass->CreateInstance(preZTransparentOutput, true);
            float aspect = w / (float)h;
            shadowRenderPass->ResetInstancePool();

            GetDrawablesParameter getDrawableParam;
            viewUniform.CameraPos = params.view.Position;
            viewUniform.ViewTransform = params.view.Transform;
            getDrawableParam.CameraDir = params.view.GetDirection();
            getDrawableParam.IsEditorMode = params.isEditorMode;
            getDrawableParam.IsBaking = true;
            Matrix4 mainProjMatrix;
            Matrix4::CreatePerspectiveMatrixFromViewAngle(mainProjMatrix,
                params.view.FOV, w / (float)h,
                params.view.ZNear, params.view.ZFar, ClipSpaceType::ZeroToOne);
            Matrix4 invProjMatrix;
            mainProjMatrix.Inverse(invProjMatrix);
            Matrix4::Multiply(viewUniform.ViewProjectionTransform, mainProjMatrix, viewUniform.ViewTransform);

            viewUniform.ViewTransform.Inverse(viewUniform.InvViewTransform);
            viewUniform.ViewProjectionTransform.Inverse(viewUniform.InvViewProjTransform);
            viewUniform.Time = Engine::Instance()->GetTime();

            getDrawableParam.CameraPos = viewUniform.CameraPos;
            getDrawableParam.rendererService = params.rendererService;
            getDrawableParam.sink = &sink;

            useAtmosphere = false;
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
                if (lighting.deviceLightmapSet)
                {
                    uint32_t lightmapIndex = lighting.deviceLightmapSet->GetDeviceLightmapId(actor.Value.Ptr());
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

                auto actorType = actor.Value->GetEngineType();
                if (actorType == EngineActorType::Atmosphere)
                {
                    useAtmosphere = true;
                    auto atmosphere = dynamic_cast<AtmosphereActor*>(actor.Value.Ptr());
                    auto newParams = atmosphere->GetParameters();
                    if (!(lastAtmosphereParams == newParams))
                    {
                        atmosphere->SunDir = atmosphere->SunDir.GetValue().Normalize();
                        newParams = atmosphere->GetParameters();
                        atmospherePass->SetParameters(&newParams, sizeof(newParams));
                        lastAtmosphereParams = newParams;
                    }
                }
            }
            // collect light data and render shadow map
            lighting.GatherInfo(hardwareRenderer, &sink, params, w, h, viewUniform, shadowRenderPass.Ptr());

            viewParams.SetUniformData(&viewUniform, (int)sizeof(viewUniform));
            auto cameraCullFrustum = CullFrustum(params.view.GetFrustum(aspect));

            // pre-z pass
            Array<Texture*, 8> textures;
            Array<Texture*, 2> prezTextures;
            preZOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            prezTextures.Add(textures[0]);
            preZTransparentOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            prezTextures.Add(textures[0]);
            customDepthRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            preZPassInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, PassType::Main, cameraCullFrustum, false));
            sharedRes->pipelineManager.PopModuleInstance();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            preZPassTransparentInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, PassType::Transparent, cameraCullFrustum, false));
            sharedRes->pipelineManager.PopModuleInstance();
            preZPassInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);
            preZPassTransparentInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);

            // build tiled light list
            BuildTiledLightListUniforms buildLightListUniforms;
            buildLightListUniforms.width = w;
            buildLightListUniforms.height = h;
            buildLightListUniforms.lightCount = lighting.lights.Count();
            buildLightListUniforms.lightProbeCount = lighting.lightProbes.Count();
            buildLightListUniforms.viewMatrix = viewUniform.ViewTransform;
            buildLightListUniforms.invProjMatrix = invProjMatrix;
            Array<ResourceBinding, 5> buildLightListBindings;
            buildLightListBindings.Add(ResourceBinding(prezTextures[0]));
            buildLightListBindings.Add(ResourceBinding(prezTextures[1]));
            buildLightListBindings.Add(ResourceBinding(lighting.lightBuffer.Ptr(), lighting.moduleInstance.GetCurrentVersion()*lighting.lightBufferSize, lighting.lightBufferSize));
            buildLightListBindings.Add(ResourceBinding(lighting.lightProbeBuffer.Ptr(), lighting.moduleInstance.GetCurrentVersion()*lighting.lightProbeBufferSize, lighting.lightProbeBufferSize));
            buildLightListBindings.Add(ResourceBinding(lighting.tiledLightListBufffer.Ptr(), 0, lighting.tiledLightListBufferSize));
            lightListBuildingComputeTaskInstance->UpdateVersionedParameters(&buildLightListUniforms, sizeof(buildLightListUniforms), buildLightListBindings.GetArrayView());
            lightListBuildingComputeTaskInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);

            // execute forward lighting pass
            forwardBaseOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            forwardRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            sharedRes->pipelineManager.PushModuleInstance(&lighting.moduleInstance);
            forwardBaseInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, PassType::Main, cameraCullFrustum, false));
            sharedRes->pipelineManager.PopModuleInstance();
            sharedRes->pipelineManager.PopModuleInstance();
            forwardBaseInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);

            if (useAtmosphere)
            {
                atmospherePass->CreateInstance(sharedModules)->Execute(hardwareRenderer, *params.renderStats);
            }
            // transparency pass
            reorderBuffer.Clear();
            for (auto drawable : GetDrawable(&sink, PassType::Transparent, cameraCullFrustum, false))
            {
                reorderBuffer.Add(drawable);
            }
            if (reorderBuffer.Count())
            {
                reorderBuffer.Sort([=](Drawable* d1, Drawable* d2) { return d1->Bounds.Distance(params.view.Position) > d2->Bounds.Distance(params.view.Position); });
                if (useAtmosphere)
                {
                    transparentPassInstance = forwardRenderPass->CreateInstance(transparentAtmosphereOutput, false);
                    transparentAtmosphereOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
                }
                else
                {
                    transparentPassInstance = forwardRenderPass->CreateInstance(forwardBaseOutput, false);
                    forwardBaseOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
                }

                forwardRenderPass->Bind();
                sharedRes->pipelineManager.PushModuleInstance(&viewParams);
                sharedRes->pipelineManager.PushModuleInstance(&lighting.moduleInstance);

                transparentPassInstance->SetFixedOrderDrawContent(sharedRes->pipelineManager, reorderBuffer.GetArrayView());
                sharedRes->pipelineManager.PopModuleInstance();
                sharedRes->pipelineManager.PopModuleInstance();
                transparentPassInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);
            }
            hardwareRenderer->EndJobSubmission(nullptr);
        }
    };

    IRenderProcedure * CreateLightProbeRenderProcedure()
    {
        return new LightProbeRenderProcedure();
    }
}