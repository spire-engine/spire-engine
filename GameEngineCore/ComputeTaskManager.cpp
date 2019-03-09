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

    void ComputeTaskInstance::SetUniformData(void * data, int size)
    {
        CoreLib::Diagnostics::DynamicAssert("uniform size mismatch.", size == uniformBufferSize);
        memcpy(uniformData, data, size);
    }

    void ComputeTaskInstance::SetBinding(CoreLib::ArrayView<ResourceBinding> resources)
    {
        descriptorSet->BeginUpdate();
        for (int i = 0; i < resources.Count(); i++)
        {
            switch (resources[i].type)
            {
            case ResourceBinding::BindingType::Texture:
                descriptorSet->Update(i, resources[i].resourceHandles.textureBinding, TextureAspect::Color);
                break;
            case ResourceBinding::BindingType::Sampler:
                descriptorSet->Update(i, resources[i].resourceHandles.samplerBinding);
                break;
            case ResourceBinding::BindingType::StorageBuffer:
                descriptorSet->Update(i, resources[i].resourceHandles.storageBufferBinding, resources[i].bufferOffset, resources[i].bufferLength);
                break;
            case ResourceBinding::BindingType::TextureArray:
                descriptorSet->Update(i, resources[i].textureArrayBinding, TextureAspect::Color);
                break;
            }
        }
        descriptorSet->EndUpdate();
    }

    void ComputeTaskInstance::Dispatch(CommandBuffer * cmdBuffer, int x, int y, int z)
    {
        auto kernelImpl = (ComputeKernelImpl*)kernel;
        cmdBuffer->BindPipeline(kernelImpl->pipeline.Ptr());
        cmdBuffer->BindDescriptorSet(0, descriptorSet.Ptr());
        cmdBuffer->DispatchCompute(x, y, z);
    }

    void ComputeTaskInstance::Run(int x, int y, int z, Fence * fence)
    {
        if (!fence)
            fence = manager->fence.Ptr();
        manager->commandBuffer->BeginRecording();
        Dispatch(manager->commandBuffer.Ptr(), x, y, z);
        manager->commandBuffer->EndRecording();
        manager->hardwareRenderer->ExecuteNonRenderCommandBuffers(MakeArrayView(manager->commandBuffer.Ptr()), fence);
    }

    ComputeTaskInstance::~ComputeTaskInstance()
    {
        if (uniformBufferSize)
            manager->memory.Free(uniformData, uniformBufferSize);
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
        kernel->shader = hardwareRenderer->CreateShader(ShaderType::ComputeShader, (char*)crs.ShaderCode.Buffer(), crs.ShaderCode.Count());

        kernel->descriptorSetLayout = hardwareRenderer->CreateDescriptorSetLayout(crs.BindingLayouts[0].Descriptors.GetArrayView());
        RefPtr<PipelineBuilder> pb = hardwareRenderer->CreatePipelineBuilder();
        kernel->pipeline = pb->CreateComputePipeline(MakeArrayView(kernel->descriptorSetLayout.Ptr()), kernel->shader.Ptr());
        kernels[kernelKey] = kernel;
        return kernel.Ptr();
    }

    RefPtr<ComputeTaskInstance> ComputeTaskManager::CreateComputeTaskInstance(ComputeKernel * kernel, CoreLib::ArrayView<ResourceBinding> resources, void * uniformData, int uniformSize)
    {
        ComputeKernelImpl* kernelImpl = (ComputeKernelImpl*)kernel;
        RefPtr<ComputeTaskInstance> inst = new ComputeTaskInstance();
        inst->manager = this;
        inst->descriptorSet = hardwareRenderer->CreateDescriptorSet(kernelImpl->descriptorSetLayout.Ptr());

        inst->uniformBufferSize = uniformSize;
        if (uniformSize)
            inst->uniformData = memory.Alloc(uniformSize);
        if (uniformData)
            inst->SetUniformData(uniformData, uniformSize);
        inst->SetBinding(resources);
        return inst;
    }

    ComputeTaskManager::ComputeTaskManager(HardwareRenderer * hw, IShaderCompiler* compiler)
    {
        shaderCompiler = compiler;
        hardwareRenderer = hw;
        commandBuffer = hardwareRenderer->CreateCommandBuffer();
        fence = hardwareRenderer->CreateFence();
        memory.Init(hw, BufferUsage::UniformBuffer, true, 21, hardwareRenderer->UniformBufferAlignment());
    }

}