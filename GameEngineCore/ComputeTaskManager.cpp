#include "ComputeTaskManager.h"
#include "CoreLib/DebugAssert.h"

using namespace CoreLib;

namespace GameEngine
{
    class ComputeKernelImpl : public ComputeKernel
    {
    public:
        RefPtr<Shader> shader;
        RefPtr<Pipeline> pipeline;
        RefPtr<DescriptorSetLayout> descriptorSetLayout;
    };

    void ComputeTaskInstance::UpdateVersionedParameters(void * data, int size, ArrayView<ResourceBinding> resources)
    {
        if (isVersioned)
        {
            version++;
            version = version % DynamicBufferLengthMultiplier;
        }
        SetUniformData(data, size);
        SetBinding(resources);
    }

    void ComputeTaskInstance::SetUniformData(void * data, int size)
    {
        CoreLib::Diagnostics::DynamicAssert("uniform size mismatch.", size <= uniformBufferSize);
        manager->memory.SetDataAsync(uniformDataPtr + uniformBufferSize * version, data, size);
    }

    void ComputeTaskInstance::SetBinding(CoreLib::ArrayView<ResourceBinding> resources)
    {
        auto descriptorSet = descriptorSets[version].Ptr();
        descriptorSet->BeginUpdate();
        int bindingOffset = 0;
        if (uniformBufferSize)
        {
            bindingOffset = 1;
            descriptorSet->Update(0, manager->memory.GetBuffer(), uniformDataPtr + uniformBufferSize * version, uniformBufferSize);
        }
        for (int i = 0; i < resources.Count(); i++)
        {
            switch (resources[i].type)
            {
            case ResourceBinding::BindingType::Texture:
                descriptorSet->Update(bindingOffset + i, resources[i].resourceHandles.textureBinding, TextureAspect::Color);
                break;
            case ResourceBinding::BindingType::Sampler:
                descriptorSet->Update(bindingOffset + i, resources[i].resourceHandles.samplerBinding);
                break;
            case ResourceBinding::BindingType::StorageBuffer:
                descriptorSet->Update(bindingOffset + i, resources[i].resourceHandles.storageBufferBinding, resources[i].bufferOffset, resources[i].bufferLength);
                break;
            case ResourceBinding::BindingType::TextureArray:
                descriptorSet->Update(bindingOffset + i, resources[i].textureArrayBinding, TextureAspect::Color);
                break;
            }
        }
        descriptorSet->EndUpdate();
    }

    void ComputeTaskInstance::Dispatch(CommandBuffer * cmdBuffer, int x, int y, int z)
    {
        auto kernelImpl = (ComputeKernelImpl*)kernel;
        cmdBuffer->BindPipeline(kernelImpl->pipeline.Ptr());
        cmdBuffer->BindDescriptorSet(0, descriptorSets[version].Ptr());
        cmdBuffer->DispatchCompute(x, y, z);
    }

    void ComputeTaskInstance::Queue(int x, int y, int z)
    {
        auto kernelImpl = (ComputeKernelImpl*)kernel;
        manager->hardwareRenderer->QueueComputeTask(kernelImpl->pipeline.Ptr(), descriptorSets[version].Ptr(), x, y, z);
    }

    void ComputeTaskInstance::Run(CommandBuffer * cmdBuffer, int x, int y, int z, Fence * fence)
    {
        cmdBuffer->BeginRecording();
        Dispatch(cmdBuffer, x, y, z);
        cmdBuffer->EndRecording();
        manager->hardwareRenderer->BeginJobSubmission();
        manager->hardwareRenderer->QueueNonRenderCommandBuffers(MakeArrayView(cmdBuffer));
        manager->hardwareRenderer->EndJobSubmission(fence);
    }

    ComputeTaskInstance::~ComputeTaskInstance()
    {
        if (uniformBufferSize)
            manager->memory.Free((char*)manager->memory.BufferPtr() + uniformDataPtr, uniformBufferSize * (isVersioned?DynamicBufferLengthMultiplier:1));
    }

    ComputeKernel * ComputeTaskManager::LoadKernel(CoreLib::String shaderName, CoreLib::String functionName)
    {
        auto kernelKey = shaderName + "/" + functionName;
        RefPtr<ComputeKernel> cachedKernel;
        if (kernels.TryGetValue(kernelKey, cachedKernel))
            return cachedKernel.Ptr();

        RefPtr<ComputeKernelImpl> kernel = new ComputeKernelImpl();
        auto shaderEntryPoint = shaderCompiler->LoadShaderEntryPoint(shaderName, functionName);
        ShaderCompilationResult crs;
        auto succ = shaderCompiler->CompileShader(crs, MakeArrayView(shaderEntryPoint));
        if (!succ)
        {
            throw InvalidOperationException(String("Cannot compile compute shader kernel \')") + shaderName + String("'"));
        }
        kernel->shader = hardwareRenderer->CreateShader(ShaderType::ComputeShader, (char*)crs.ShaderCode[0].Buffer(), crs.ShaderCode[0].Count());
        List<DescriptorLayout> descriptors = crs.BindingLayouts[0].Descriptors;
        for (auto & desc : descriptors)
            desc.Stages = sfCompute;
        kernel->descriptorSetLayout = hardwareRenderer->CreateDescriptorSetLayout(crs.BindingLayouts[0].Descriptors.GetArrayView());
        RefPtr<PipelineBuilder> pb = hardwareRenderer->CreatePipelineBuilder();
        kernel->pipeline = pb->CreateComputePipeline(MakeArrayView(kernel->descriptorSetLayout.Ptr()), kernel->shader.Ptr());
        kernels[kernelKey] = kernel;
        return kernel.Ptr();
    }

    RefPtr<ComputeTaskInstance> ComputeTaskManager::CreateComputeTaskInstance(ComputeKernel * kernel, int uniformSize, bool isVersioned)
    {
        ComputeKernelImpl* kernelImpl = (ComputeKernelImpl*)kernel;
        RefPtr<ComputeTaskInstance> inst = new ComputeTaskInstance();
        inst->manager = this;

        int align = hardwareRenderer->UniformBufferAlignment();
        uniformSize = ((uniformSize + align - 1) / align) * align;

        inst->descriptorSets[0] = hardwareRenderer->CreateDescriptorSet(kernelImpl->descriptorSetLayout.Ptr());
        for (int i = 1; i < DynamicBufferLengthMultiplier; i++)
            inst->descriptorSets[i] = hardwareRenderer->CreateDescriptorSet(kernelImpl->descriptorSetLayout.Ptr());

        inst->kernel = kernel;
        inst->isVersioned = isVersioned;
        inst->uniformBufferSize = uniformSize;
        if (uniformSize)
            inst->uniformDataPtr = (int)((char*)memory.Alloc(uniformSize * (isVersioned ? DynamicBufferLengthMultiplier : 1)) - memory.BufferPtr());
        return inst;
    }

    ComputeTaskManager::ComputeTaskManager(HardwareRenderer * hw, IShaderCompiler* compiler)
    {
        shaderCompiler = compiler;
        hardwareRenderer = hw;
        memory.Init(hw, BufferUsage::UniformBuffer, true, 21, hardwareRenderer->UniformBufferAlignment());
    }

}