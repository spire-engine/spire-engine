#include "HardwareRenderer.h"
#include "CoreLib/Stream.h"
#include "CoreLib/TextIO.h"

namespace GameEngine
{
namespace DummyRenderer
{
    class Buffer : public GameEngine::Buffer
	{
        CoreLib::List<unsigned char> bufferContent;
	public:
        Buffer(int sizeInBytes)
        {
            bufferContent.SetSize(sizeInBytes);
        }
		virtual void SetDataAsync(int offset, void * data, int size) override
        {
            SetData(offset, data, size);
        }
		virtual void SetData(int offset, void* data, int size) override
        {
            memcpy(bufferContent.Buffer() + offset, data, size);
        }
		virtual void SetData(void* data, int size) override
        {
            SetData(0, data, size);
        }
		virtual void GetData(void * buffer, int offset, int size) override
        {
            memcpy(buffer, bufferContent.Buffer() + offset, size);
        }
		virtual int GetSize() override { return bufferContent.Count(); }
		virtual void* Map(int /*offset*/, int /*size*/) override { return bufferContent.Buffer(); }
		virtual void* Map() override { return bufferContent.Buffer(); }
		virtual void Flush(int /*offset*/, int /*size*/) override {}
		virtual void Flush() override {}
		virtual void Unmap() override {}
	};

    class Texture2D : public virtual GameEngine::Texture2D
    {
    public:
        Texture2D() {}
    public:
        virtual void GetSize(int &/*width*/, int &/*height*/) override {}
        virtual void SetData(int /*level*/, int /*width*/, int /*height*/, int /*samples*/, DataType /*inputType*/, void * /*data*/) override {}
        virtual void SetData(int /*width*/, int /*height*/, int /*samples*/, DataType /*inputType*/, void * /*data*/) override {}
        virtual void GetData(int /*mipLevel*/, void * /*data*/, int /*bufSize*/) override {}
        virtual void BuildMipmaps() override {}
        virtual bool IsDepthStencilFormat() override { return false;  }
        virtual void* GetInternalPtr() override { return this; }
    };

    class Texture2DArray : public virtual GameEngine::Texture2DArray
    {
    public:
        Texture2DArray() {}
    public:
        virtual void GetSize(int &/*width*/, int &/*height*/, int &/*layers*/) override {}
        virtual void SetData(int /*mipLevel*/, int /*xOffset*/, int /*yOffset*/, int /*layerOffset*/, int /*width*/, int /*height*/, int /*layerCount*/, DataType /*inputType*/, void * /*data*/) override {}
        virtual void BuildMipmaps() override {}
        virtual bool IsDepthStencilFormat() override { return false;  }
        virtual void* GetInternalPtr() override { return this; }
    };

    class Texture3D : public virtual GameEngine::Texture3D
    {
    public:
        Texture3D() {}

    public:
        virtual void GetSize(int &/*width*/, int &/*height*/, int &/*depth*/) override {}
        virtual void SetData(int /*mipLevel*/, int /*xOffset*/, int /*yOffset*/, int /*zOffset*/, int /*width*/, int /*height*/, int /*depth*/, DataType /*inputType*/, void * /*data*/) override {}
        virtual bool IsDepthStencilFormat() override { return false;  }
        virtual void* GetInternalPtr() override { return this; }
    };

    class TextureCube : public virtual GameEngine::TextureCube
    {
    public:
        TextureCube() {}

    public:
        virtual void GetSize(int &/*size*/) override {}
        virtual void SetData(int /*mipLevel*/, int /*xOffset*/, int /*yOffset*/, int /*layerOffset*/, int /*width*/, int /*height*/, int /*layerCount*/, DataType /*inputType*/, void* /*data*/) override {}
        virtual bool IsDepthStencilFormat() override { return false; }
        virtual void* GetInternalPtr() override { return this; }
    };

    class TextureCubeArray : public virtual GameEngine::TextureCubeArray
    {
    public:
        TextureCubeArray() {}
    public:
        virtual void GetSize(int &/*size*/, int &/*layerCount*/) override {}
        virtual void SetData(int /*mipLevel*/, int /*xOffset*/, int /*yOffset*/, int /*layerOffset*/, int /*width*/, int /*height*/, int /*layerCount*/, DataType /*inputType*/, void* /*data*/) override {}
        virtual bool IsDepthStencilFormat() override { return false; }
        virtual void* GetInternalPtr() override { return this; }
    };

    class TextureSampler : public virtual GameEngine::TextureSampler
    {
    public:
        TextureSampler() {}

    public:
        virtual TextureFilter GetFilter() override { return TextureFilter::Linear; }
        virtual void SetFilter(TextureFilter /*filter*/) override {}
        virtual WrapMode GetWrapMode() override { return WrapMode::Clamp; }
        virtual void SetWrapMode(WrapMode /*wrap*/) override {}
        virtual CompareFunc GetCompareFunc() override { return CompareFunc::Disabled; }
        virtual void SetDepthCompare(CompareFunc /*op*/) override {}
    };

    class Shader : public virtual GameEngine::Shader
	{
	public:
		Shader() {};
	};

    class FrameBuffer : public virtual GameEngine::FrameBuffer
	{
	public:
        RenderAttachments renderAttachments;
		FrameBuffer(const RenderAttachments& attachments)
            : renderAttachments(attachments)
        {}
	public:
		virtual RenderAttachments& GetRenderAttachments() override
        {
            return renderAttachments;
        }
	};

	class Fence : public virtual GameEngine::Fence
	{
	public:
		Fence() {}
	public:
		virtual void Reset() override {}
		virtual void Wait() override {}
	};

	class RenderTargetLayout : public virtual GameEngine::RenderTargetLayout
	{
	public:
		RenderTargetLayout() {}
	public:
		virtual GameEngine::FrameBuffer* CreateFrameBuffer(const RenderAttachments& attachments) override
        {
            return new FrameBuffer(attachments);
        }
	};

    class DescriptorSetLayout : public virtual GameEngine::DescriptorSetLayout
    {
    public:
        DescriptorSetLayout() {}
    };

    class DescriptorSet : public virtual GameEngine::DescriptorSet
    {
    public:
        DescriptorSet() {}
    public:
        virtual void BeginUpdate() override {}
        virtual void Update(int /*location*/, GameEngine::Texture * /*texture*/, TextureAspect /*aspect*/) override {}
        virtual void Update(int /*location*/, CoreLib::ArrayView<GameEngine::Texture *> /*texture*/, TextureAspect /*aspect*/) override {}
        virtual void UpdateStorageImage(int /*location*/, CoreLib::ArrayView<GameEngine::Texture *> /*texture*/, TextureAspect /*aspect*/) override {}
        virtual void Update(int /*location*/, GameEngine::TextureSampler * /*sampler*/) override {}
        virtual void Update(int /*location*/, GameEngine::Buffer * /*buffer*/, int /*offset*/, int /*length*/) override {}
        virtual void EndUpdate() override {}
    };

    class Pipeline : public virtual GameEngine::Pipeline
	{
	public:
		Pipeline() {}
	};

    class PipelineBuilder : public virtual GameEngine::PipelineBuilder
	{
        CoreLib::IO::StreamWriter *writer;
        CoreLib::String pipelineName;

    public:
        PipelineBuilder(CoreLib::IO::StreamWriter *streamWriter)
            : writer(streamWriter) {}
	public:
		virtual void SetShaders(CoreLib::ArrayView<GameEngine::Shader*> /*shaders*/) override {}
		virtual void SetVertexLayout(VertexFormat /*vertexFormat*/) override {}
		virtual void SetBindingLayout(CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> /*descriptorSets*/) override {}
        virtual void SetDebugName(CoreLib::String name) override
        {
            pipelineName = name;
        }
        virtual GameEngine::Pipeline* ToPipeline(GameEngine::RenderTargetLayout* /*renderTargetLayout*/) override
        {
            writer->Write("Create GraphicsPipeline ");
            writer->Write(pipelineName);
            writer->Write("\n");
            return new Pipeline();
        }
		virtual GameEngine::Pipeline* CreateComputePipeline(CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> /*descriptorSets*/, GameEngine::Shader* /*shader*/) override
        {
            writer->Write("Create ComputePipeline ");
            writer->Write(pipelineName);
            writer->Write("\n");
            return new Pipeline();
        }
	};

    class CommandBuffer : public virtual GameEngine::CommandBuffer
	{
	public:
		CommandBuffer() {}
	public:
		virtual void BeginRecording(GameEngine::FrameBuffer* /*frameBuffer*/) override {}
		virtual void EndRecording() override {}
		virtual void SetViewport(int /*x*/, int /*y*/, int /*width*/, int /*height*/) override {}
		virtual void BindVertexBuffer(GameEngine::Buffer* /*vertexBuffer*/, int /*byteOffset*/) override {}
		virtual void BindIndexBuffer(GameEngine::Buffer* /*indexBuffer*/, int /*byteOffset*/) override {}
		virtual void BindPipeline(GameEngine::Pipeline* /*pipeline*/) override {}
		virtual void BindDescriptorSet(int /*binding*/, GameEngine::DescriptorSet* /*descSet*/) override {}
		virtual void Draw(int /*firstVertex*/, int /*vertexCount*/) override {}
		virtual void DrawInstanced(int /*numInstances*/, int /*firstVertex*/, int /*vertexCount*/) override {}
		virtual void DrawIndexed(int /*firstIndex*/, int /*indexCount*/) override {}
		virtual void DrawIndexedInstanced(int /*numInstances*/, int /*firstIndex*/, int /*indexCount*/) override {}
		virtual void DispatchCompute(int /*groupCountX*/, int /*groupCountY*/, int /*groupCountZ*/) override {}
	};

    class WindowSurface : public GameEngine::WindowSurface
    {
    public:
		WindowSurface() {}
    public:
        virtual WindowHandle GetWindowHandle() override { return WindowHandle(); }
        virtual void Resize(int /*width*/, int /*height*/) override {}
        virtual void GetSize(int & /*width*/, int & /*height*/) override {}
    };

    class HardwareRenderer : public GameEngine::HardwareRenderer
	{
	public:
        CoreLib::RefPtr<CoreLib::IO::StreamWriter> writer;
        HardwareRenderer()
        {
            writer = new CoreLib::IO::StreamWriter("rendercommands.txt");
        }
        virtual void ThreadInit(int /*threadId*/) override {}
        virtual void BeginJobSubmission() override {}
        virtual void QueueRenderPass(GameEngine::FrameBuffer * /*frameBuffer*/, bool /*clearFrameBuffer*/,
            CoreLib::ArrayView<GameEngine::CommandBuffer *> /*commands*/,
            PipelineBarriers /*barriers*/) override
        {
            writer->Write("Execute RenderPass\n");
        }
        virtual void QueueComputeTask(GameEngine::Pipeline* /*computePipeline*/, GameEngine::DescriptorSet* /*descriptorSet*/, 
            int /*x*/, int /*y*/, int /*z*/, PipelineBarriers /*barriers*/) override
        {
            writer->Write("Execute ComputeTask\n");
        }
        virtual void EndJobSubmission(GameEngine::Fence* /*fence*/) override {}
		virtual void Present(GameEngine::WindowSurface * /*surface*/, GameEngine::Texture2D* /*srcImage*/) override
        {
            writer->Write("Present\n");
        }
        virtual void Blit(GameEngine::Texture2D* /*dstImage*/, GameEngine::Texture2D* /*srcImage*/, VectorMath::Vec2i /*destOffset*/, bool /*flipSrc*/) override {}
		virtual void Wait() override {}
		virtual void SetMaxTempBufferVersions(int /*versionCount*/) override {}
		virtual void ResetTempBufferVersion(int /*version*/) override {}
		virtual GameEngine::Fence* CreateFence() override
        {
            return new Fence();
        }
		virtual GameEngine::Buffer* CreateBuffer(BufferUsage /*usage*/, int sizeInBytes, const BufferStructureInfo* /*structInfo*/) override
        {
            writer->Write("Create Buffer (");
            writer->Write(CoreLib::String(sizeInBytes));
            writer->Write(" bytes)\n");
            return new Buffer(sizeInBytes);
        }
		virtual GameEngine::Buffer* CreateMappedBuffer(BufferUsage /*usage*/, int sizeInBytes, const BufferStructureInfo* /*structInfo*/) override
        {
            writer->Write("Create Buffer (");
            writer->Write(CoreLib::String(sizeInBytes));
            writer->Write(" bytes)\n");
            return new Buffer(sizeInBytes);
        }
		virtual GameEngine::Texture2D* CreateTexture2D(CoreLib::String /*name*/, int width, int height, StorageFormat /*format*/, DataType /*type*/, void* /*data*/) override
        {
            writer->Write("Create Texture2D (");
            writer->Write(CoreLib::String(width));
            writer->Write("x");
            writer->Write(CoreLib::String(height));
            writer->Write(")\n");
            return new Texture2D();
        }
		virtual GameEngine::Texture2D* CreateTexture2D(CoreLib::String /*name*/, TextureUsage /*usage*/, int width, int height, int /*mipLevelCount*/, StorageFormat /*format*/) override
        {
            writer->Write("Create Texture2D (");
            writer->Write(CoreLib::String(width));
            writer->Write("x");
            writer->Write(CoreLib::String(height));
            writer->Write(")\n");
            return new Texture2D();
        }
		virtual GameEngine::Texture2D* CreateTexture2D(CoreLib::String /*name*/, TextureUsage /*usage*/, int width, int height, int /*mipLevelCount*/, StorageFormat /*format*/, DataType /*type*/, CoreLib::ArrayView<void*> /*mipLevelData*/) override
        {
            writer->Write("Create Texture2D (");
            writer->Write(CoreLib::String(width));
            writer->Write("x");
            writer->Write(CoreLib::String(height));
            writer->Write(")\n");
            return new Texture2D();
        }
		virtual GameEngine::Texture2DArray* CreateTexture2DArray(CoreLib::String /*name*/, TextureUsage /*usage*/, int width, int height, int layers, int /*mipLevelCount*/, StorageFormat /*format*/) override
        {
            writer->Write("Create Texture2DArray (");
            writer->Write(CoreLib::String(width));
            writer->Write("x");
            writer->Write(CoreLib::String(height));
            writer->Write("x");
            writer->Write(CoreLib::String(layers));
            writer->Write(")\n");
            return new Texture2DArray();
        }
		virtual GameEngine::TextureCube* CreateTextureCube(CoreLib::String /*name*/, TextureUsage /*usage*/, int size, int /*mipLevelCount*/, StorageFormat /*format*/) override
        {
            writer->Write("Create TextureCube (");
            writer->Write(CoreLib::String(size));
            writer->Write(")\n");
            return new TextureCube();
        }
		virtual GameEngine::TextureCubeArray* CreateTextureCubeArray(CoreLib::String /*name*/, TextureUsage /*usage*/, int size, int /*mipLevelCount*/, int cubemapCount, StorageFormat /*format*/) override
        {
            writer->Write("Create TextureCubeArray (");
            writer->Write(CoreLib::String(size));
            writer->Write("x");
            writer->Write(CoreLib::String(cubemapCount));
            writer->Write(")\n");
            return new TextureCubeArray();
        }
		virtual GameEngine::Texture3D* CreateTexture3D(CoreLib::String /*name*/, TextureUsage /*usage*/, int width, int height, int depth, int /*mipLevelCount*/, StorageFormat /*format*/) override
        {
            writer->Write("Create Texture3D (");
            writer->Write(CoreLib::String(width));
            writer->Write("x");
            writer->Write(CoreLib::String(height));
            writer->Write("x");
            writer->Write(CoreLib::String(depth));
            writer->Write(")\n");
            return new Texture3D();
        }
		virtual GameEngine::TextureSampler* CreateTextureSampler() override
        {
            writer->Write("Create TextureSampler\n");
            return new TextureSampler();
        }
		virtual GameEngine::Shader* CreateShader(ShaderType /*stage*/, const char* /*data*/, int /*size*/) override
        {
            writer->Write("Create Shader\n");
            return new Shader();
        }
		virtual GameEngine::RenderTargetLayout* CreateRenderTargetLayout(CoreLib::ArrayView<AttachmentLayout> /*bindings*/, bool /*ignoreInitialContent*/) override
        {
            writer->Write("Create RenderTargetLayout\n");
            return new RenderTargetLayout();
        }
		virtual GameEngine::PipelineBuilder* CreatePipelineBuilder() override
        {
            return new PipelineBuilder(writer.Ptr());
        }
		virtual GameEngine::DescriptorSetLayout* CreateDescriptorSetLayout(CoreLib::ArrayView<DescriptorLayout> /*descriptors*/) override
        {
            writer->Write("Create DescriptorSetLayout\n");
            return new DescriptorSetLayout();
        }
		virtual GameEngine::DescriptorSet* CreateDescriptorSet(GameEngine::DescriptorSetLayout* /*layout*/) override
        {
            writer->Write("Create DescriptorSet\n");
            return new DescriptorSet();
        }
		virtual GameEngine::CommandBuffer* CreateCommandBuffer() override
        {
            writer->Write("Create CommandBuffer\n");
            return new CommandBuffer();
        }
		virtual TargetShadingLanguage GetShadingLanguage() override { return TargetShadingLanguage::SPIRV; }
		virtual int UniformBufferAlignment() override { return 16; }
		virtual int StorageBufferAlignment() override { return 16; }
        virtual GameEngine::WindowSurface * CreateSurface(WindowHandle /*windowHandle*/, int /*width*/, int /*height*/) override
        {
            writer->Write("Create Surface\n");
            return new WindowSurface();
        }
		virtual CoreLib::String GetRendererName() override
        {
            return "Dummy Renderer";
        }
	};
}
}

namespace GameEngine
{
    HardwareRenderer* CreateDummyHardwareRenderer()
    {
        return new DummyRenderer::HardwareRenderer();
    }
}