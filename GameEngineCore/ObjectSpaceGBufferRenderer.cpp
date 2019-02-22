#include "RenderContext.h"
#include "ViewResource.h"
#include "RenderProcedure.h"
#include "Actor.h"
#include "Material.h"
using namespace CoreLib;
namespace GameEngine
{
    class IRenderProcedure;
    class Renderer;

    class ObjectSpaceGBufferRenderer : public CoreLib::RefObject
    {
    private:
        HardwareRenderer * hwRenderer;
        ShaderSet shaders;
        RendererService * rendererService;
    public:
        ObjectSpaceGBufferRenderer(HardwareRenderer * hw, RendererService * service, CoreLib::String shaderFileName)
        {
            ShaderCompilationResult crs;
            shaders = CompileGraphicsShader(crs, hwRenderer, shaderFileName);
            rendererService = service;
        }
        void RenderObjectSpaceMap(ArrayView<Texture2D*> dest, ArrayView<StorageFormat> attachmentFormats, Actor * actor)
        {
            List<AttachmentLayout> attachments;
            RenderAttachments renderAttachments;
            for (int i = 0; i < attachmentFormats.Count(); i++)
            {
                AttachmentLayout layout;
                layout.ImageFormat = attachmentFormats[i];
                layout.Usage = TextureUsage::ColorAttachment;
                attachments.Add(layout);
                renderAttachments.SetAttachment(i, dest[i]);
            }
            RefPtr<RenderTargetLayout> renderTargetLayout = hwRenderer->CreateRenderTargetLayout(attachments.GetArrayView());
            RefPtr<FrameBuffer> frameBuffer = renderTargetLayout->CreateFrameBuffer(renderAttachments);
            
            DrawableSink sink;
            RefPtr<PipelineBuilder> pipelineBuilder = hwRenderer->CreatePipelineBuilder();
            GetDrawablesParameter param;
            param.sink = &sink;
            param.rendererService = rendererService;
            param.IsEditorMode = false;
            param.CameraDir = VectorMath::Vec3::Create(0.0f, 0.0f, -1.0f);
            param.CameraPos = actor->GetPosition();
            param.UseSkeleton = false;
            actor->GetDrawables(param);
            Array<Shader*, 2> shaderArray;
            shaderArray.Add(shaders.vertexShader.Ptr());
            shaderArray.Add(shaders.fragmentShader.Ptr());
            DescriptorLayout descriptors[] =
            {
                DescriptorLayout(StageFlags::sfGraphics, 0, BindingType::UniformBuffer),
                DescriptorLayout(StageFlags::sfGraphics, 1, BindingType::Sampler),
            };
            RefPtr<DescriptorSetLayout> descSetLayout = hwRenderer->CreateDescriptorSetLayout(MakeArrayView(descriptors, sizeof(descriptors)/sizeof(DescriptorLayout)));
            
            for (auto & drawable : sink.GetDrawables(false))
            {
                pipelineBuilder->SetVertexLayout(drawable->GetMesh()->vertexFormat);
                pipelineBuilder->SetShaders(shaderArray.GetArrayView());
                pipelineBuilder->FixedFunctionStates.BlendMode = BlendMode::Replace;
                pipelineBuilder->FixedFunctionStates.DepthCompareFunc = CompareFunc::Always;
                pipelineBuilder->FixedFunctionStates.PrimitiveTopology = PrimitiveType::Triangles;
                drawable->GetMaterial()->IsDoubleSided
                pipelineBuilder->SetBindingLayout(MakeArrayView(descriptorSetLayout.Ptr()));
            }
        }
    };
}