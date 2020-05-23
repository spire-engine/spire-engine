#include "Engine.h"
#include "WorldRenderPass.h"
#include "PostRenderPass.h"
#include "Renderer.h"
#include "RenderPassRegistry.h"
#include "DirectionalLightActor.h"
#include "AtmosphereActor.h"
#include "ToneMappingActor.h"
#include "FrustumCulling.h"
#include "RenderProcedure.h"
#include "StandardViewUniforms.h"
#include "LightingData.h"
#include "BuildHistogram.h"
#include "EyeAdaptation.h"
#include "SSAOActor.h"

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

    class StandardRenderProcedure : public IRenderProcedure
    {
    private:
        const int histogramSize = 128;

        RendererSharedResource * sharedRes = nullptr;
        ViewResource * viewRes = nullptr;

        RefPtr<WorldRenderPass> shadowRenderPass;
        RefPtr<WorldRenderPass> forwardRenderPass;
        RefPtr<WorldRenderPass> customDepthRenderPass;
        RefPtr<WorldRenderPass> debugGraphicsRenderPass;

        RefPtr<PostRenderPass> atmospherePass;
        RefPtr<PostRenderPass> toneMappingFromAtmospherePass;
        RefPtr<PostRenderPass> toneMappingFromLitColorPass;
        RefPtr<PostRenderPass> editorOutlinePass;

        RenderOutput * forwardBaseOutput = nullptr;
        RenderOutput * transparentAtmosphereOutput = nullptr;
        RenderOutput * customDepthOutput = nullptr;
        RenderOutput * preZOutput = nullptr;
        RenderOutput * preZTransparentOutput = nullptr;

        StandardViewUniforms viewUniform;

        RefPtr<WorldPassRenderTask> forwardBaseInstance, transparentPassInstance, customDepthPassInstance, 
            preZPassInstance, preZPassTransparentInstance, debugGraphicsPassInstance;

        ComputeKernel* lightListBuildingComputeKernel;
        ComputeKernel* clearHistogramComputeKernel;
        ComputeKernel* histogramBuildingComputeKernel;
        ComputeKernel* eyeAdaptationComputeKernel;
        ComputeKernel* ssaoComputeKernel;
        ComputeKernel* ssaoBlurXComputeKernel;
        ComputeKernel* ssaoBlurYComputeKernel;

        RefPtr<ComputeTaskInstance> lightListBuildingComputeTaskInstance, 
            clearHistogramComputeTaskInstance, histogramBuildingComputeTaskInstance, eyeAdaptationComputeTaskInstance;
        RefPtr<ComputeTaskInstance> ssaoComputeTaskInstance, ssaoBlurXInstance, ssaoBlurYInstance, ssaoCompositeInstance;
        RefPtr<RenderTarget> aoRenderTarget, aoBlurTarget;
        RefPtr<Buffer> randomDirectionBuffer;

        DeviceMemory renderPassUniformMemory;
        SharedModuleInstances sharedModules;
        ModuleInstance viewParams;
        CoreLib::List<ModuleInstance> shadowViewInstances;

        DrawableSink sink;

        List<Drawable*> reorderBuffer, drawableBuffer;
        LightingEnvironment lighting;
        AtmosphereParameters lastAtmosphereParams;
        ToneMappingParameters lastToneMappingParams;
        bool useAtmosphere = false;
        bool postProcess = false;
        bool useEnvMap = false;
    public:
        StandardRenderProcedure(bool pPostProcess, bool pUseEnvMap)
        {
            postProcess = pPostProcess;
            useEnvMap = pUseEnvMap;
        }
        ~StandardRenderProcedure()
        {
            if (customDepthOutput)
                viewRes->DestroyRenderOutput(customDepthOutput);
            if (forwardBaseOutput)
                viewRes->DestroyRenderOutput(forwardBaseOutput);
            if (transparentAtmosphereOutput)
                viewRes->DestroyRenderOutput(transparentAtmosphereOutput);
            histogramBuildingComputeTaskInstance = nullptr;
            lightListBuildingComputeTaskInstance = nullptr;
            eyeAdaptationComputeTaskInstance = nullptr;
        }
        virtual CoreLib::String GetName() override
        {
            return "Standard Output";
        }
        virtual RenderTarget* GetOutput() override
        {
            if (postProcess)
            {
                if (Engine::Instance()->GetEngineMode() == EngineMode::Editor)
                    return viewRes->LoadSharedRenderTarget("editorColor", StorageFormat::RGBA_8).Ptr();
                else
                    return viewRes->LoadSharedRenderTarget("toneColor", StorageFormat::RGBA_8).Ptr();
            }
            else
            {
                if (useAtmosphere)
                    return viewRes->LoadSharedRenderTarget("litAtmosphereColor", StorageFormat::RGBA_F16).Ptr();
                else
                    return viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16, 1.0f, 1024, 1024, true).Ptr();
            }
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
        RefPtr<Buffer> CreateRandomDirectionsBuffer()
        {
            size_t bufferSize = sizeof(VectorMath::Vec4) * 16 * 16 * 16;
            RefPtr<Buffer> rs = sharedRes->hardwareRenderer->CreateBuffer(BufferUsage::StorageBuffer, (int)bufferSize);
            List<VectorMath::Vec4> dirs;
            dirs.SetSize(16 * 16 * 16);
            Random random(7291);
            for (int i = 0; i < dirs.Count(); i++)
            {
                VectorMath::Vec4 dir;
                dir.y = random.NextFloat() * 0.8f + 0.2f;
                float theta = random.NextFloat() * (2.0f * Math::Pi);
                float s = sqrt(1.0f - dir.y * dir.y);
                dir.x = s * cos(theta);
                dir.z = s * sin(theta);
                dir.w = 1.0f;
                dirs[i] = dir;
            }
            rs->SetDataAsync(0, dirs.Buffer(), (int)bufferSize);
            return rs;
        }
        virtual void Init(Renderer * renderer, ViewResource * pViewRes) override
        {
            viewRes = pViewRes;
            sharedRes = renderer->GetSharedResource();
            shadowRenderPass = CreateShadowRenderPass();
            shadowRenderPass->Init(renderer);

            forwardRenderPass = CreateForwardBaseRenderPass();
            forwardRenderPass->Init(renderer);

            debugGraphicsRenderPass = CreateDebugGraphicsRenderPass();
            debugGraphicsRenderPass->Init(renderer);

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
            customDepthOutput = viewRes->CreateRenderOutput(
                customDepthRenderPass->GetRenderTargetLayout(),
                viewRes->LoadSharedRenderTarget("customDepthBuffer", DepthBufferFormat));
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


            if (postProcess)
            {
                aoRenderTarget = viewRes->LoadSharedRenderTarget("ao", StorageFormat::RG_8, 1.0f, 1024, 1024, true);
                aoBlurTarget = viewRes->LoadSharedRenderTarget("aoBlur", StorageFormat::RG_8, 1.0f, 1024, 1024, true);

                toneMappingFromAtmospherePass = CreateToneMappingPostRenderPass(viewRes);
                toneMappingFromAtmospherePass->SetSource(MakeArray(
                    PostPassSource("litAtmosphereColor", StorageFormat::RGBA_F16),
                    PostPassSource("toneColor", StorageFormat::RGBA_8)
                ).GetArrayView());
                toneMappingFromAtmospherePass->Init(renderer);
                toneMappingFromLitColorPass = CreateToneMappingPostRenderPass(viewRes);
                toneMappingFromLitColorPass->SetSource(MakeArray(
                    PostPassSource("litColor", StorageFormat::RGBA_F16),
                    PostPassSource("toneColor", StorageFormat::RGBA_8)
                ).GetArrayView());
                toneMappingFromLitColorPass->Init(renderer);
                if (Engine::Instance()->GetEngineMode() == EngineMode::Editor)
                {
                    editorOutlinePass = CreateOutlinePostRenderPass(viewRes);
                    editorOutlinePass->SetSource(MakeArray(
                        PostPassSource("toneColor", StorageFormat::RGBA_8),
                        PostPassSource("customDepthBuffer", DepthBufferFormat),
                        PostPassSource("editorColor", StorageFormat::RGBA_8)
                    ).GetArrayView());
                    editorOutlinePass->Init(renderer);
                }
                auto computeTaskManager = renderer->GetComputeTaskManager();
                clearHistogramComputeKernel = computeTaskManager->LoadKernel("ClearHistogram.slang", "cs_ClearHistogram");
                clearHistogramComputeTaskInstance = computeTaskManager->CreateComputeTaskInstance(clearHistogramComputeKernel, sizeof(int), false);
                clearHistogramComputeTaskInstance->SetUniformData((void*)&histogramSize, sizeof(int));
                Array<ResourceBinding, 1> clearHistogramBindings;
                clearHistogramBindings.Add(ResourceBinding(sharedRes->histogramBuffer.Ptr(), 0, -1));
                clearHistogramComputeTaskInstance->SetBinding(clearHistogramBindings.GetArrayView());
                histogramBuildingComputeKernel = computeTaskManager->LoadKernel("BuildHistogram.slang", "cs_BuildHistogram");
                histogramBuildingComputeTaskInstance = computeTaskManager->CreateComputeTaskInstance(histogramBuildingComputeKernel, sizeof(BuildHistogramUniforms), true);
                eyeAdaptationComputeKernel = computeTaskManager->LoadKernel("EyeAdaptation.slang", "cs_EyeAdaptation");
                eyeAdaptationComputeTaskInstance = computeTaskManager->CreateComputeTaskInstance(eyeAdaptationComputeKernel, sizeof(EyeAdaptationUniforms), true);
            
                ssaoComputeKernel = computeTaskManager->LoadKernel("SSAO.slang", "cs_SSAO");
                ssaoComputeTaskInstance = computeTaskManager->CreateComputeTaskInstance(ssaoComputeKernel, sizeof(SSAOUniforms), true);
                ssaoBlurXComputeKernel = computeTaskManager->LoadKernel("SSAO.slang", "cs_SSAOBlurX");
                ssaoBlurXInstance = computeTaskManager->CreateComputeTaskInstance(ssaoBlurXComputeKernel, sizeof(SSAOUniforms), true);
                ssaoBlurYComputeKernel = computeTaskManager->LoadKernel("SSAO.slang", "cs_SSAOBlurY");
                ssaoBlurYInstance = computeTaskManager->CreateComputeTaskInstance(ssaoBlurYComputeKernel, sizeof(SSAOUniforms), true);
                auto ssaoCompositeKernel = computeTaskManager->LoadKernel("SSAOComposite.slang", "cs_SSAOComposite");
                ssaoCompositeInstance = computeTaskManager->CreateComputeTaskInstance(ssaoCompositeKernel, 0, true);

                randomDirectionBuffer = CreateRandomDirectionsBuffer();
            }
            // initialize forwardBasePassModule and lightingModule
            renderPassUniformMemory.Init(sharedRes->hardwareRenderer.Ptr(), BufferUsage::UniformBuffer, true, 22, sharedRes->hardwareRenderer->UniformBufferAlignment());
            sharedRes->CreateModuleInstance(viewParams, Engine::GetShaderCompiler()->LoadSystemTypeSymbol("ViewParams"), &renderPassUniformMemory);
            lighting.Init(*sharedRes, &renderPassUniformMemory, useEnvMap);
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
            bool ssaoEnabled = false;
            SSAOUniforms ssaoUniforms = {};

            int w = 0, h = 0;
            auto hardwareRenderer = params.renderer->GetHardwareRenderer();
            hardwareRenderer->BeginJobSubmission();

            forwardRenderPass->ResetInstancePool();
            forwardBaseOutput->GetSize(w, h);
            forwardBaseInstance = forwardRenderPass->CreateInstance(forwardBaseOutput, true);

            debugGraphicsRenderPass->ResetInstancePool();
            debugGraphicsPassInstance = debugGraphicsRenderPass->CreateInstance(forwardBaseOutput, false);

            customDepthRenderPass->ResetInstancePool();
            customDepthPassInstance = customDepthRenderPass->CreateInstance(customDepthOutput, true);
            preZPassInstance = customDepthRenderPass->CreateInstance(preZOutput, true);
            preZPassTransparentInstance = customDepthRenderPass->CreateInstance(preZTransparentOutput, true);
            float aspect = w / (float)h;
            shadowRenderPass->ResetInstancePool();

            GetDrawablesParameter getDrawableParam;
            viewUniform.CameraPos = params.view.Position;
            viewUniform.ViewTransform = params.view.Transform;
            getDrawableParam.CameraDir = params.view.GetDirection();
            getDrawableParam.IsEditorMode = params.isEditorMode;
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
            ToneMappingParameters toneMappingParameters;
            EyeAdaptationUniforms eyeAdaptationUniforms;
            
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
                else if (postProcess && actorType == EngineActorType::ToneMapping)
                {
                    auto toneMappingActor = dynamic_cast<ToneMappingActor*>(actor.Value.Ptr());
                    toneMappingParameters = toneMappingActor->GetToneMappingParameters();
                    eyeAdaptationUniforms = toneMappingActor->GetEyeAdaptationParameters();
                }
                else if (postProcess && actorType == EngineActorType::SSAO)
                {
                    ssaoUniforms = dynamic_cast<SSAOActor*>(actor.Value.Ptr())->GetParameters();
                    ssaoEnabled = true;
                }
            }
            if (postProcess)
            {
                eyeAdaptationUniforms.height = h;
                eyeAdaptationUniforms.width = w;
                eyeAdaptationUniforms.histogramSize = histogramSize;
                eyeAdaptationUniforms.frameId = Engine::Instance()->GetFrameId();
                eyeAdaptationUniforms.deltaTime = Engine::Instance()->GetTimeDelta(EngineThread::Rendering);
                if (!(lastToneMappingParams == toneMappingParameters))
                {
                    toneMappingFromAtmospherePass->SetParameters(&toneMappingParameters, sizeof(toneMappingParameters));
                    toneMappingFromLitColorPass->SetParameters(&toneMappingParameters, sizeof(toneMappingParameters));
                    lastToneMappingParams = toneMappingParameters;
                }
            }
            // collect light data and render shadow maps
            lighting.GatherInfo(hardwareRenderer, &sink, params, w, h, viewUniform, shadowRenderPass.Ptr());

            viewParams.SetUniformData(&viewUniform, (int)sizeof(viewUniform));
            auto cameraCullFrustum = CullFrustum(params.view.GetFrustum(aspect));

            // custom depth pass
            Array<Texture*, 8> textures;
            customDepthOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            customDepthRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            customDepthPassInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, PassType::CustomDepth, cameraCullFrustum, false));
            sharedRes->pipelineManager.PopModuleInstance();
            customDepthPassInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::MemoryAndImage);

            // pre-z pass
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

            if (ssaoEnabled)
            {
                // ssao
                Array<ResourceBinding, 3> ssaoBindings;
                ssaoBindings.Add(ResourceBinding(prezTextures[0]));
                ssaoBindings.Add(ResourceBinding(aoRenderTarget->Texture.Ptr(), ResourceBinding::BindingType::StorageImage));
                ssaoBindings.Add(ResourceBinding(randomDirectionBuffer.Ptr(), 0, -1));

                ssaoUniforms.width = w;
                ssaoUniforms.height = h;
                ssaoUniforms.ProjMatrix = mainProjMatrix;
                ssaoUniforms.InvProjMatrix = invProjMatrix;

                ssaoComputeTaskInstance->UpdateVersionedParameters(&ssaoUniforms, sizeof(ssaoUniforms), ssaoBindings.GetArrayView());
                ssaoComputeTaskInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);

                ssaoBindings[0] = ResourceBinding(aoRenderTarget->Texture.Ptr(), ResourceBinding::BindingType::Texture);
                ssaoBindings[1] = ResourceBinding(aoBlurTarget->Texture.Ptr(), ResourceBinding::BindingType::StorageImage);
                ssaoBlurXInstance->UpdateVersionedParameters(&ssaoUniforms, sizeof(ssaoUniforms), ssaoBindings.GetArrayView());
                ssaoBlurXInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);

                ssaoBindings[0] = ResourceBinding(aoBlurTarget->Texture.Ptr(), ResourceBinding::BindingType::Texture);
                ssaoBindings[1] = ResourceBinding(aoRenderTarget->Texture.Ptr(), ResourceBinding::BindingType::StorageImage);
                ssaoBlurYInstance->UpdateVersionedParameters(&ssaoUniforms, sizeof(ssaoUniforms), ssaoBindings.GetArrayView());
                ssaoBlurYInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);
            }

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

            debugGraphicsRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            debugGraphicsPassInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer,
                Engine::GetDebugGraphics()->GetDrawables(Engine::Instance()->GetRenderer()->GetRendererService()));
            sharedRes->pipelineManager.PopModuleInstance();
            debugGraphicsPassInstance->Execute(hardwareRenderer, *params.renderStats, PipelineBarriers::None);

            if (ssaoEnabled)
            {
                // composite AO with lit buffer
                Array<ResourceBinding, 2> aoCompositeBindings;
                aoCompositeBindings.Add(ResourceBinding(aoRenderTarget->Texture.Ptr(), ResourceBinding::BindingType::Texture));
                aoCompositeBindings.Add(ResourceBinding(textures[0],ResourceBinding::BindingType::StorageImage));
                ssaoCompositeInstance->UpdateVersionedParameters(nullptr, 0, aoCompositeBindings.GetArrayView());
                ssaoCompositeInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);
            }

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

            if (postProcess)
            {
                if (useAtmosphere)
                    transparentAtmosphereOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
                else
                    forwardBaseOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
                auto lightingOutput = textures[0];
                // build histogram for eye adaptation
                clearHistogramComputeTaskInstance->Queue(1, 1, 1);
                BuildHistogramUniforms buildHistogramUniforms;
                buildHistogramUniforms.width = w;
                buildHistogramUniforms.height = h;
                buildHistogramUniforms.histogramSize = histogramSize;
                Array<ResourceBinding, 5> buildHistogramBindings;
                buildHistogramBindings.Add(ResourceBinding(lightingOutput));
                buildHistogramBindings.Add(ResourceBinding(sharedRes->histogramBuffer.Ptr(), 0, sizeof(int32_t)*128));
                histogramBuildingComputeTaskInstance->UpdateVersionedParameters(&buildHistogramUniforms, sizeof(buildHistogramUniforms), buildHistogramBindings.GetArrayView());
                histogramBuildingComputeTaskInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);

                // compute adapted luminance
                Array<ResourceBinding, 5> eyeAdaptationBindings;
                eyeAdaptationBindings.Add(ResourceBinding(sharedRes->histogramBuffer.Ptr(), 0, sizeof(int32_t) * 128));
                eyeAdaptationBindings.Add(ResourceBinding(sharedRes->adaptedLuminanceBuffer.Ptr(), 0, sizeof(float)));
                eyeAdaptationComputeTaskInstance->UpdateVersionedParameters(&eyeAdaptationUniforms, sizeof(eyeAdaptationUniforms), eyeAdaptationBindings.GetArrayView());
                eyeAdaptationComputeTaskInstance->Queue(1, 1, 1);

                // tone mapping with adapted luminance
                if (useAtmosphere)
                {
                    toneMappingFromAtmospherePass->CreateInstance(sharedModules)->Execute(hardwareRenderer, *params.renderStats);
                }
                else
                {
                    toneMappingFromLitColorPass->CreateInstance(sharedModules)->Execute(hardwareRenderer, *params.renderStats);
                }
                if (Engine::Instance()->GetEngineMode() == EngineMode::Editor)
                    editorOutlinePass->CreateInstance(sharedModules)->Execute(hardwareRenderer, *params.renderStats);
            }
            hardwareRenderer->EndJobSubmission(nullptr);
        }
    };

    IRenderProcedure * CreateStandardRenderProcedure(bool toneMapping, bool useEnvMap)
    {
        return new StandardRenderProcedure(toneMapping, useEnvMap);
    }
}