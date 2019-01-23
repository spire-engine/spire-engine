#include "PipelineContext.h"
#include "Mesh.h"
#include "Engine.h"
#include "CoreLib/LibIO.h"
#include "ShaderCompiler.h"
#include "EngineLimits.h"
#include "Renderer.h"

using namespace CoreLib;
using namespace CoreLib::IO;

namespace GameEngine
{
	VertexFormat PipelineContext::LoadVertexFormat(MeshVertexFormat vertFormat)
	{
		VertexFormat rs;
		auto vertTypeId = vertFormat.GetTypeId();
		if (vertexFormats.TryGetValue(vertTypeId, rs))
			return rs;
		VertexFormat vertexFormat;
		int location = 0;

		const int UNNORMALIZED = 0;
		const int NORMALIZED = 1;

		// Always starts with vec3 pos
		rs.Attributes.Add(VertexAttributeDesc(DataType::Float3, UNNORMALIZED, 0, location));
		location++;

		for (int i = 0; i < vertFormat.GetUVChannelCount(); i++)
		{
			rs.Attributes.Add(VertexAttributeDesc(DataType::Half2, UNNORMALIZED, vertFormat.GetUVOffset(i), location));
			location++;
		}
		if (vertFormat.HasTangent())
		{
			rs.Attributes.Add(VertexAttributeDesc(DataType::UInt, UNNORMALIZED, vertFormat.GetTangentFrameOffset(), location));
			location++;
		}
		for (int i = 0; i < vertFormat.GetColorChannelCount(); i++)
		{
			rs.Attributes.Add(VertexAttributeDesc(DataType::Byte4, NORMALIZED, vertFormat.GetColorOffset(i), location));
			location++;
		}
		if (vertFormat.HasSkinning())
		{
			rs.Attributes.Add(VertexAttributeDesc(DataType::UInt, UNNORMALIZED, vertFormat.GetBoneIdsOffset(), location));
			location++;
			rs.Attributes.Add(VertexAttributeDesc(DataType::UInt, UNNORMALIZED, vertFormat.GetBoneWeightsOffset(), location));
			location++;
		}

		vertexFormats[vertTypeId] = rs;
		return rs;
	}

	PipelineClass * PipelineContext::GetPipelineInternal(MeshVertexFormat * vertFormat, int vtxId)
	{
		shaderKeyChanged = false;
		lastVtxId = vtxId;

		shaderKeyBuilder.Clear();
		shaderKeyBuilder.Append(fragmentShaderEntryPoint->Id);
		shaderKeyBuilder.FlipLeadingByte(vtxId);
		for (int i = 0; i < modulePtr; i++)
			shaderKeyBuilder.Append(modules[i]->ModuleId);
		/*
		if (shaderKeyBuilder.Key == lastKey)
		{
		return lastPipeline;
		}*/
		if (auto pipeline = pipelineObjects.TryGetValue(shaderKeyBuilder.Key))
		{
			//lastKey = shaderKeyBuilder.Key;
			lastPipeline = pipeline->Ptr();
			return pipeline->Ptr();
		}
		//lastKey = shaderKeyBuilder.Key;
		lastPipeline = CreatePipeline(vertFormat);
		return lastPipeline;
	}

	PipelineClass * PipelineContext::CreatePipeline(MeshVertexFormat * vertFormat)
	{
		RefPtr<PipelineBuilder> pipelineBuilder = hwRenderer->CreatePipelineBuilder();

		pipelineBuilder->FixedFunctionStates = fixedFunctionStates;

		// Set vertex layout
		pipelineBuilder->SetVertexLayout(LoadVertexFormat(*vertFormat));

		// Compile shaders
        ShaderCompilationEnvironment env;
		Array<ShaderTypeSymbol*, 32> spireModules;
        for (int i = 0; i < modulePtr; i++)
        {
            env.SpecializationTypes.Add(modules[i]->typeSymbol);
			spireModules.Add(modules[i]->typeSymbol);
        }
		spireModules.Add(vertFormat->GetTypeSymbol());
        struct CompilationTask
        {
            ShaderEntryPoint * entryPoint;
            ShaderType shaderType;
        };
        const CompilationTask tasks[] = { {vertexShaderEntryPoint, ShaderType::VertexShader}, {fragmentShaderEntryPoint, ShaderType::FragmentShader} };
        RefPtr<PipelineClass> pipelineClass = new PipelineClass();
        static int pipelineClassId = 0;
        pipelineClassId++;
        pipelineClass->Id = pipelineClassId;
		List<RefPtr<DescriptorSetLayout>> descSetLayouts;
        for (auto task : tasks)
        {
            ShaderCompilationResult compileRs;
            Engine::GetShaderCompiler()->CompileShader(compileRs, Engine::Instance()->GetRenderer()->GetHardwareRenderer()->GetShadingLanguage(),
                task.shaderType, task.entryPoint, &env);
            for (auto & diag : compileRs.Diagnostics)
            {
                Print("%S(%d): %S\n", String(diag.FileName).ToWString(), diag.Line, String(diag.Message).ToWString());
            }
            auto shaderObj = hwRenderer->CreateShader(tasks->shaderType, compileRs.ShaderCode.Buffer(), compileRs.ShaderCode.Count());
            pipelineClass->shaders.Add(shaderObj);

            for (auto & descSet : compileRs.BindingLayouts)
            {
                if (descSet.Value.BindingPoint == -1)
                    continue;
                for (auto & desc : descSet.Value.Descriptors)
                    desc.Stages = (StageFlags)(StageFlags::sfVertex | StageFlags::sfFragment);
                auto layout = hwRenderer->CreateDescriptorSetLayout(descSet.Value.Descriptors.GetArrayView());
                if (descSet.Value.BindingPoint >= descSetLayouts.Count())
                    descSetLayouts.SetSize(descSet.Value.BindingPoint + 1);
                descSetLayouts[descSet.Value.BindingPoint] = layout;

            }
        }
		pipelineBuilder->SetShaders(From(pipelineClass->shaders).Select([](const RefPtr<Shader>& s) {return s.Ptr(); }).ToList().GetArrayView());
		pipelineBuilder->SetBindingLayout(From(descSetLayouts).Select([](auto x) {return x.Ptr(); }).ToList().GetArrayView());
		pipelineClass->pipeline = pipelineBuilder->ToPipeline(renderTargetLayout);
		pipelineObjects[shaderKeyBuilder.Key] = pipelineClass;
		return pipelineClass.Ptr();
	}

	void ModuleInstance::SetUniformData(void * data, int length)
	{
#ifdef _DEBUG
		if (length > BufferLength)
			throw HardwareRendererException("insufficient uniform buffer.");
#endif
		if (length && UniformMemory)
		{
			currentDescriptor++;
			currentDescriptor = currentDescriptor % DynamicBufferLengthMultiplier;
			int alternateBufferOffset = currentDescriptor * BufferLength;
			UniformMemory->SetDataAsync(BufferOffset + alternateBufferOffset, data, length);
		}
	}

	//IMPL_POOL_ALLOCATOR(ModuleInstance, MaxModuleInstances)

	ModuleInstance::~ModuleInstance()
	{
		if (UniformMemory)
			UniformMemory->Free((char*)UniformMemory->BufferPtr() + BufferOffset, BufferLength * DynamicBufferLengthMultiplier);
	}

	void ModuleInstance::SetDescriptorSetLayout(HardwareRenderer * hw, DescriptorSetLayout * layout)
	{
		descriptors.Clear();
		for (int i = 0; i < descriptors.GetCapacity(); i++)
			descriptors.Add(hw->CreateDescriptorSet(layout));
	}

}

