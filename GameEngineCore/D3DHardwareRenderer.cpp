#include "HardwareRenderer.h"

#if __has_include(<d3d12.h>)

#include "CoreLib/Stream.h"
#include "CoreLib/TextIO.h"

#include <mutex>
#include <thread>
#include <d3d12.h>
#include <dxgi1_4.h>

#define CHECK_DX(x) CORELIB_ASSERT(SUCCEEDED(x) && "Direct3D call error check");

namespace GameEngine
{
    namespace D3DRenderer
    {
        typedef HRESULT (__stdcall *PFN_CREATE_DXGI_FACTORY1)(REFIID, _COM_Outptr_ void**);

        thread_local int renderThreadId = 0;
        static constexpr int MaxRenderThreads = 8;
        static constexpr int D3DConstantBufferAlignment = 256;
        static constexpr int D3DStorageBufferAlignment = 256;
        // 64MB staging buffer per frame version
        static constexpr int StagingBufferSize = 1 << 26;
        // Max data size allowed to use the shared staging buffer for CPU-GPU upload.
        static constexpr int SharedStagingBufferDataSizeThreshold = 1 << 20;

        int AlignBufferSize(int size, int alignment)
        {
            return (size + alignment - 1) / alignment * alignment;
        }

        class RendererState
        {
        public:
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* queue = nullptr;
            ID3D12Heap* heap = nullptr;
            int version = 0;
            CoreLib::List<ID3D12Resource*> stagingBuffers;
            CoreLib::List<long> stagingBufferAllocPtr;
            std::mutex stagingBufferMutex;
            CoreLib::List<CoreLib::List<ID3D12GraphicsCommandList*>> tempCommandLists;
            CoreLib::List<long> tempCommandListAllocPtr;
            CoreLib::List<ID3D12CommandAllocator*> commandAllocators;
            std::mutex tempCommandListMutex;
            ID3D12CommandAllocator* largeCopyCmdListAllocator;
            std::mutex largeCopyCmdListMutex;

            CoreLib::String deviceName;
            CoreLib::String cacheLocation;
            int rendererCount = 0;
            size_t videoMemorySize = 0;
            uint64_t waitFenceValue = 1;
            ID3D12Fence* waitFences[MaxRenderThreads] = {};
            HANDLE waitEvents[MaxRenderThreads] = {};
            ID3D12GraphicsCommandList* GetTempCommandList()
            {
                long cmdListId = 0;
                {
                    std::lock_guard<std::mutex> lock(tempCommandListMutex);
                    auto& allocPtr = tempCommandListAllocPtr[version];
                    if (allocPtr == tempCommandLists[version].Count())
                    {
                        ID3D12GraphicsCommandList* commandList;
                        CHECK_DX(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[version],
                            nullptr, IID_PPV_ARGS(&commandList)));
                        commandList->Close();
                        tempCommandLists[version].Add(commandList);
                    }
                    cmdListId = allocPtr;
                    allocPtr++;
                }
                auto cmdList = tempCommandLists[version][cmdListId];
                cmdList->Reset(commandAllocators[version], nullptr);
                return cmdList;
            }
            ID3D12Resource* CreateBufferResource(size_t sizeInBytes, D3D12_HEAP_TYPE heapType, D3D12_CPU_PAGE_PROPERTY cpuPageProperty, bool allowUnorderedAccess)
            {
                D3D12_HEAP_PROPERTIES heapProperties = {};
                heapProperties.Type = heapType;
                heapProperties.CreationNodeMask = heapProperties.VisibleNodeMask = 1;
                if (heapType == D3D12_HEAP_TYPE_CUSTOM)
                {
                    heapProperties.CPUPageProperty = cpuPageProperty;
                    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
                }
                D3D12_RESOURCE_DESC resourceDesc = {};
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                resourceDesc.Width = CoreLib::Math::Max((size_t)1, sizeInBytes);
                resourceDesc.Height = 1;
                resourceDesc.DepthOrArraySize = 1;
                resourceDesc.MipLevels = 1;
                resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
                resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                resourceDesc.SampleDesc.Count = 1;
                resourceDesc.SampleDesc.Quality = 0;
                if (allowUnorderedAccess)
                {
                    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                }
                ID3D12Resource* resource;
                D3D12_RESOURCE_STATES resState = D3D12_RESOURCE_STATE_COMMON;
                if (heapType == D3D12_HEAP_TYPE_UPLOAD)
                    resState = D3D12_RESOURCE_STATE_GENERIC_READ;
                else if (heapType == D3D12_HEAP_TYPE_UPLOAD)
                    resState = D3D12_RESOURCE_STATE_COPY_DEST;
                CHECK_DX(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                    &resourceDesc, resState, nullptr, IID_PPV_ARGS(&resource)));
                return resource;
            }
            void Wait()
            {
                auto value = InterlockedIncrement(&waitFenceValue);
                if (waitFences[renderThreadId] == nullptr)
                {
                    CHECK_DX(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&waitFences[renderThreadId])));
                    waitEvents[renderThreadId] = CreateEventW(nullptr, 0, 0, nullptr);
                }
                CHECK_DX(queue->Signal(waitFences[renderThreadId], value));
                CHECK_DX(waitFences[renderThreadId]->SetEventOnCompletion(value, waitEvents[renderThreadId]));
                WaitForSingleObject(waitEvents[renderThreadId], INFINITE);
            }
            static void Free()
            {
                auto& state = Get();
                for (int i = 0; i < MaxRenderThreads; i++)
                {
                    if (state.waitFences[i])
                    {
                        state.waitFences[i]->Release();
                        CloseHandle(state.waitEvents[i]);
                    }
                }
                for (auto& cmdLists : state.tempCommandLists)
                    for (auto cmdList : cmdLists)
                        cmdList->Release();
                for (auto cmdAllocator : state.commandAllocators)
                    cmdAllocator->Release();
                state.largeCopyCmdListAllocator->Release();
                for (auto stagingBuffer : state.stagingBuffers)
                    stagingBuffer->Release();
                if (state.queue)
                    state.queue->Release();
                if (state.device)
                    state.device->Release();
                state.tempCommandListAllocPtr = decltype(state.tempCommandListAllocPtr)();
                state.commandAllocators = decltype(state.commandAllocators)();
                state.stagingBuffers = decltype(state.stagingBuffers)();
                state.stagingBufferAllocPtr = decltype(state.stagingBufferAllocPtr)();
                state.tempCommandLists = decltype(state.tempCommandLists)();
                state.cacheLocation = CoreLib::String();
                state.deviceName = CoreLib::String();
            }
            static RendererState& Get()
            {
                static RendererState state;
                return state;
            }
        };

        class Buffer : public GameEngine::Buffer
        {
        private:
            int bufferSize;
            ID3D12Resource* resource;
            bool isMappable = false;
            int mappedStart = -1;
            int mappedEnd = -1;
        public:
            Buffer(BufferUsage usage, int sizeInBytes, bool mappable)
            {
                auto& state = RendererState::Get();
                this->isMappable = mappable;
                resource = state.CreateBufferResource(sizeInBytes,
                    mappable ? D3D12_HEAP_TYPE_CUSTOM : D3D12_HEAP_TYPE_DEFAULT,
                    mappable ? D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE : D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                    usage == BufferUsage::StorageBuffer);
                bufferSize = sizeInBytes;
            }
            ~Buffer()
            {
                resource->Release();
            }

            // SetDataImpl: returns true if succeeded without GPU synchronization.
            bool SetDataImpl(int offset, void* data, int size)
            {
                auto& state = RendererState::Get();
                if (size == 0)
                    return true;
                if (isMappable)
                {
                    D3D12_RANGE range;
                    range.Begin = (SIZE_T)offset;
                    range.End = (SIZE_T)size;
                    void* mappedPtr = nullptr;
                    CHECK_DX(resource->Map(0, &range, &mappedPtr));
                    memcpy(mappedPtr, data, size);
                    resource->Unmap(0, &range);
                    return true;
                }

                if (size < SharedStagingBufferDataSizeThreshold)
                {
                    // Use shared staging buffer for async upload
                    std::lock_guard<std::mutex> lockGuard(state.stagingBufferMutex);
                    auto stagingOffset = InterlockedAdd(&(state.stagingBufferAllocPtr[state.version]), size);
                    if (stagingOffset + size <= StagingBufferSize)
                    {
                        // Map and copy to staging buffer.
                        D3D12_RANGE range;
                        range.Begin = (SIZE_T)stagingOffset;
                        range.End = (SIZE_T)size;
                        void* mappedStagingPtr = nullptr;
                        CHECK_DX(state.stagingBuffers[state.version]->Map(0, &range, &mappedStagingPtr));
                        memcpy(mappedStagingPtr, data, size);
                        state.stagingBuffers[state.version]->Unmap(0, &range);
                        // Submit a copy command to GPU.
                        auto copyCommandList = state.GetTempCommandList();
                        copyCommandList->CopyBufferRegion(resource, offset, state.stagingBuffers[state.version], stagingOffset, size);
                        CHECK_DX(copyCommandList->Close());
                        ID3D12CommandList* cmdList = copyCommandList;
                        state.queue->ExecuteCommandLists(1, &cmdList);
                        return true;
                    }
                }

                // Data is too large to use the shared staging buffer, or the staging buffer
                // is already full.
                // Create a dedicated staging buffer and perform uploading right here.
                std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
                ID3D12Resource* stagingResource = state.CreateBufferResource(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
                D3D12_RANGE mapRange;
                mapRange.Begin = 0;
                mapRange.End = size;
                void* stagingBufferPtr;
                CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
                memcpy(stagingBufferPtr, data, size);
                ID3D12GraphicsCommandList* copyCmdList;
                CHECK_DX(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, state.largeCopyCmdListAllocator, nullptr,
                    IID_PPV_ARGS(&copyCmdList)));
                copyCmdList->CopyBufferRegion(resource, offset, stagingResource, 0, size);
                CHECK_DX(copyCmdList->Close());
                ID3D12CommandList* cmdList = copyCmdList;
                state.queue->ExecuteCommandLists(1, &cmdList);
                state.Wait();
                copyCmdList->Release();
                stagingResource->Release();
                state.largeCopyCmdListAllocator->Reset();
                return false;
            }
            virtual void SetDataAsync(int offset, void* data, int size) override
            {
                SetDataImpl(offset, data, size);
            }
            virtual void SetData(int offset, void* data, int size) override
            {
                if (!SetDataImpl(offset, data, size))
                    RendererState::Get().Wait();
            }
            virtual void SetData(void* data, int size) override
            {
                SetData(0, data, size);
            }
            virtual void GetData(void* buffer, int offset, int size) override
            {
                auto& state = RendererState::Get();
                if (size == 0)
                    return;
                std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
                ID3D12Resource* stagingResource = state.CreateBufferResource(size, D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,  false);
                ID3D12GraphicsCommandList* copyCmdList;
                CHECK_DX(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, state.largeCopyCmdListAllocator, nullptr,
                    IID_PPV_ARGS(&copyCmdList)));
                copyCmdList->CopyBufferRegion(stagingResource, 0, resource, offset, size);
                CHECK_DX(copyCmdList->Close());
                ID3D12CommandList* cmdList = copyCmdList;
                state.queue->ExecuteCommandLists(1, &cmdList);
                state.Wait();
                copyCmdList->Release();
                state.largeCopyCmdListAllocator->Reset();
                D3D12_RANGE mapRange;
                mapRange.Begin = 0;
                mapRange.End = size;
                void* stagingBufferPtr;
                CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
                memcpy(buffer, stagingBufferPtr, size);
                mapRange.End = 0;
                stagingResource->Unmap(0, &mapRange);
                stagingResource->Release();
            }
            virtual int GetSize() override
            {
                return bufferSize;
            }
            virtual void* Map(int offset, int size) override
            {
                CORELIB_ASSERT(isMappable);
                CORELIB_ASSERT(mappedStart == -1 && "Nested calls to Buffer::Map() not allowed.");
                D3D12_RANGE range;
                range.Begin = (SIZE_T)offset;
                range.End = (SIZE_T)size;
                void* mappedPtr = nullptr;
                CHECK_DX(resource->Map(0, &range, &mappedPtr));
                mappedStart = offset;
                mappedEnd = offset + size;
                return mappedPtr;
            }
            virtual void* Map() override
            {
                return Map(0, bufferSize);
            }
            virtual void Flush(int offset, int size) override
            {
                CORELIB_ASSERT(isMappable);
                CORELIB_ASSERT(mappedStart == -1);
                D3D12_RANGE range;
                range.Begin = (SIZE_T)offset;
                range.End = (SIZE_T)size;
                void* mappedPtr = nullptr;
                CHECK_DX(resource->Map(0, &range, &mappedPtr));
                resource->Unmap(0, &range);
            }
            virtual void Flush() override
            {
                Flush(0, bufferSize);
            }
            virtual void Unmap() override
            {
                CORELIB_ASSERT(mappedStart != -1);
                D3D12_RANGE range;
                range.Begin = (SIZE_T)mappedStart;
                range.End = (SIZE_T)mappedEnd;
                resource->Unmap(0, &range);
            }
        };

        class Texture : public GameEngine::Texture
        {
        public:
            Texture() {}
        public:
            virtual void SetCurrentLayout(TextureLayout /*layout*/) override {}
            virtual bool IsDepthStencilFormat() override { return false; }
        };

        class Texture2D : public virtual GameEngine::Texture2D
        {
        public:
            Texture2D() {}
        public:
            virtual void GetSize(int&/*width*/, int&/*height*/) override {}
            virtual void SetData(int /*level*/, int /*width*/, int /*height*/, int /*samples*/, DataType /*inputType*/, void* /*data*/) override {}
            virtual void SetData(int /*width*/, int /*height*/, int /*samples*/, DataType /*inputType*/, void* /*data*/) override {}
            virtual void GetData(int /*mipLevel*/, void* /*data*/, int /*bufSize*/) override {}
            virtual void BuildMipmaps() override {}
            virtual void SetCurrentLayout(TextureLayout /*layout*/) override {}
            virtual bool IsDepthStencilFormat() override { return false; }
        };

        class Texture2DArray : public virtual GameEngine::Texture2DArray
        {
        public:
            Texture2DArray() {}
        public:
            virtual void GetSize(int&/*width*/, int&/*height*/, int&/*layers*/) override {}
            virtual void SetData(int /*mipLevel*/, int /*xOffset*/, int /*yOffset*/, int /*layerOffset*/, int /*width*/, int /*height*/, int /*layerCount*/, DataType /*inputType*/, void* /*data*/) override {}
            virtual void BuildMipmaps() override {}
            virtual void SetCurrentLayout(TextureLayout /*layout*/) override {}
            virtual bool IsDepthStencilFormat() override { return false; }
        };

        class Texture3D : public virtual GameEngine::Texture3D
        {
        public:
            Texture3D() {}

        public:
            virtual void GetSize(int&/*width*/, int&/*height*/, int&/*depth*/) override {}
            virtual void SetData(int /*mipLevel*/, int /*xOffset*/, int /*yOffset*/, int /*zOffset*/, int /*width*/, int /*height*/, int /*depth*/, DataType /*inputType*/, void* /*data*/) override {}
            virtual void SetCurrentLayout(TextureLayout /*layout*/) override {}
            virtual bool IsDepthStencilFormat() override { return false; }
        };

        class TextureCube : public virtual GameEngine::TextureCube
        {
        public:
            TextureCube() {}

        public:
            virtual void GetSize(int&/*size*/) override {}
            virtual void SetCurrentLayout(TextureLayout /*layout*/) override {}
            virtual bool IsDepthStencilFormat() override { return false; }
        };

        class TextureCubeArray : public virtual GameEngine::TextureCubeArray
        {
        public:
            TextureCubeArray() {}
        public:
            virtual void GetSize(int&/*size*/, int&/*layerCount*/) override {}
            virtual void SetCurrentLayout(TextureLayout /*layout*/) override {}
            virtual bool IsDepthStencilFormat() override { return false; }
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
            ID3D12Fence* fence = nullptr;
            HANDLE waitEvent;
            Fence()
            {
                CHECK_DX(RendererState::Get().device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
                waitEvent = CreateEventW(nullptr, 1, 0, nullptr);
            }
            ~Fence()
            {
                fence->Release();
                CloseHandle(waitEvent);
            }
        public:
            virtual void Reset() override
            {
                ResetEvent(waitEvent);
            }
            virtual void Wait() override
            {
                WaitForSingleObject(waitEvent, INFINITE);
            }
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
            virtual void Update(int /*location*/, GameEngine::Texture* /*texture*/, TextureAspect /*aspect*/) override {}
            virtual void Update(int /*location*/, CoreLib::ArrayView<GameEngine::Texture*> /*texture*/, TextureAspect /*aspect*/, TextureLayout /*layout*/) override {}
            virtual void UpdateStorageImage(int /*location*/, CoreLib::ArrayView<GameEngine::Texture*> /*texture*/, TextureAspect /*aspect*/) override {}
            virtual void Update(int /*location*/, GameEngine::TextureSampler* /*sampler*/) override {}
            virtual void Update(int /*location*/, GameEngine::Buffer* /*buffer*/, int /*offset*/, int /*length*/) override {}
            virtual void EndUpdate() override {}
        };

        class Pipeline : public virtual GameEngine::Pipeline
        {
        public:
            Pipeline() {}
        };

        class PipelineBuilder : public virtual GameEngine::PipelineBuilder
        {
        private:
            CoreLib::String pipelineName;

        public:
            PipelineBuilder() {}
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
                return new Pipeline();
            }
            virtual GameEngine::Pipeline* CreateComputePipeline(CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> /*descriptorSets*/, GameEngine::Shader* /*shader*/) override
            {
                return new Pipeline();
            }
        };

        class CommandBuffer : public virtual GameEngine::CommandBuffer
        {
        public:
            CommandBuffer() {}
        public:
            virtual void BeginRecording() override {}
            virtual void BeginRecording(GameEngine::FrameBuffer* /*frameBuffer*/) override {}
            virtual void BeginRecording(GameEngine::RenderTargetLayout* /*renderTargetLayout*/) override {}
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
            virtual void TransferLayout(CoreLib::ArrayView<GameEngine::Texture*> /*attachments*/, TextureLayoutTransfer /*transferDirection*/) override {}
            virtual void Blit(GameEngine::Texture2D* /*dstImage*/, GameEngine::Texture2D* /*srcImage*/, TextureLayout /*srcLayout*/, VectorMath::Vec2i /*destOffset*/, bool /*flipSrc*/) override {}
            virtual void ClearAttachments(GameEngine::FrameBuffer* /*frameBuffer*/) override {}
            virtual void MemoryAccessBarrier(MemoryBarrierType /*barrierType*/) override {}
        };

        class WindowSurface : public GameEngine::WindowSurface
        {
        public:
            WindowSurface() {}
        public:
            virtual WindowHandle GetWindowHandle() override { return WindowHandle(); }
            virtual void Resize(int /*width*/, int /*height*/) override {}
            virtual void GetSize(int& /*width*/, int& /*height*/) override {}
        };

        class HardwareRenderer : public GameEngine::HardwareRenderer
        {
        private:
        public:
            HardwareRenderer(int gpuId, bool useSoftwareDevice, CoreLib::String cacheLocation)
            {
                auto& state = RendererState::Get();
                state.cacheLocation = cacheLocation;
                if (state.rendererCount == 0)
                {
                    HMODULE dx12module = LoadLibraryEx(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                    if (!dx12module)
                    {
                        throw HardwareRendererException("cannot load d3d12.dll");
                    }
                    PFN_D3D12_CREATE_DEVICE createDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(dx12module, "D3D12CreateDevice");
                    if (!createDevice)
                    {
                        throw HardwareRendererException("cannot load d3d12.dll");
                    }
                    HMODULE dxgimodule = LoadLibrary(L"dxgi.dll");
                    if (!dxgimodule)
                    {
                        throw HardwareRendererException("cannot load dxgi.dll");
                    }
                    auto createDXGIFactory1 = (PFN_CREATE_DXGI_FACTORY1)GetProcAddress(dxgimodule, "CreateDXGIFactory1");
                    if (!createDXGIFactory1)
                    {
                        throw HardwareRendererException("cannot load dxgi.dll");
                    }
                    // Initialize D3D Context

                    IDXGIFactory4* dxgiFactory;
                    CHECK_DX(createDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
                    CoreLib::List<IDXGIAdapter1*> adapters;
                    if (useSoftwareDevice)
                    {
                        IDXGIAdapter1* adapter = nullptr;
                        CHECK_DX(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
                        adapters.Add(adapter);
                    }
                    else
                    {
                        IDXGIAdapter1* adapter = nullptr;
                        for (unsigned i = 0;; i++)
                        {
                            if (DXGI_ERROR_NOT_FOUND == dxgiFactory->EnumAdapters1(i, &adapter))
                            {
                                // No more adapters to enumerate.
                                break;
                            }
                            // Check to see if the adapter supports Direct3D 12, but don't create the
                            // actual device yet.
                            if (SUCCEEDED(createDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                            {
                                adapters.Add(adapter);
                                continue;
                            }
                            adapter->Release();
                        }
                    }
                    gpuId = CoreLib::Math::Min(adapters.Count() - 1, gpuId);
                    if (gpuId < 0)
                        CORELIB_ABORT("No D3D12-compatible GPU found.");

#if defined(_DEBUG)
                    // Enable the debug layer.
                    auto d3d12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)(GetProcAddress(dx12module, "D3D12GetDebugInterface"));
                    if (d3d12GetDebugInterface)
                    {
                        ID3D12Debug* debugController;
                        if (SUCCEEDED(d3d12GetDebugInterface(IID_PPV_ARGS(&debugController))))
                        {
                            debugController->EnableDebugLayer();
                            debugController->Release();
                        }
                    }
#endif
                    CHECK_DX(createDevice(adapters[gpuId], D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state.device)));

                    // Read adapter info.
                    DXGI_ADAPTER_DESC adapterDesc;
                    adapters[gpuId]->GetDesc(&adapterDesc);
                    state.deviceName = CoreLib::String::FromWString(adapterDesc.Description);
                    state.videoMemorySize = adapterDesc.DedicatedVideoMemory;
                    for (auto& adapter : adapters)
                    {
                        adapter->Release();
                    }

                    // Create CommandQueue
                    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                    CHECK_DX(state.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&state.queue)));

                    // Create Copy command list allocator
                    CHECK_DX(state.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                        IID_PPV_ARGS(&state.largeCopyCmdListAllocator)));
                }
                state.rendererCount++;
            }
            ~HardwareRenderer()
            {
                auto& state = RendererState::Get();
                state.rendererCount--;
                if (state.rendererCount == 0)
                {
                    RendererState::Free();
                }
            }
            virtual void ThreadInit(int threadId) override
            {
                CORELIB_ASSERT(threadId < MaxRenderThreads);
                renderThreadId = threadId;
            }
            virtual void ClearTexture(GameEngine::Texture2D* /*texture*/) override {}
            virtual void BeginJobSubmission() override {}
            virtual void QueueRenderPass(GameEngine::FrameBuffer* /*frameBuffer*/, CoreLib::ArrayView<GameEngine::CommandBuffer*> /*commands*/) override
            {
            }
            virtual void QueueNonRenderCommandBuffers(CoreLib::ArrayView<GameEngine::CommandBuffer*> /*commands*/) override
            {
            }
            virtual void QueueComputeTask(GameEngine::Pipeline* /*computePipeline*/, GameEngine::DescriptorSet* /*descriptorSet*/, int /*x*/, int /*y*/, int /*z*/) override
            {
            }
            virtual void QueuePipelineBarrier(ResourceUsage /*usageBefore*/, ResourceUsage /*usageAfter*/, CoreLib::ArrayView<ImagePipelineBarrier> /*barriers*/) override
            {
            }
            virtual void QueuePipelineBarrier(ResourceUsage /*usageBefore*/, ResourceUsage /*usageAfter*/, CoreLib::ArrayView<GameEngine::Buffer*> /*buffers*/) override
            {
            }
            virtual void EndJobSubmission(GameEngine::Fence* fence) override
            {
                auto& state = RendererState::Get();

                if (fence)
                {
                    auto d3dfence = dynamic_cast<D3DRenderer::Fence*>(fence);
                    auto value = InterlockedIncrement(&state.waitFenceValue);
                    state.queue->Signal(d3dfence->fence, value);
                    d3dfence->fence->SetEventOnCompletion(value, d3dfence->waitEvent);
                }
            }
            virtual void Present(GameEngine::WindowSurface* /*surface*/, GameEngine::Texture2D* /*srcImage*/) override
            {
            }
            virtual void Blit(GameEngine::Texture2D* /*dstImage*/, GameEngine::Texture2D* /*srcImage*/, VectorMath::Vec2i /*destOffset*/) override {}
            virtual void Wait() override
            {
                RendererState::Get().Wait();
            }
            virtual void SetMaxTempBufferVersions(int versionCount) override
            {
                auto& state = RendererState::Get();
                CORELIB_ASSERT(state.stagingBuffers.Count() == 0);
                state.stagingBuffers.SetSize(versionCount);
                state.stagingBufferAllocPtr.SetSize(versionCount);
                state.commandAllocators.SetSize(versionCount);
                for (int i = 0; i < versionCount; i++)
                {
                    state.stagingBuffers[i] = state.CreateBufferResource(StagingBufferSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
                    CHECK_DX(state.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&state.commandAllocators[i])));
                }
                state.tempCommandLists.SetSize(versionCount);
                state.tempCommandListAllocPtr.SetSize(versionCount);
                for (int i = 0; i < versionCount; i++)
                {
                    state.tempCommandListAllocPtr[i] = 0;
                    state.stagingBufferAllocPtr[i] = 0;
                }
            }
            virtual void ResetTempBufferVersion(int version) override
            {
                auto& state = RendererState::Get();
                state.version = version;
                state.stagingBufferAllocPtr[state.version] = 0;
                state.tempCommandListAllocPtr[state.version] = 0;
            }
            virtual GameEngine::Fence* CreateFence() override
            {
                return new Fence();
            }
            virtual GameEngine::Buffer* CreateBuffer(BufferUsage usage, int sizeInBytes) override
            {
                return new Buffer(usage, sizeInBytes, false);
            }
            virtual GameEngine::Buffer* CreateMappedBuffer(BufferUsage usage, int sizeInBytes) override
            {
                return new Buffer(usage, sizeInBytes, true);
            }
            virtual GameEngine::Texture2D* CreateTexture2D(int /*width*/, int /*height*/, StorageFormat /*format*/, DataType /*type*/, void* /*data*/) override
            {
                return new Texture2D();
            }
            virtual GameEngine::Texture2D* CreateTexture2D(TextureUsage /*usage*/, int /*width*/, int /*height*/, int /*mipLevelCount*/, StorageFormat /*format*/) override
            {
                return new Texture2D();
            }
            virtual GameEngine::Texture2D* CreateTexture2D(TextureUsage /*usage*/, int /*width*/, int /*height*/, int /*mipLevelCount*/, StorageFormat /*format*/, DataType /*type*/, CoreLib::ArrayView<void*> /*mipLevelData*/) override
            {
                return new Texture2D();
            }
            virtual GameEngine::Texture2DArray* CreateTexture2DArray(TextureUsage /*usage*/, int /*width*/, int /*height*/, int /*layers*/, int /*mipLevelCount*/, StorageFormat /*format*/) override
            {
                return new Texture2DArray();
            }
            virtual GameEngine::TextureCube* CreateTextureCube(TextureUsage /*usage*/, int /*size*/, int /*mipLevelCount*/, StorageFormat /*format*/) override
            {
                return new TextureCube();
            }
            virtual GameEngine::TextureCubeArray* CreateTextureCubeArray(TextureUsage /*usage*/, int /*size*/, int /*mipLevelCount*/, int /*cubemapCount*/, StorageFormat /*format*/) override
            {
                return new TextureCubeArray();
            }
            virtual GameEngine::Texture3D* CreateTexture3D(TextureUsage /*usage*/, int /*width*/, int /*height*/, int /*depth*/, int /*mipLevelCount*/, StorageFormat /*format*/) override
            {
                return new Texture3D();
            }
            virtual GameEngine::TextureSampler* CreateTextureSampler() override
            {
                return new TextureSampler();
            }
            virtual GameEngine::Shader* CreateShader(ShaderType /*stage*/, const char* /*data*/, int /*size*/) override
            {
                return new Shader();
            }
            virtual GameEngine::RenderTargetLayout* CreateRenderTargetLayout(CoreLib::ArrayView<AttachmentLayout> /*bindings*/, bool /*ignoreInitialContent*/) override
            {
                return new RenderTargetLayout();
            }
            virtual GameEngine::PipelineBuilder* CreatePipelineBuilder() override
            {
                return new PipelineBuilder();
            }
            virtual GameEngine::DescriptorSetLayout* CreateDescriptorSetLayout(CoreLib::ArrayView<DescriptorLayout> /*descriptors*/) override
            {
                return new DescriptorSetLayout();
            }
            virtual GameEngine::DescriptorSet* CreateDescriptorSet(GameEngine::DescriptorSetLayout* /*layout*/) override
            {
                return new DescriptorSet();
            }
            virtual int GetDescriptorPoolCount() override
            {
                return 1;
            }
            virtual GameEngine::CommandBuffer* CreateCommandBuffer() override
            {
                return new CommandBuffer();
            }
            virtual TargetShadingLanguage GetShadingLanguage() override { return TargetShadingLanguage::HLSL; }
            virtual int UniformBufferAlignment() override { return D3DConstantBufferAlignment; }
            virtual int StorageBufferAlignment() override { return D3DStorageBufferAlignment; }
            virtual GameEngine::WindowSurface* CreateSurface(WindowHandle /*windowHandle*/, int /*width*/, int /*height*/) override
            {
                return new WindowSurface();
            }
            virtual CoreLib::String GetRendererName() override
            {
                return RendererState::Get().deviceName;
            }
            virtual void TransferBarrier(int /*barrierId*/) override {}
        };
    }
}

namespace GameEngine
{
    HardwareRenderer* CreateD3DHardwareRenderer(int gpuId, bool useSoftwareRenderer, CoreLib::String cachePath)
    {
        return new D3DRenderer::HardwareRenderer(gpuId, useSoftwareRenderer, cachePath);
    }
}

#else

namespace GameEngine
{
    HardwareRenderer* CreateD3DHardwareRenderer(int /*gpuId*/, bool /*useSoftwareRenderer*/, CoreLib::String /*cachePath*/)
    {
        throw HardwareRendererException("Direct3D 12 is not available on this platform.");
    }
}

#endif