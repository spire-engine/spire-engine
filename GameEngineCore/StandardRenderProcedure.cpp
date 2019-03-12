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

using namespace VectorMath;

namespace GameEngine
{
	class LightUniforms
	{
	public:
		VectorMath::Vec3 lightDir; float pad0 = 0.0f;
		VectorMath::Vec3 lightColor; 
		float ambient = 0.2f;
		int shadowMapId = -1;
		int numCascades = 0;
		int padding2 = 0, pad3 = 0;
		VectorMath::Matrix4 lightMatrix[MaxShadowCascades];
		float zPlanes[MaxShadowCascades];
		LightUniforms()
		{
			lightDir.SetZero();
			lightColor.SetZero();
			for (int i = 0; i < MaxShadowCascades; i++)
				zPlanes[i] = 0.0f;
		}
	};

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

		StandardViewUniforms viewUniform;

		RefPtr<WorldPassRenderTask> forwardBaseInstance, transparentPassInstance, customDepthPassInstance, preZPassInstance, debugGraphicsPassInstance;
        RefPtr<ComputeKernel> lightListBuildingComputeKernel;
        RefPtr<ComputeTaskInstance> lightListBuildingComputeTaskInstance;

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
					return viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16).Ptr();
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
				viewRes->LoadSharedRenderTarget("litColor", StorageFormat::RGBA_F16),
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
			
			atmospherePass = CreateAtmospherePostRenderPass(viewRes);
			atmospherePass->SetSource(MakeArray(
				PostPassSource("litColor", StorageFormat::RGBA_F16),
				PostPassSource("depthBuffer", DepthBufferFormat),
				PostPassSource("litAtmosphereColor", StorageFormat::RGBA_F16)
			).GetArrayView());
			atmospherePass->Init(renderer);


			if (postProcess)
			{
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
			}
			// initialize forwardBasePassModule and lightingModule
			renderPassUniformMemory.Init(sharedRes->hardwareRenderer.Ptr(), BufferUsage::UniformBuffer, true, 22, sharedRes->hardwareRenderer->UniformBufferAlignment());
			sharedRes->CreateModuleInstance(viewParams, Engine::GetShaderCompiler()->LoadSystemTypeSymbol("ViewParams"), &renderPassUniformMemory);
			lighting.Init(*sharedRes, &renderPassUniformMemory, useEnvMap);
			UpdateSharedResourceBinding();
			sharedModules.View = &viewParams;
			shadowViewInstances.Reserve(1024);

            BuildTiledLightListUniforms buildLightListUniforms;
            lightListBuildingComputeKernel = Engine::GetComputeTaskManager()->LoadKernel("LightTiling.slang", "cs_BuildTiledLightList");
            lightListBuildingComputeTaskInstance = Engine::GetComputeTaskManager()->CreateComputeTaskInstance(lightListBuildingComputeKernel.Ptr(),
                ArrayView<ResourceBinding>(), &buildLightListUniforms, sizeof(buildLightListUniforms));
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

        enum class DataDependencyType
        {
            RenderTargetToGraphics, ComputeToGraphics, RenderTargetToCompute, UndefinedToRenderTarget, SampledToRenderTarget
        };
        List<ImagePipelineBarrier> imageBarriers;
        void QueueImageBarrier(HardwareRenderer* hw, ArrayView<Texture*> texturesToUse, DataDependencyType dep)
        {
            imageBarriers.Clear();
            for (auto img : texturesToUse)
            {
                ImagePipelineBarrier ib;
                ib.Image = img;
                if (dep == DataDependencyType::UndefinedToRenderTarget || dep == DataDependencyType::SampledToRenderTarget)
                    ib.LayoutAfter = img->IsDepthStencilFormat() ? TextureLayout::DepthStencilAttachment : TextureLayout::ColorAttachment;
                else
                    ib.LayoutAfter = TextureLayout::Sample;
                if (dep == DataDependencyType::UndefinedToRenderTarget)
                    ib.LayoutBefore = TextureLayout::Undefined;
                else
                {
                    if (dep == DataDependencyType::RenderTargetToGraphics || dep == DataDependencyType::RenderTargetToCompute)
                        ib.LayoutBefore = img->IsDepthStencilFormat() ? TextureLayout::DepthStencilAttachment : TextureLayout::ColorAttachment;
                    else
                        ib.LayoutBefore = TextureLayout::Sample;
                }
                imageBarriers.Add(ib);
            }
            switch (dep)
            {
            case DataDependencyType::RenderTargetToGraphics:
                hw->QueuePipelineBarrier(ResourceUsage::RenderAttachmentOutput, ResourceUsage::FragmentShaderAccess, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::ComputeToGraphics:
                hw->QueuePipelineBarrier(ResourceUsage::ComputeAccess, ResourceUsage::GraphicsShaderAccess, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::RenderTargetToCompute:
                hw->QueuePipelineBarrier(ResourceUsage::RenderAttachmentOutput, ResourceUsage::ComputeAccess, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::UndefinedToRenderTarget:
                hw->QueuePipelineBarrier(ResourceUsage::NonFragmentShaderGraphicsAccess, ResourceUsage::All, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::SampledToRenderTarget:
                hw->QueuePipelineBarrier(ResourceUsage::NonFragmentShaderGraphicsAccess, ResourceUsage::All, imageBarriers.GetArrayView());
                break;
            }
        }
		
		virtual void Run(const RenderProcedureParameters & params) override
		{
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
                    toneMappingParameters = toneMappingActor->Parameters;
				}
			}
            if (postProcess)
            {
                if (!(lastToneMappingParams == toneMappingParameters))
                {
                    toneMappingFromAtmospherePass->SetParameters(&toneMappingParameters, sizeof(toneMappingParameters));
                    toneMappingFromLitColorPass->SetParameters(&toneMappingParameters, sizeof(toneMappingParameters));
                    lastToneMappingParams = toneMappingParameters;
                }
            }
            // collect light data and render shadow maps

            QueueImageBarrier(hardwareRenderer, ArrayView<Texture*>(sharedRes->shadowMapResources.shadowMapArray.Ptr()), DataDependencyType::UndefinedToRenderTarget);

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
            customDepthPassInstance->Execute(hardwareRenderer, *params.renderStats);
            QueueImageBarrier(hardwareRenderer, textures.GetArrayView(), DataDependencyType::RenderTargetToGraphics);

            // pre-z pass
            preZOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            customDepthRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            preZPassInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, PassType::Main, cameraCullFrustum, false));
            sharedRes->pipelineManager.PopModuleInstance();
            preZPassInstance->Execute(hardwareRenderer, *params.renderStats);
            QueueImageBarrier(hardwareRenderer, textures.GetArrayView(), DataDependencyType::RenderTargetToGraphics);
            auto preZDepthTexture = textures[0];

            // build tiled light list
            BuildTiledLightListUniforms buildLightListUniforms;
            buildLightListUniforms.width = w;
            buildLightListUniforms.height = h;
            buildLightListUniforms.lightCount = lighting.lights.Count();
            buildLightListUniforms.lightProbeCount = lighting.lightProbes.Count();
            buildLightListUniforms.viewMatrix = viewUniform.ViewTransform;
            buildLightListUniforms.invProjMatrix = invProjMatrix;
            lightListBuildingComputeTaskInstance->SetUniformData(&buildLightListUniforms, sizeof(buildLightListUniforms));
            Array<ResourceBinding, 4> buildLightListBindings;
            buildLightListBindings.Add(ResourceBinding(preZDepthTexture));
            buildLightListBindings.Add(ResourceBinding(lighting.lightBuffer.Ptr(), 0, lighting.lightBufferSize));
            buildLightListBindings.Add(ResourceBinding(lighting.lightProbeBuffer.Ptr(), 0, lighting.lightProbeBufferSize));
            buildLightListBindings.Add(ResourceBinding(lighting.tiledLightListBufffer.Ptr(), 0, lighting.tiledLightListBufferSize));
            lightListBuildingComputeTaskInstance->SetBinding(buildLightListBindings.GetArrayView());
            lightListBuildingComputeTaskInstance->Queue((w + 15) / 16, (h + 15) / 16, 1);
            hardwareRenderer->QueuePipelineBarrier(ResourceUsage::ComputeAccess, ResourceUsage::FragmentShaderAccess);

            // execute forward lighting pass
            QueueImageBarrier(hardwareRenderer, ArrayView<Texture*>(sharedRes->shadowMapResources.shadowMapArray.Ptr()), DataDependencyType::RenderTargetToGraphics);
            
			forwardBaseOutput->GetFrameBuffer()->GetRenderAttachments().GetTextures(textures);
            QueueImageBarrier(hardwareRenderer, textures.GetArrayView(), DataDependencyType::SampledToRenderTarget);
			forwardRenderPass->Bind();
			sharedRes->pipelineManager.PushModuleInstance(&viewParams);
			sharedRes->pipelineManager.PushModuleInstance(&lighting.moduleInstance);
			forwardBaseInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer, GetDrawable(&sink, PassType::Main, cameraCullFrustum, false));
			sharedRes->pipelineManager.PopModuleInstance();
			sharedRes->pipelineManager.PopModuleInstance();
			forwardBaseInstance->Execute(hardwareRenderer, *params.renderStats);

            debugGraphicsRenderPass->Bind();
            sharedRes->pipelineManager.PushModuleInstance(&viewParams);
            debugGraphicsPassInstance->SetDrawContent(sharedRes->pipelineManager, reorderBuffer,
                Engine::GetDebugGraphics()->GetDrawables(Engine::Instance()->GetRenderer()->GetRendererService()));
            sharedRes->pipelineManager.PopModuleInstance();

            debugGraphicsPassInstance->Execute(hardwareRenderer, *params.renderStats);
			QueueImageBarrier(hardwareRenderer, textures.GetArrayView(), DataDependencyType::RenderTargetToGraphics);

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
				QueueImageBarrier(hardwareRenderer, textures.GetArrayView(), DataDependencyType::SampledToRenderTarget);
				transparentPassInstance->Execute(hardwareRenderer, *params.renderStats);
				QueueImageBarrier(hardwareRenderer, textures.GetArrayView(), DataDependencyType::RenderTargetToGraphics);
			}

			if (postProcess)
			{
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