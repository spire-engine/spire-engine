#include "Renderer.h"
#include "HardwareRenderer.h"
#include "Level.h"
#include "Engine.h"
#include "SkeletalMeshActor.h"
#include "CameraActor.h"
#include "EnvMapActor.h"
#include "CoreLib/Graphics/TextureFile.h"
#include "CoreLib/Imaging/Bitmap.h"
#include "CoreLib/Imaging/TextureData.h"
#include "CoreLib/WinForm/Debug.h"
#include "LightProbeRenderer.h"
#include "TextureCompressor.h"
#include "DeviceLightmapSet.h"
#include <fstream>
#include "DeviceMemory.h"
#include "WorldRenderPass.h"
#include "PostRenderPass.h"
#include "RenderProcedure.h"
#include "ComputeTaskManager.h"

using namespace CoreLib;
using namespace VectorMath;

namespace GameEngine
{
	int Align(int ptr, int alignment)
	{
		int m = ptr % alignment;
		if (m)
		{
			int padding = alignment - m;
			return ptr + padding;
		}
		return ptr;
	}

	class RendererImpl : public Renderer
	{
		class RendererServiceImpl : public RendererService
		{
		private:
			RendererImpl * renderer;
			RefPtr<Drawable> CreateDrawableShared(Mesh * mesh, Material * material, bool cacheMesh)
			{
				auto sceneResources = renderer->sceneRes.Ptr();
				RefPtr<Drawable> rs = new Drawable(sceneResources);
                if (cacheMesh)
                    rs->mesh = sceneResources->LoadDrawableMesh(mesh);
                else
                    rs->mesh = sceneResources->CreateDrawableMesh(mesh);
				rs->material = material;
				return rs;
			}

		public:
			RendererServiceImpl(RendererImpl * pRenderer)
				: renderer(pRenderer)
			{
            }
			void CreateTransformModuleInstance(ModuleInstance & rs, const char * name, int uniformBufferSize)
			{
				auto sceneResources = renderer->sceneRes.Ptr();
				renderer->sharedRes.CreateModuleInstance(rs, Engine::GetShaderCompiler()->LoadSystemTypeSymbol(name), &sceneResources->transformMemory, uniformBufferSize);
                uint32_t lightmapId = 0xFFFFFFFF;
                for (int i = 0; i < DynamicBufferLengthMultiplier; i++)
                    rs.SetUniformData(&lightmapId, sizeof(lightmapId));
			}

			virtual CoreLib::RefPtr<Drawable> CreateStaticDrawable(Mesh * mesh, int elementId, Material * material, bool cacheMesh) override
			{
                if (!material)
                    material = Engine::Instance()->GetLevel()->LoadErrorMaterial();
				if (!material->MaterialModule)
					renderer->sceneRes->RegisterMaterial(material);
				RefPtr<Drawable> rs = CreateDrawableShared(mesh, material, cacheMesh);
				rs->type = DrawableType::Static;
                rs->primType = mesh->GetPrimitiveType();
				rs->elementRange = mesh->ElementRanges[elementId];
				CreateTransformModuleInstance(*rs->transformModule, "StaticMeshTransform", (int)(sizeof(Vec4) * 4));
				return rs;
			}
			virtual CoreLib::RefPtr<Drawable> CreateSkeletalDrawable(Mesh * mesh, int elementId, Skeleton * skeleton, Material * material, bool cacheMesh) override
			{
				if (!material->MaterialModule)
					renderer->sceneRes->RegisterMaterial(material);
				RefPtr<Drawable> rs = CreateDrawableShared(mesh, material, cacheMesh);
				rs->type = DrawableType::Skeletal;
                rs->primType = mesh->GetPrimitiveType();
				rs->elementRange = mesh->ElementRanges[elementId];
				rs->skeleton = skeleton;
				int poseMatrixSize = skeleton->Bones.Count() * (sizeof(Vec4) * 4);
				CreateTransformModuleInstance(*rs->transformModule, "SkeletalAnimationTransform", poseMatrixSize);
				return rs;
			}
		};
	private:
		RendererSharedResource sharedRes;
		RefPtr<SceneResource> sceneRes;
		RefPtr<ViewResource> mainView;
		RefPtr<RendererServiceImpl> renderService;
		RefPtr<IRenderProcedure> renderProcedure;
		EnumerableDictionary<uint32_t, int> worldRenderPassIds;
		List<RefPtr<WorldRenderPass>> worldRenderPasses;
		List<RefPtr<PostRenderPass>> postRenderPasses;
		HardwareRenderer * hardwareRenderer = nullptr;
		Level* level = nullptr;
		int uniformBufferAlignment = 256;
		int storageBufferAlignment = 32;
		int defaultEnvMapId = -1;
	private:
		void RunRenderProcedure()
		{
			if (!level) return;
			RenderProcedureParameters params;
			params.renderStats = &sharedRes.renderStats;
			params.level = level;
			params.renderer = this;
			params.isEditorMode = Engine::Instance()->GetEngineMode() == EngineMode::Editor;
			auto curCam = level->CurrentCamera.Ptr();
			if (curCam)
				params.view = curCam->GetView();
			else
				params.view = View();
			params.rendererService = renderService.Ptr();
			renderProcedure->Run(params);
		}
	public:
        CoreLib::RefPtr<ComputeTaskManager> computeTaskManager;

		RendererImpl(RenderAPI api)
			: sharedRes(api)
		{
			switch (api)
			{
			case RenderAPI::Vulkan:
				hardwareRenderer = CreateVulkanHardwareRenderer(Engine::Instance()->GpuId, 
                    Path::Combine(Engine::Instance()->GetDirectory(false, ResourceType::ShaderCache), "pipeline_cache.tmp"));
				break;
			}
			hardwareRenderer->SetMaxTempBufferVersions(DynamicBufferLengthMultiplier);
            
            computeTaskManager = new ComputeTaskManager(hardwareRenderer, Engine::GetShaderCompiler());

			sharedRes.Init(hardwareRenderer);

			mainView = new ViewResource(hardwareRenderer);
			mainView->Resize(1024, 1024);
			
			renderProcedure = CreateStandardRenderProcedure(true, true);
			renderProcedure->Init(this, mainView.Ptr());

			// Fetch uniform buffer alignment requirements
			uniformBufferAlignment = hardwareRenderer->UniformBufferAlignment();
			storageBufferAlignment = hardwareRenderer->StorageBufferAlignment();
			
			sceneRes = new SceneResource(&sharedRes);
			renderService = new RendererServiceImpl(this);
			hardwareRenderer->Wait();
		}
		~RendererImpl()
		{
			Wait();
			for (auto & postPass : postRenderPasses)
				postPass = nullptr;

			renderProcedure = nullptr;
            cubemapRenderProc = nullptr;
			mainView = nullptr;
			sceneRes = nullptr;
			sharedRes.Destroy();
            computeTaskManager = nullptr;
		}

		virtual void Wait() override
		{
			hardwareRenderer->Wait();
		}

        virtual ComputeTaskManager* GetComputeTaskManager() override
        {
            return computeTaskManager.Ptr();
        }

		virtual int RegisterWorldRenderPass(uint32_t shaderId) override
		{
			int passId;
			if (worldRenderPassIds.TryGetValue(shaderId, passId))
				return passId;
			int newId = worldRenderPassIds.Count();
			worldRenderPassIds[shaderId] = newId;
			return newId;
		}

		virtual HardwareRenderer * GetHardwareRenderer() override
		{
			return hardwareRenderer;
		}
        virtual RendererService* GetRendererService() override
        {
            return renderService.Ptr();
        }
        virtual void UpdateLightmap(LightmapSet& lightmapSet) override
        {
            if (level)
            {
                Wait();
                sceneRes->deviceLightmapSet = new DeviceLightmapSet();
                sceneRes->deviceLightmapSet->Init(hardwareRenderer, lightmapSet);
                renderProcedure->UpdateSceneResourceBinding(sceneRes.Ptr());
                cubemapRenderProc->UpdateSceneResourceBinding(sceneRes.Ptr());
            }
        }
		RefPtr<ViewResource> cubemapRenderView;
		RefPtr<IRenderProcedure> cubemapRenderProc;
		virtual void UpdateLightProbes() override
		{
			if (!level) return;
			LightProbeRenderer lpRenderer(this, renderService.Ptr(), cubemapRenderProc.Ptr(), cubemapRenderView.Ptr());
			int lightProbeCount = 0;
			for (auto & actor : level->Actors)
			{
				if (actor.Value->GetEngineType() == EngineActorType::EnvMap)
				{
					auto envMapActor = dynamic_cast<EnvMapActor*>(actor.Value.Ptr());
					if (envMapActor->GetEnvMapId() != -1)
						lpRenderer.RenderLightProbe(sharedRes.envMapArray.Ptr(), envMapActor->GetEnvMapId(), level, envMapActor->GetPosition());
					lightProbeCount++;
				}
			}
			if (lightProbeCount == 0)
			{
				if (defaultEnvMapId == -1)
					defaultEnvMapId = sharedRes.AllocEnvMap();
				lpRenderer.RenderLightProbe(sharedRes.envMapArray.Ptr(), defaultEnvMapId, level, Vec3::Create(0.0f, 1000.0f, 0.0f));
			}
		}
        void TryLoadLightmap()
        {
            auto lightmapFile = Engine::Instance()->FindFile(Path::ReplaceExt(level->FileName, "lightmap"), ResourceType::Level);
            sceneRes->deviceLightmapSet = nullptr;
            if (lightmapFile.Length())
            {
                LightmapSet lightmapSet;
                lightmapSet.LoadFromFile(level, lightmapFile);
                if (lightmapSet.ActorLightmapIds.Count() != lightmapSet.Lightmaps.Count())
                {
                    return;
                }
                for (auto & lm : lightmapSet.Lightmaps)
                {
                    if (lm.Width != lm.Height || (1 << Math::Log2Ceil(lm.Width)) != lm.Width)
                        return;
                }
                sceneRes->deviceLightmapSet = new DeviceLightmapSet();
                sceneRes->deviceLightmapSet->Init(hardwareRenderer, lightmapSet);
            }
        }
		virtual void InitializeLevel(Level* pLevel) override
		{
			if (!pLevel) return;
			level = pLevel;
            TryLoadLightmap();

			cubemapRenderView = new ViewResource(hardwareRenderer);
			cubemapRenderView->Resize(EnvMapSize, EnvMapSize);
			cubemapRenderProc = CreateStandardRenderProcedure(false, false);
			cubemapRenderProc->Init(this, cubemapRenderView.Ptr());
            cubemapRenderProc->UpdateSceneResourceBinding(sceneRes.Ptr());
			defaultEnvMapId = -1;
			UpdateLightProbes();
			renderProcedure->UpdateSharedResourceBinding();
            renderProcedure->UpdateSceneResourceBinding(sceneRes.Ptr());
			RunRenderProcedure();
			hardwareRenderer->TransferBarrier(DynamicBufferLengthMultiplier);
			RenderFrame();
			Wait();
			sharedRes.renderStats.Clear();
		}
		virtual void TakeSnapshot() override
		{
			
		}
		virtual RenderStat & GetStats() override
		{
			return sharedRes.renderStats;
		}

		struct DescriptorSetUpdate
		{
			DescriptorSet * descSet;
			int index;
			Buffer * buffer;
			int offset;
			int length;
		};

		virtual void RenderFrame() override
		{
			if (!level) return;
            static int frameId = 0;
            frameId++;
            sharedRes.renderStats.Divisor++;
			sharedRes.renderStats.NumMaterials = 0;
			sharedRes.renderStats.NumShaders = 0;
            RunRenderProcedure();
		}
		virtual RendererSharedResource * GetSharedResource() override
		{
			return &sharedRes;
		}
		virtual SceneResource * GetSceneResource() override
		{
			return sceneRes.Ptr();
		}
		virtual void Resize(int w, int h) override
		{
            Wait();
			mainView->Resize(w, h);
			Wait();
		}
		Texture2D * GetRenderedImage()
		{
			if (renderProcedure)
				return renderProcedure->GetOutput()->Texture.Ptr();
			return nullptr;
		}
		virtual void DestroyContext() override
		{
			sharedRes.ResetEnvMapAllocation();
			sceneRes->Clear();
		}
	};

	Renderer* CreateRenderer(RenderAPI api)
	{
		return new RendererImpl(api);
	}
}