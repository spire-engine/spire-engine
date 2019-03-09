#include "ComputeTaskManager.h"
#include "Engine.h"

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
        manager->memory.SetDataAsync(uniformBufferOffset, data, size);
    }

    ComputeKernel * ComputeTaskManager::LoadKernel(CoreLib::String shaderName, CoreLib::String functionName)
    {
        auto kernelKey = shaderName + "/" + functionName;
        RefPtr<ComputeKernel> cachedKernel;
        if (kernels.TryGetValue(kernelKey, cachedKernel))
            return cachedKernel.Ptr();

        RefPtr<ComputeKernelImpl> kernel = new ComputeKernelImpl();
        auto shaderEntryPoint = Engine::GetShaderCompiler()->LoadShaderEntryPoint(shaderName, functionName);
        ShaderCompilationResult crs;
        auto succ = Engine::GetShaderCompiler()->CompileShader(crs, MakeArrayView(shaderEntryPoint));
        if (!succ)
        {
            Engine::Print("Cannot compile compute shader kernel \'%S'\n", shaderName.ToWString());
            return nullptr;
        }
        kernel->shader = hardwareRenderer->CreateShader(ShaderType::ComputeShader, (char*)crs.ShaderCode.Buffer(), crs.ShaderCode.Count());
        // todo create descriptor set layout here
         // crs.BindingLayouts[0].Descriptors
        //descriptorSetLayout = hardwareRenderer->CreateDescriptorSetLayout(crs.BindingLayouts)
        RefPtr<PipelineBuilder> pb = hardwareRenderer->CreatePipelineBuilder();
        kernel->pipeline = pb->CreateComputePipeline(MakeArrayView(kernel->descriptorSetLayout.Ptr()), kernel->shader.Ptr());

        kernels[kernelKey] = kernel;
        return kernel.Ptr();
    }

    ComputeTaskManager::ComputeTaskManager(HardwareRenderer * hw)
    {
        hardwareRenderer = hw;
        commandBuffer = hardwareRenderer->CreateCommandBuffer();
        fence = hardwareRenderer->CreateFence();
        memory.Init(hw, BufferUsage::UniformBuffer, true, 21, hardwareRenderer->UniformBufferAlignment());
    }

}