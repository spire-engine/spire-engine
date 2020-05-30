#include "LightProbeRenderer.h"
#include "Engine.h"

using namespace VectorMath;

namespace GameEngine
{
	using namespace CoreLib;

	class Renderer;

	LightProbeRenderer::LightProbeRenderer(Renderer * prenderer, RendererService * prenderService, IRenderProcedure * pRenderProc, ViewResource * pViewRes)
	{
		renderer = prenderer;
		renderService = prenderService;
		renderProc = pRenderProc;
		viewRes = pViewRes;
		tempEnv = prenderer->GetHardwareRenderer()->CreateTextureCube("LightProbeRenderer::tempEnv", TextureUsage::SampledColorAttachment, EnvMapSize, Math::Log2Floor(EnvMapSize) + 1, StorageFormat::RGBA_F16);
        {
            ShaderCompilationResult crs;
            copyShaderSet = CompileGraphicsShader(crs, prenderer->GetHardwareRenderer(), "CopyPixel.slang");
        }
        {
            ShaderCompilationResult crs;
            prefilterShaderSet = CompileGraphicsShader(crs, prenderer->GetHardwareRenderer(), "LightProbePrefilter.slang");
        }
        fence = prenderer->GetHardwareRenderer()->CreateFence();
        fence->Reset();
	}

	struct PrefilterUniform
	{
		Vec4 origin;
		Vec4 s, t, r;
		float roughness;
	};

	void LightProbeRenderer::RenderLightProbe(TextureCubeArray* dest, int id, Level * level, VectorMath::Vec3 position)
	{
		HardwareRenderer * hw = renderer->GetHardwareRenderer();
		int resolution = EnvMapSize;
		int numLevels = Math::Log2Floor(resolution) + 1;

		RenderStat stat;
		viewRes->Resize(resolution, resolution);
		RenderProcedureParameters params;
		params.level = level;
		params.renderer = renderer;
		params.rendererService = renderService;
		params.renderStats = &stat;
		params.view.FOV = 90.0f;
		params.view.ZFar = 40000.0f;
		params.view.ZNear = 20.0f;
		params.view.Position = position;
		params.isEditorMode = Engine::Instance()->GetEngineMode() == EngineMode::Editor;
		auto sharedRes = renderer->GetSharedResource();
		RefPtr<PipelineBuilder> pb = hw->CreatePipelineBuilder();
		VertexFormat quadVert;
        pb->FixedFunctionStates.cullMode = CullMode::Disabled;
		pb->FixedFunctionStates.PrimitiveTopology = PrimitiveType::TriangleStrips;
		quadVert.Attributes.Add(VertexAttributeDesc(DataType::Float2, 0, 0, 0, "POSITION", 0));
		quadVert.Attributes.Add(VertexAttributeDesc(DataType::Float2, 0, 8, 1, "TEXCOORD", 0));
		pb->SetVertexLayout(quadVert);
		RefPtr<DescriptorSetLayout> copyPassLayout = hw->CreateDescriptorSetLayout(MakeArray(
			DescriptorLayout(StageFlags::sfGraphics, 0, BindingType::Texture),
			DescriptorLayout(StageFlags::sfGraphics, 1, BindingType::Sampler)).GetArrayView());
		pb->SetBindingLayout(copyPassLayout.Ptr());
        {
            Shader* shaders[] = { copyShaderSet.vertexShader.Ptr(), copyShaderSet.fragmentShader.Ptr() };
            pb->SetShaders(ArrayView<Shader*>(shaders, 2));
        }
		RefPtr<RenderTargetLayout> copyRTLayout = hw->CreateRenderTargetLayout(MakeArrayView(AttachmentLayout(TextureUsage::ColorAttachment, StorageFormat::RGBA_F16)), true);
		RefPtr<Pipeline> copyPipeline = pb->ToPipeline(copyRTLayout.Ptr());
		RefPtr<DescriptorSet> copyDescSet = hw->CreateDescriptorSet(copyPassLayout.Ptr());
		List<RefPtr<CommandBuffer>> commandBuffers;
		List<RefPtr<FrameBuffer>> frameBuffers;
        for (int f = 0; f < 6; f++)
		{
			Matrix4 viewMatrix;
			Matrix4::CreateIdentityMatrix(viewMatrix);
			switch (f)
			{
			case 0:
			{
				viewMatrix = Matrix4(0.0f, 0.0f, -1.0f, 0.0f,
					                 0.0f, -1.0f, 0.0f, 0.0f,
					                 -1.0f, 0.0f, 0.0f, 0.0f,
					                 0.0f, 0.0f, 0.0f, 1.0f);
				break;
			}
			case 1:
			{
				viewMatrix = Matrix4(0.0f, 0.0f, 1.0f, 0.0f,
									0.0f, -1.0f, 0.0f, 0.0f,
									1.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 1.0f);
				break;
			}
			case 2:
			{
				viewMatrix = Matrix4(1.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, -1.0f, 0.0f,
									0.0f, 1.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 1.0f);
				break;
			}
			case 3:
			{
				viewMatrix = Matrix4(1.0f, 0.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 1.0f, 0.0f,
									0.0f, -1.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 1.0f);
				break;
			}
			case 4:
			{
				viewMatrix = Matrix4(1.0f, 0.0f, 0.0f, 0.0f,
									0.0f, -1.0f, 0.0f, 0.0f,
									0.0f, 0.0f, -1.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 1.0f);
				break;
			}
			case 5:
			{
				viewMatrix = Matrix4(-1.0f, 0.0f, 0.0f, 0.0f,
									0.0f, -1.0f, 0.0f, 0.0f,
									0.0f, 0.0f, 1.0f, 0.0f,
									0.0f, 0.0f, 0.0f, 1.0f);
				break;
			}
			}

			viewMatrix.values[12] = -(viewMatrix.values[0] * position.x + viewMatrix.values[4] * position.y + viewMatrix.values[8] * position.z);
			viewMatrix.values[13] = -(viewMatrix.values[1] * position.x + viewMatrix.values[5] * position.y + viewMatrix.values[9] * position.z);
			viewMatrix.values[14] = -(viewMatrix.values[2] * position.x + viewMatrix.values[6] * position.y + viewMatrix.values[10] * position.z);

			params.view.Transform = viewMatrix;
			renderProc->Run(params);

			copyDescSet->BeginUpdate();
			copyDescSet->Update(0, renderProc->GetOutput()->Texture.Ptr(), TextureAspect::Color);
			copyDescSet->Update(1, sharedRes->nearestSampler.Ptr());
			copyDescSet->EndUpdate();
            hw->BeginJobSubmission();

			RefPtr<FrameBuffer> fb0, fb1;
			// copy to level 0 of tempEnv
			{
				RenderAttachments attachments;
				attachments.SetAttachment(0, tempEnv.Ptr(), (TextureCubeFace)f, 0);
				fb0 = copyRTLayout->CreateFrameBuffer(attachments);
				frameBuffers.Add(fb0);
				auto cmdBuffer = hw->CreateCommandBuffer();
				cmdBuffer->BeginRecording(fb0.Ptr());
                cmdBuffer->SetViewport(Viewport(0, 0, resolution, resolution));
				cmdBuffer->BindPipeline(copyPipeline.Ptr());
				cmdBuffer->BindDescriptorSet(0, copyDescSet.Ptr());
				cmdBuffer->BindVertexBuffer(sharedRes->fullScreenQuadVertBuffer.Ptr(), 0);
				cmdBuffer->Draw(0, 4);
				cmdBuffer->EndRecording();
				commandBuffers.Add(cmdBuffer);
                hw->QueueRenderPass(fb0.Ptr(), true, MakeArrayView(cmdBuffer));
			}

			// copy to level 0 of result
			{
				RenderAttachments attachments;
				attachments.SetAttachment(0, dest, id, (TextureCubeFace)f, 0);
				fb1 = copyRTLayout->CreateFrameBuffer(attachments);
				frameBuffers.Add(fb1);
				auto cmdBuffer = hw->CreateCommandBuffer();
				cmdBuffer->BeginRecording(fb1.Ptr());
                cmdBuffer->SetViewport(Viewport(0, 0, resolution, resolution));
				cmdBuffer->BindPipeline(copyPipeline.Ptr());
				cmdBuffer->BindDescriptorSet(0, copyDescSet.Ptr());
				cmdBuffer->BindVertexBuffer(sharedRes->fullScreenQuadVertBuffer.Ptr(), 0);
				cmdBuffer->Draw(0, 4);
				cmdBuffer->EndRecording();
				commandBuffers.Add(cmdBuffer);
                hw->QueueRenderPass(fb1.Ptr(), true, MakeArrayView(cmdBuffer));
			}
            hw->EndJobSubmission(fence.Ptr());
            fence->Wait();
            fence->Reset();
            hw->ResetTempBufferVersion(0);
		}
		// prefilter
		RefPtr<PipelineBuilder> pb2 = hw->CreatePipelineBuilder();
		pb2->FixedFunctionStates.PrimitiveTopology = PrimitiveType::TriangleStrips;
        pb2->FixedFunctionStates.cullMode = CullMode::Disabled;
		pb2->SetVertexLayout(quadVert);
		RefPtr<DescriptorSetLayout> prefilterPassLayout = hw->CreateDescriptorSetLayout(MakeArray(
			DescriptorLayout(StageFlags::sfGraphics, 0, BindingType::UniformBuffer),
			DescriptorLayout(StageFlags::sfGraphics, 1, BindingType::Texture),
			DescriptorLayout(StageFlags::sfGraphics, 2, BindingType::Sampler)).GetArrayView());
		RefPtr<Buffer> uniformBuffer = hw->CreateMappedBuffer(BufferUsage::UniformBuffer, sizeof(PrefilterUniform));
		pb2->SetBindingLayout(prefilterPassLayout.Ptr());
        {
            Shader* shaderList[] = { prefilterShaderSet.vertexShader.Ptr(), prefilterShaderSet.fragmentShader.Ptr() };
            pb2->SetShaders(ArrayView<Shader*>(shaderList, 2));
        }
		RefPtr<RenderTargetLayout> prefilterRTLayout = hw->CreateRenderTargetLayout(MakeArrayView(AttachmentLayout(TextureUsage::ColorAttachment, StorageFormat::RGBA_F16)), true);
		RefPtr<Pipeline> prefilterPipeline = pb2->ToPipeline(prefilterRTLayout.Ptr());
		RefPtr<DescriptorSet> prefilterDescSet = hw->CreateDescriptorSet(prefilterPassLayout.Ptr());
		prefilterDescSet->BeginUpdate();
		prefilterDescSet->Update(0, uniformBuffer.Ptr());
		prefilterDescSet->Update(1, tempEnv.Ptr(), TextureAspect::Color);
		prefilterDescSet->Update(2, sharedRes->nearestSampler.Ptr());
		prefilterDescSet->EndUpdate();
		
		for (int f = 0; f < 6; f++)
		{
			PrefilterUniform prefilterParams;
			switch (f)
			{
			case 0:
				prefilterParams.origin = Vec4::Create(1.0f, 1.0f, 1.0f, 0.0f);
				prefilterParams.r = Vec4::Create(1.0f, 0.0f, 0.0f, 0.0f);
				prefilterParams.s = Vec4::Create(0.0f, 0.0f, -1.0f, 0.0f);
				prefilterParams.t = Vec4::Create(0.0f, -1.0f, 0.0f, 0.0f);
				break;
			case 1:
				prefilterParams.origin = Vec4::Create(-1.0f, 1.0f, -1.0f, 0.0f);
				prefilterParams.r = Vec4::Create(1.0f, 0.0f, 0.0f, 0.0f);
				prefilterParams.s = Vec4::Create(0.0f, 0.0f, 1.0f, 0.0f);
				prefilterParams.t = Vec4::Create(0.0f, -1.0f, 0.0f, 0.0f);
				break;
			case 2:
				prefilterParams.origin = Vec4::Create(-1.0f, 1.0f, -1.0f, 0.0f);
				prefilterParams.r = Vec4::Create(0.0f, 1.0f, 0.0f, 0.0f);
				prefilterParams.s = Vec4::Create(1.0f, 0.0f, 0.0f, 0.0f);
				prefilterParams.t = Vec4::Create(0.0f, 0.0f, 1.0f, 0.0f);
				break;
			case 3:
				prefilterParams.origin = Vec4::Create(-1.0f, -1.0f, 1.0f, 0.0f);
				prefilterParams.r = Vec4::Create(0.0f, -1.0f, 0.0f, 0.0f);
				prefilterParams.s = Vec4::Create(1.0f, 0.0f, 0.0f, 0.0f);
				prefilterParams.t = Vec4::Create(0.0f, 0.0f, -1.0f, 0.0f);
				break;
			case 4:
				prefilterParams.origin = Vec4::Create(-1.0f, 1.0f, 1.0f, 0.0f);
				prefilterParams.r = Vec4::Create(0.0f, 0.0f, 1.0f, 0.0f);
				prefilterParams.s = Vec4::Create(1.0f, 0.0f, 0.0f, 0.0f);
				prefilterParams.t = Vec4::Create(0.0f, -1.0f, 0.0f, 0.0f);
				break;
			case 5:
				prefilterParams.origin = Vec4::Create(1.0f, 1.0f, -1.0f, 0.0f);
				prefilterParams.r = Vec4::Create(0.0f, 0.0f, -1.0f, 0.0f);
				prefilterParams.s = Vec4::Create(-1.0f, 0.0f, 0.0f, 0.0f);
				prefilterParams.t = Vec4::Create(0.0f, -1.0f, 0.0f, 0.0f);
				break;
			}
			for (int l = 1; l < numLevels; l++)
			{
				prefilterParams.roughness = (l / (float)(numLevels - 1));

				uniformBuffer->SetData(&prefilterParams, sizeof(prefilterParams));

				RenderAttachments attachments;
				attachments.SetAttachment(0, dest, id, (TextureCubeFace)f, l);
				RefPtr<FrameBuffer> fb = prefilterRTLayout->CreateFrameBuffer(attachments);
				frameBuffers.Add(fb);
				auto cmdBuffer = hw->CreateCommandBuffer();
				cmdBuffer->BeginRecording(fb.Ptr());
                cmdBuffer->SetEventMarker((String("Prefilter Light Probe Level ") + String(l)).Buffer(), 0);
                cmdBuffer->SetViewport(Viewport(0, 0, resolution >> l, resolution >> l));
				cmdBuffer->BindPipeline(prefilterPipeline.Ptr());
				cmdBuffer->BindDescriptorSet(0, prefilterDescSet.Ptr());
				cmdBuffer->BindVertexBuffer(sharedRes->fullScreenQuadVertBuffer.Ptr(), 0);
				cmdBuffer->Draw(0, 4);
				cmdBuffer->EndRecording();
				commandBuffers.Add(cmdBuffer);
                hw->BeginJobSubmission();
                hw->QueueRenderPass(fb.Ptr(), true, cmdBuffer);
                hw->EndJobSubmission(fence.Ptr());
                fence->Wait();
                fence->Reset();
                hw->ResetTempBufferVersion(0);
			}
		}
	}
}
