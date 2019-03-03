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

	PipelineClass * PipelineContext::GetPipelineInternal(MeshVertexFormat * vertFormat, int vtxId, PrimitiveType primType)
	{
		shaderKeyChanged = false;
		lastVtxId = vtxId;
        lastPrimType = primType;
		shaderKeyBuilder.Clear();
		shaderKeyBuilder.Append(fragmentShaderEntryPoint->Id);
		shaderKeyBuilder.FlipLeadingByte(vtxId);
		for (int i = 0; i < modulePtr; i++)
			shaderKeyBuilder.Append(modules[i]->ModuleId);
        shaderKeyBuilder.Append((unsigned int)primType);
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
		lastPipeline = CreatePipeline(vertFormat, primType);
		return lastPipeline;
	}

	PipelineClass * PipelineContext::CreatePipeline(MeshVertexFormat * vertFormat, PrimitiveType primType)
	{
		RefPtr<PipelineBuilder> pipelineBuilder = hwRenderer->CreatePipelineBuilder();

		pipelineBuilder->FixedFunctionStates = fixedFunctionStates;
        pipelineBuilder->FixedFunctionStates.PrimitiveTopology = primType;

		// Set vertex layout
		pipelineBuilder->SetVertexLayout(LoadVertexFormat(*vertFormat));

		// Compile shaders
        ShaderCompilationEnvironment env;
        for (int i = 0; i < modulePtr; i++)
        {
            env.SpecializationTypes.Add(modules[i]->typeSymbol);
        }
        env.SpecializationTypes.Add(vertFormat->GetTypeSymbol());
        struct CompilationTask
        {
            ShaderEntryPoint * entryPoint;
            ShaderType shaderType;
        };
        RefPtr<PipelineClass> pipelineClass = new PipelineClass();
        static int pipelineClassId = 0;
        pipelineClassId++;
        pipelineClass->Id = pipelineClassId;
        Array<ShaderEntryPoint*, 2> entryPoints;
        entryPoints.SetSize(2);
        entryPoints[0] = vertexShaderEntryPoint;
        entryPoints[1] = fragmentShaderEntryPoint;
		List<RefPtr<DescriptorSetLayout>> descSetLayouts;
        ShaderCompilationResult compileRs;
        Engine::GetShaderCompiler()->CompileShader(compileRs, entryPoints.GetArrayView(), &env);
        auto vsObj = hwRenderer->CreateShader(ShaderType::VertexShader, compileRs.ShaderCode[0].Buffer(), compileRs.ShaderCode[0].Count());
        auto fsObj = hwRenderer->CreateShader(ShaderType::FragmentShader, compileRs.ShaderCode[1].Buffer(), compileRs.ShaderCode[1].Count());
        pipelineClass->shaders.Add(vsObj);
        pipelineClass->shaders.Add(fsObj);

        for (auto & descSet : compileRs.BindingLayouts)
        {
            if (descSet.BindingPoint == -1 || descSet.Descriptors.Count() == 0)
                continue;
            for (auto & desc : descSet.Descriptors)
                desc.Stages = StageFlags::sfGraphicsAndCompute;
            auto layout = hwRenderer->CreateDescriptorSetLayout(descSet.Descriptors.GetArrayView());
            if (descSet.BindingPoint >= descSetLayouts.Count())
                descSetLayouts.SetSize(descSet.BindingPoint + 1);
            descSetLayouts[descSet.BindingPoint] = layout;

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
        descLayout = layout;
        if (layout)
        {
            for (int i = 0; i < descriptors.GetCapacity(); i++)
                descriptors.Add(hw->CreateDescriptorSet(layout));
        }
        else
        {
            for (int i = 0; i < descriptors.GetCapacity(); i++)
                descriptors.Add(nullptr);
        }
	}

}

