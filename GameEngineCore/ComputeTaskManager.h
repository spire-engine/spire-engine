#ifndef GAME_ENGINE_COMPUTE_TASK_H
#define GAME_ENGINE_COMPUTE_TASK_H

#include "CoreLib/Basic.h"
#include "HardwareRenderer.h"
#include "DeviceMemory.h"

namespace GameEngine
{
    class ComputeKernel : public RefObject
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
            Texture, Sampler, StorageBuffer
        };
        BindingType type;
        int bufferOffset;
        int bufferLength;
        ResourceBinding() {}
        ResourceBinding(Texture* texture)
        {
            type = BindingType::Texture;
            resourceHandles.textureBinding = texture;
        }
        ResourceBinding(TextureSampler* sampler)
        {
            type = BindingType::Texture;
            resourceHandles.samplerBinding = sampler;
        }
        ResourceBinding(Buffer* buffer, int offset, int size)
        {
            type = BindingType::Texture;
            resourceHandles.storageBufferBinding = buffer;
            bufferOffset = offset;
            bufferLength = size;
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
        ComputeTaskManager* manager;
        CoreLib::RefPtr<DescriptorSet> descriptorSet;
        int uniformBufferOffset, uniformBufferSize;
    public:
        void SetUniformData(void * data, int size);
        void SetBinding(CoreLib::ArrayView<ResourceBinding> resources);
        void Dispatch(CommandBuffer* cmdBuffer, int x, int y, int z, Fence* fence = nullptr);
        void Run(int x, int y, int z);
    };

    class ComputeTaskManager
    {
        friend class ComputeTaskInstance;
    private:
        CoreLib::Dictionary<CoreLib::String, CoreLib::RefPtr<ComputeKernel>> kernels;
        HardwareRenderer* hardwareRenderer;
        CoreLib::RefPtr<CommandBuffer> commandBuffer;
        CoreLib::RefPtr<Fence> fence;
        DeviceMemory memory;
    public:
        ComputeKernel* LoadKernel(CoreLib::String shaderName, CoreLib::String functionName);
        ComputeTaskInstance CreateComputeTaskInstance(ComputeKernel* kernel, CoreLib::ArrayView<ResourceBinding> resources,
            void * uniformData, int uniformSize);
        CommandBuffer& GetCommandBuffer()
        {
            return *commandBuffer;
        }
        Fence* GetFence()
        {
            return fence.Ptr();
        }
        ComputeTaskManager(HardwareRenderer * hw);
    };
}

#endif