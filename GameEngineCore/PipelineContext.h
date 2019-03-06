#ifndef GAME_ENGINE_PIPELINE_CONTEXT_H
#define GAME_ENGINE_PIPELINE_CONTEXT_H

#include "ShaderCompiler.h"
#include "HardwareRenderer.h"
#include "DeviceMemory.h"
#include "EngineLimits.h"
#include "Mesh.h"

namespace GameEngine
{
	class ShaderKey
	{
	public:
		long long ModuleIds[2] = {0, 0};
		int count = 0;
		inline int GetHashCode()
		{
			return (int)(ModuleIds[0] ^ ModuleIds[1]);
		}
		bool operator == (const ShaderKey & key)
		{
			return ModuleIds[0] == key.ModuleIds[0] && ModuleIds[1] == key.ModuleIds[1];
		}
	};
	class ShaderKeyBuilder
	{
	public:
		ShaderKey Key;
		inline void Clear()
		{
			Key.count = 0;
			Key.ModuleIds[0] = 0;
			Key.ModuleIds[1] = 0;
		}
		inline void FlipLeadingByte(unsigned int header)
		{
			Key.ModuleIds[0] ^= ((long long)header << 54);
		}
		inline void Append(unsigned int moduleId)
		{
			Key.ModuleIds[Key.count >> 2] ^= ((long long)moduleId << (Key.count & 3) * 16);
			Key.count++;
		}
		inline void Pop()
		{
			Key.count--;
			Key.ModuleIds[Key.count >> 2] ^= (Key.ModuleIds[Key.count >> 2] & (0xFFFFULL << (Key.count & 3) * 16));
		}
	};

	class MeshVertexFormat;
	
	class ModuleInstance
	{
		friend class PipelineContext;
	public:
		int ModuleId;
		//USE_POOL_ALLOCATOR(ModuleInstance, MaxModuleInstances)
	private:
        ShaderTypeSymbol * typeSymbol = nullptr;
		CoreLib::Array<CoreLib::RefPtr<DescriptorSet>, DynamicBufferLengthMultiplier> descriptors;
		int currentDescriptor = 0;
        DescriptorSetLayout * descLayout = nullptr;
	public:
		DeviceMemory * UniformMemory = nullptr;
		int BufferOffset = 0, BufferLength = 0;
		CoreLib::String BindingName;
		void SetUniformData(void * data, int length, int dstOffset = 0);
		ModuleInstance() = default;
		void Init(ShaderTypeSymbol * m)
		{
            ModuleId = m->TypeId;
            typeSymbol = m;
		}
		~ModuleInstance();
        ShaderTypeSymbol * GetTypeSymbol()
		{
			return typeSymbol;
		}
        DescriptorSetLayout* GetDescriptorSetLayout() { return descLayout; }
		void SetDescriptorSetLayout(HardwareRenderer * hw, DescriptorSetLayout * layout);
		DescriptorSet * GetDescriptorSet(int i)
		{
			return descriptors[i].Ptr();
		}
		DescriptorSet * GetCurrentDescriptorSet()
		{
			return descriptors[currentDescriptor].Ptr();
		}
		int GetCurrentVersion()
		{
			return currentDescriptor;
		}
		operator bool()
		{
			return typeSymbol != nullptr;
		}
	};

	using DescriptorSetBindingArray = CoreLib::Array<DescriptorSet*, 32>;

	class PipelineClass
	{
	public:
		int Id = 0;
		CoreLib::List<CoreLib::RefPtr<Shader>> shaders;
		CoreLib::RefPtr<Pipeline> pipeline;
		CoreLib::List<CoreLib::RefPtr<DescriptorSetLayout>> descriptorSetLayouts;
	};

	class RenderStat;

	class PipelineContext
	{
	private:
		int modulePtr = 0;
		ShaderKey lastKey; 
		unsigned int lastVtxId = 0;
        PrimitiveType lastPrimType = PrimitiveType::Triangles;
        bool shaderKeyChanged = true; 
        ShaderEntryPoint* vertexShaderEntryPoint, *fragmentShaderEntryPoint;
		ModuleInstance* modules[32];
		RenderTargetLayout * renderTargetLayout = nullptr;
		PipelineClass * lastPipeline = nullptr;
		FixedFunctionPipelineStates fixedFunctionStates;
		CoreLib::EnumerableDictionary<ShaderKey, CoreLib::RefPtr<PipelineClass>> pipelineObjects;
		ShaderKeyBuilder shaderKeyBuilder;
		HardwareRenderer * hwRenderer;
		RenderStat * renderStats = nullptr;
		CoreLib::Dictionary<int, VertexFormat> vertexFormats;
		PipelineClass * GetPipelineInternal(MeshVertexFormat * vertFormat, int vtxId, PrimitiveType primType);
		PipelineClass* CreatePipeline(MeshVertexFormat * vertFormat, PrimitiveType primType);
	public:
		PipelineContext() = default;
		void Init(HardwareRenderer * hw, RenderStat * pRenderStats)
		{
			hwRenderer = hw;
			renderStats = pRenderStats;
		}
		inline RenderStat * GetRenderStat() 
		{
			return renderStats;
		}
		VertexFormat LoadVertexFormat(MeshVertexFormat vertFormat);
		void BindEntryPoint(ShaderEntryPoint * pVS, ShaderEntryPoint * pFS, RenderTargetLayout * pRenderTargetLayout, FixedFunctionPipelineStates * states)
		{
			vertexShaderEntryPoint = pVS;
            fragmentShaderEntryPoint = pFS;
			renderTargetLayout = pRenderTargetLayout;
			fixedFunctionStates = *states;
			shaderKeyChanged = true;
			modulePtr = 0;
			for (int i = 0; i < sizeof(modules) / sizeof(ModuleInstance*); i++)
				modules[i] = nullptr;
		}
		void SetCullMode(CullMode mode)
		{
			fixedFunctionStates.CullMode = mode;
		}
		void PushModuleInstance(ModuleInstance * module)
		{
			if (!modules[modulePtr] || modules[modulePtr]->ModuleId != module->ModuleId)
			{
				shaderKeyChanged = true;
			}
			modules[modulePtr] = module;
			++modulePtr;
		}
		void PushModuleInstanceNoShaderChange(ModuleInstance * module)
		{
			modules[modulePtr] = module;
			++modulePtr;
		}
		void PopModuleInstance()
		{
			--modulePtr;
		}
		void GetBindings(DescriptorSetBindingArray & bindings)
		{
			bindings.Clear();
            for (int i = 0; i < modulePtr; i++)
            {
                if (auto dset = modules[i]->GetCurrentDescriptorSet())
				    bindings.Add(dset);
            }
		}
		inline PipelineClass* GetPipeline(MeshVertexFormat * vertFormat, PrimitiveType primType)
		{
			unsigned int vtxId = (unsigned int)vertFormat->GetTypeId();
			if (!shaderKeyChanged && vtxId == lastVtxId && primType == lastPrimType)
				return lastPipeline;
			return GetPipelineInternal(vertFormat, vtxId, primType);
		}
	};
}

#endif