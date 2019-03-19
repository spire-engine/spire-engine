#ifndef GAME_ENGINE_COMPUTE_TASK_H
#define GAME_ENGINE_COMPUTE_TASK_H

#include "CoreLib/Basic.h"
#include "HardwareRenderer.h"
#include "DeviceMemory.h"
#include "ShaderCompiler.h"
#include "EngineLimits.h"

namespace GameEngine
{
    class ComputeKernel : public CoreLib::RefObject
    {
    public:

    };

    class ResourceBinding
    {
    public:
        union BindingHandle
        {
            Texture* textureBinding;
            TextureSampler* samplerBinding;
            Buffer* storageBufferBinding;
        } resourceHandles;
        enum class BindingType
        {
            Texture, TextureArray, Sampler, StorageBuffer, StorageImage
        };
        BindingType type;
        int bufferOffset;
        int bufferLength;
        CoreLib::ArrayView<Texture*> textureArrayBinding;
        ResourceBinding() {}
        ResourceBinding(Texture* texture, BindingType bindingType = BindingType::Texture)
        {
            type = bindingType;
            resourceHandles.textureBinding = texture;
        }
        ResourceBinding(TextureSampler* sampler)
        {
            type = BindingType::Sampler;
            resourceHandles.samplerBinding = sampler;
        }
        ResourceBinding(Buffer* buffer, int offset, int size)
        {
            type = BindingType::StorageBuffer;
            resourceHandles.storageBufferBinding = buffer;
            bufferOffset = offset;
            bufferLength = size;
        }
        ResourceBinding(CoreLib::ArrayView<Texture*> texArray)
        {
            type = BindingType::TextureArray;
            textureArrayBinding = texArray;
        }
    };

    class LaunchedComputeTask : public CoreLib::RefObject
    {
    public:
        virtual void Wait() = 0;
    };

    class ComputeTaskManager;

    class ComputeTaskInstance
    {
    private:
        friend class ComputeTaskManager;
        ComputeTaskManager* manager;
        ComputeKernel* kernel;
        CoreLib::RefPtr<DescriptorSet> descriptorSets[DynamicBufferLengthMultiplier];
        int uniformBufferSize;
        int uniformDataPtr;
        int version = 0;
        bool isVersioned = false;
    public:
        void SetUniformData(void * data, int size);
        void SetBinding(CoreLib::ArrayView<ResourceBinding> resources);
        void UpdateVersionedParameters(void * data, int size, CoreLib::ArrayView<ResourceBinding> resources);
        void Dispatch(CommandBuffer* cmdBuffer, int x, int y, int z);
        void Queue(int x, int y, int z);
        void Run(CommandBuffer * cmdBuffer, int x, int y, int z, Fence* fence = nullptr);
        ~ComputeTaskInstance();
    };

    class ComputeTaskManager
    {
        friend class ComputeTaskInstance;
    private:
        IShaderCompiler * shaderCompiler;
        CoreLib::Dictionary<CoreLib::String, CoreLib::RefPtr<ComputeKernel>> kernels;
        HardwareRenderer* hardwareRenderer;
        DeviceMemory memory;
    public:
        ComputeKernel* LoadKernel(CoreLib::String shaderName, CoreLib::String functionName);
        CoreLib::RefPtr<ComputeTaskInstance> CreateComputeTaskInstance(ComputeKernel* kernel, int uniformSize, bool isVersioned = false);
        ComputeTaskManager(HardwareRenderer * hw, IShaderCompiler* shaderCompiler);
    };
}

#endif