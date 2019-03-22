#ifndef GAME_ENGINE_IMAGE_BARRIER_HELPER_H
#define GAME_ENGINE_IMAGE_BARRIER_HELPER_H

#include "CoreLib/Basic.h"
#include "HardwareRenderer.h"

namespace GameEngine
{
    class ImageBarrierHelper
    {
    public:
        enum DataDependencyType
        {
            RenderTargetToGraphics, ComputeToGraphics, RenderTargetToCompute, UndefinedToRenderTarget, SampledToRenderTarget, RenderTargetToComputeStorage,
            ComputeStorageToSample, ComputeStorageToRenderTarget
        };
        CoreLib::List<ImagePipelineBarrier> imageBarriers;
        void QueueImageBarrier(HardwareRenderer* hw, CoreLib::ArrayView<Texture*> texturesToUse, DataDependencyType dep)
        {
            imageBarriers.Clear();
            for (auto img : texturesToUse)
            {
                ImagePipelineBarrier ib;
                ib.Image = img;
                if (dep == DataDependencyType::UndefinedToRenderTarget || dep == DataDependencyType::SampledToRenderTarget ||
                    dep == DataDependencyType::ComputeStorageToRenderTarget)
                    ib.LayoutAfter = img->IsDepthStencilFormat() ? TextureLayout::DepthStencilAttachment : TextureLayout::ColorAttachment;
                else if (dep == DataDependencyType::RenderTargetToComputeStorage)
                    ib.LayoutAfter = TextureLayout::General;
                else
                    ib.LayoutAfter = TextureLayout::Sample;
                if (dep == DataDependencyType::UndefinedToRenderTarget)
                    ib.LayoutBefore = TextureLayout::Undefined;
                else
                {
                    if (dep == DataDependencyType::RenderTargetToGraphics || dep == DataDependencyType::RenderTargetToCompute ||
                        dep == DataDependencyType::RenderTargetToComputeStorage)
                        ib.LayoutBefore = img->IsDepthStencilFormat() ? TextureLayout::DepthStencilAttachment : TextureLayout::ColorAttachment;
                    else if (dep == DataDependencyType::ComputeStorageToSample || dep == DataDependencyType::ComputeStorageToRenderTarget)
                        ib.LayoutBefore = TextureLayout::General;
                    else
                        ib.LayoutBefore = TextureLayout::Sample;
                }
                imageBarriers.Add(ib);
            }
            switch (dep)
            {
            case DataDependencyType::RenderTargetToGraphics:
                hw->QueuePipelineBarrier(ResourceUsage::RenderAttachmentOutput, ResourceUsage::FragmentShaderRead, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::ComputeToGraphics:
                hw->QueuePipelineBarrier(ResourceUsage::ComputeWrite, ResourceUsage::FragmentShaderRead, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::RenderTargetToCompute:
                hw->QueuePipelineBarrier(ResourceUsage::RenderAttachmentOutput, ResourceUsage::ComputeRead, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::RenderTargetToComputeStorage:
                hw->QueuePipelineBarrier(ResourceUsage::RenderAttachmentOutput, ResourceUsage::ComputeReadWrite, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::ComputeStorageToSample:
                hw->QueuePipelineBarrier(ResourceUsage::ComputeWrite, ResourceUsage::FragmentShaderRead, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::UndefinedToRenderTarget:
                hw->QueuePipelineBarrier(ResourceUsage::FragmentShaderRead, ResourceUsage::All, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::SampledToRenderTarget:
                hw->QueuePipelineBarrier(ResourceUsage::FragmentShaderRead, ResourceUsage::All, imageBarriers.GetArrayView());
                break;
            case DataDependencyType::ComputeStorageToRenderTarget:
                hw->QueuePipelineBarrier(ResourceUsage::ComputeWrite, ResourceUsage::All, imageBarriers.GetArrayView());
                break;
            }
        }
    };
}

#endif