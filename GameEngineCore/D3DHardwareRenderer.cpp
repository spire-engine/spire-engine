#include "HardwareRenderer.h"

#if __has_include(<d3d12.h>)

#include "CoreLib/Stream.h"
#include "CoreLib/TextIO.h"
#include "CoreLib/VariableSizeAllocator.h"

#include <mutex>
#include <thread>
#include <d3d12.h>
#include <dxgi1_4.h>

#define CHECK_DX(x) CORELIB_ASSERT(SUCCEEDED(x) && "Direct3D call error check");

using namespace CoreLib;

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

        static constexpr int ResourceDescriptorHeapSize = 1000000;
        static constexpr int SamplerDescriptorHeapSize = 512;

        int Align(int size, int alignment)
        {
            return (size + alignment - 1) / alignment * alignment;
        }

        struct DescriptorAddress
        {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        };

        class DescriptorHeap
        {
        public:
            D3D12_DESCRIPTOR_HEAP_DESC desc;
            ID3D12DescriptorHeap* heap;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHeapStart;
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHeapStart;
            UINT handleIncrementSize;
            VariableSizeAllocator allocator;
            DescriptorAddress GetAddress(int offsetInDescSlots)
            {
                DescriptorAddress addr;
                addr.cpuHandle = cpuHeapStart;
                addr.gpuHandle = gpuHeapStart;
                addr.cpuHandle.ptr += offsetInDescSlots * handleIncrementSize;
                addr.gpuHandle.ptr += offsetInDescSlots * handleIncrementSize;
                return addr;
            }
        public:
            void Create(
                ID3D12Device* pDevice,
                D3D12_DESCRIPTOR_HEAP_TYPE Type,
                UINT numDescriptors,
                bool shaderVisible = false)
            {
                desc.Type = Type;
                desc.NumDescriptors = numDescriptors;
                desc.Flags = (shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

                CHECK_DX(pDevice->CreateDescriptorHeap(&desc,
                    __uuidof(ID3D12DescriptorHeap),
                    (void**)&heap));

                cpuHeapStart = heap->GetCPUDescriptorHandleForHeapStart();
                gpuHeapStart = heap->GetGPUDescriptorHandleForHeapStart();

                handleIncrementSize = pDevice->GetDescriptorHandleIncrementSize(desc.Type);

                allocator.InitPool(numDescriptors);
            }

            DescriptorAddress Alloc(int numDescs)
            {
                if (numDescs == 0)
                    return GetAddress(0);

                int offset = allocator.Alloc(numDescs);
                CORELIB_ASSERT(offset != -1 && "Descriptor allocation failed.");
                return GetAddress(offset);
            }

            void Free(DescriptorAddress addr, int numDescs)
            {
                if (numDescs == 0)
                    return;
                int offset = (int)(addr.cpuHandle.ptr = cpuHeapStart.ptr);
                allocator.Free(offset, numDescs);
            }
        };


        class RendererState
        {
        public:
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* queue = nullptr;
            DescriptorHeap resourceDescHeap, rtvDescHeap, samplerDescHeap;

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
                else if (heapType == D3D12_HEAP_TYPE_READBACK)
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
                CHECK_DX(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr,
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
                CHECK_DX(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr,
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

        DXGI_FORMAT TranslateStorageFormat(StorageFormat format)
        {
            switch (format)
            {
            case StorageFormat::R_F16: return DXGI_FORMAT_R16_FLOAT;
            case StorageFormat::R_F32: return DXGI_FORMAT_R32_FLOAT;
            case StorageFormat::R_I8: return DXGI_FORMAT_R8_UINT;
            case StorageFormat::R_I16: return DXGI_FORMAT_R16_UINT;
            case StorageFormat::R_8: return DXGI_FORMAT_R8_UNORM;
            case StorageFormat::R_16: return DXGI_FORMAT_R16_UNORM;
            case StorageFormat::Int32_Raw: return DXGI_FORMAT_R32_UINT;
            case StorageFormat::RG_F16: return DXGI_FORMAT_R16G16_FLOAT;
            case StorageFormat::RG_F32: return DXGI_FORMAT_R32G32_FLOAT;
            case StorageFormat::RG_I8: return DXGI_FORMAT_R8G8_UINT;
            case StorageFormat::RG_8: return DXGI_FORMAT_R8G8_UNORM;
            case StorageFormat::RG_16: return DXGI_FORMAT_R16G16_UNORM;
            case StorageFormat::RG_I16: return DXGI_FORMAT_R16G16_UINT;
            case StorageFormat::RG_I32_Raw: return DXGI_FORMAT_R32G32_UINT;
            case StorageFormat::RGBA_F16: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case StorageFormat::RGBA_F32: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case StorageFormat::RGBA_8: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case StorageFormat::RGBA_8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case StorageFormat::RGBA_16: return DXGI_FORMAT_R16G16B16A16_UNORM;
            case StorageFormat::RGBA_I8: return DXGI_FORMAT_R8G8B8A8_UINT;
            case StorageFormat::RGBA_I16: return DXGI_FORMAT_R16G16B16A16_UINT;
            case StorageFormat::RGBA_I32_Raw: return DXGI_FORMAT_R32G32B32A32_UINT;
            case StorageFormat::BC1: return DXGI_FORMAT_BC1_UNORM;
            case StorageFormat::BC1_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
            case StorageFormat::BC5: return DXGI_FORMAT_BC5_UNORM;
            case StorageFormat::BC3: return DXGI_FORMAT_BC3_UNORM;
            case StorageFormat::BC6H: return DXGI_FORMAT_BC6H_UF16;
            case StorageFormat::RGBA_Compressed: return DXGI_FORMAT_BC7_UNORM;
            case StorageFormat::R11F_G11F_B10F: return DXGI_FORMAT_R11G11B10_FLOAT;
            case StorageFormat::RGB10_A2: return DXGI_FORMAT_R10G10B10A2_UNORM;
            case StorageFormat::Depth24: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case StorageFormat::Depth32: return DXGI_FORMAT_D32_FLOAT;
            case StorageFormat::Depth24Stencil8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            default: CORELIB_NOT_IMPLEMENTED("TranslateStorageFormat");
            }
        }

        int GetResourceSize(StorageFormat format, int width, int height)
        {
            switch (format)
            {
            case StorageFormat::R_I16:
            case StorageFormat::R_F16:
            case StorageFormat::R_16:
            case StorageFormat::RG_8: return DXGI_FORMAT_R8G8_UNORM;
            case StorageFormat::RG_I8:
                return width * height * 2;
            case StorageFormat::R_F32:
            case StorageFormat::RG_F16:
            case StorageFormat::RG_16:
            case StorageFormat::Int32_Raw:
            case StorageFormat::RGBA_8:
            case StorageFormat::RGBA_I8:
            case StorageFormat::RGBA_8_SRGB:
            case StorageFormat::RG_I16:
            case StorageFormat::R11F_G11F_B10F:
            case StorageFormat::RGB10_A2:
            case StorageFormat::Depth24:
            case StorageFormat::Depth32:
            case StorageFormat::Depth24Stencil8:
                return width * height * 4;
            case StorageFormat::R_I8:
            case StorageFormat::R_8:
                return width * height;
            case StorageFormat::RG_F32:
            case StorageFormat::RG_I32_Raw:
            case StorageFormat::RGBA_F16:
            case StorageFormat::RGBA_16:
            case StorageFormat::RGBA_I16:
                return width * height * 8;
            case StorageFormat::RGBA_F32:
            case StorageFormat::RGBA_I32_Raw:
                return width * height * 16;
            case StorageFormat::BC1:
            case StorageFormat::BC1_SRGB:
                return ((width + 3) / 4) * ((height + 3) / 4) * 8;
            case StorageFormat::BC5: return DXGI_FORMAT_BC5_UNORM;
            case StorageFormat::BC3: return DXGI_FORMAT_BC3_UNORM;
            case StorageFormat::BC6H: return DXGI_FORMAT_BC6H_UF16;
            case StorageFormat::RGBA_Compressed:
                return ((width + 3) / 4) * ((height + 3) / 4) * 16;
            default: CORELIB_NOT_IMPLEMENTED("GetResourceSize");
            }
        }

        class ResourceBarrier
        {
        public:
            static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, unsigned subresourceID,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = resource;
                barrier.Transition.Subresource = subresourceID;
                barrier.Transition.StateBefore = before;
                barrier.Transition.StateAfter = after;
                return barrier;
            }
        };

        class D3DTexture
        {
        public:
            struct TextureProperties
            {
                StorageFormat format;
                TextureUsage usage;
                DXGI_FORMAT d3dformat;
                D3D12_RESOURCE_DIMENSION dimension;
                int width = 0;
                int height = 0;
                int depth = 0;
                int mipLevels = 0;
                int arraySize = 0;
            };
            List<D3D12_RESOURCE_STATES> subresourceStates;
            TextureProperties properties;
            ID3D12Resource* resource = nullptr;
            D3DTexture() {}
            ~D3DTexture()
            {
                if (resource)
                    resource->Release();
            }
            void BuildMipmaps()
            {
            }
        public:
            static ID3D12Resource* CreateTextureResource(D3D12_HEAP_TYPE heapType, int width, int height, int arraySize, int mipLevel,
                DXGI_FORMAT format, D3D12_RESOURCE_DIMENSION resourceDimension, D3D12_RESOURCE_STATES resState)
            {
                auto& state = RendererState::Get();
                D3D12_HEAP_PROPERTIES heapProperties = {};
                heapProperties.Type = heapType;
                heapProperties.CreationNodeMask = heapProperties.VisibleNodeMask = 1;
                D3D12_RESOURCE_DESC resourceDesc = {};
                resourceDesc.Dimension = resourceDimension;
                resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                resourceDesc.Width = width;
                resourceDesc.Height = height;
                resourceDesc.DepthOrArraySize = (UINT16)arraySize;
                resourceDesc.MipLevels = (UINT16)mipLevel;
                resourceDesc.Format = format;
                resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                resourceDesc.SampleDesc.Count = 1;
                resourceDesc.SampleDesc.Quality = 0;
                ID3D12Resource* resultResource;
                CHECK_DX(state.device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                    &resourceDesc, resState, nullptr, IID_PPV_ARGS(&resultResource)));
                return resultResource;
            }
            static D3D12_RESOURCE_STATES TextureUsageToInitialState(TextureUsage usage)
            {
                switch (usage)
                {
                case TextureUsage::ColorAttachment:
                case TextureUsage::DepthAttachment:
                case TextureUsage::DepthStencilAttachment:
                case TextureUsage::StencilAttachment:
                    return D3D12_RESOURCE_STATE_RENDER_TARGET;
                    break;
                default:
                    return D3D12_RESOURCE_STATE_GENERIC_READ;
                    break;
                }
            }
            void SetData(int mipLevel, int layer, int xOffset, int yOffset, int zOffset, int width, int height, int depth,
                DataType inputType, void* data, D3D12_RESOURCE_STATES newState)
            {
                CORELIB_ASSERT((properties.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && layer == 0) || 
                               (properties.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && zOffset == 0));

                auto packedResourceSize = GetResourceSize(properties.format, width, height);
                // Half float translation
                int dataTypeSize = DataTypeSize(inputType);
                CoreLib::List<unsigned short> translatedBuffer;
                if ((properties.format == StorageFormat::R_F16 || properties.format == StorageFormat::RG_F16 || properties.format == StorageFormat::RGBA_F16)
                    && (GetDataTypeElementType(inputType) != DataType::Half))
                {
                    // transcode f32 to f16
                    int channelCount = 1;
                    switch (properties.format)
                    {
                    case StorageFormat::RG_F16:
                        channelCount = 2;
                        break;
                    case StorageFormat::RGBA_F16:
                        channelCount = 4;
                        break;
                    default:
                        channelCount = 1;
                        break;
                    }
                    translatedBuffer.SetSize(width * height * depth * channelCount);
                    float* src = (float*)data;
                    for (int i = 0; i < translatedBuffer.Count(); i++)
                        translatedBuffer[i] = CoreLib::FloatToHalf(src[i]);
                    dataTypeSize >>= 1;
                    data = (void*)translatedBuffer.Buffer();
                }

                // Copy data
                int slices = 1;
                if (properties.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
                    slices = depth;
                auto& state = RendererState::Get();
                D3D12_RESOURCE_DESC desc = resource->GetDesc();
                uint32_t numRows;
                uint64_t rowSize, totalSize;
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footPrint;
                state.device->GetCopyableFootprints(&desc, mipLevel, 1, 0, &footPrint, &numRows, &rowSize, &totalSize);
                auto stagingResource = state.CreateBufferResource(totalSize, D3D12_HEAP_TYPE_UPLOAD,
                    D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
                std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
                ID3D12GraphicsCommandList* copyCmdList;
                CHECK_DX(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr,
                    IID_PPV_ARGS(&copyCmdList)));
                for (int i = 0; i < slices; i++)
                {
                    unsigned subresourceId = (layer + i) * properties.mipLevels + mipLevel;

                    D3D12_RANGE mapRange;
                    mapRange.Begin = 0;
                    mapRange.End = totalSize;
                    void* stagingBufferPtr;
                    CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
                    if (totalSize == packedResourceSize)
                        memcpy(stagingBufferPtr, data, totalSize);
                    else
                    {
                        for (unsigned row = 0; row < numRows; row++)
                        {
                            memcpy((char*)stagingBufferPtr + footPrint.Footprint.RowPitch * row, (char*)data + rowSize * row, rowSize);
                        }
                    }
                    stagingResource->Unmap(0, &mapRange);
                    if (subresourceStates[subresourceId] != D3D12_RESOURCE_STATE_COPY_DEST)
                    {
                        D3D12_RESOURCE_BARRIER preCopyBarrier = ResourceBarrier::Transition(resource, subresourceId,
                            subresourceStates[subresourceId], D3D12_RESOURCE_STATE_COPY_DEST);
                        copyCmdList->ResourceBarrier(1, &preCopyBarrier);
                    }
                    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
                    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
                    dstLoc.pResource = resource;
                    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dstLoc.SubresourceIndex = subresourceId;
                    srcLoc.pResource = stagingResource;
                    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    srcLoc.PlacedFootprint = footPrint;
                    copyCmdList->CopyTextureRegion(&dstLoc, xOffset, yOffset, zOffset, &srcLoc, nullptr);
                    if (newState != D3D12_RESOURCE_STATE_COPY_DEST)
                    {
                        D3D12_RESOURCE_BARRIER postCopyBarrier = ResourceBarrier::Transition(resource, subresourceId,
                            D3D12_RESOURCE_STATE_COPY_DEST, newState);
                        copyCmdList->ResourceBarrier(1, &postCopyBarrier);
                        subresourceStates[subresourceId] = newState;
                    }
                    CHECK_DX(copyCmdList->Close());
                    ID3D12CommandList* cmdList = copyCmdList;
                    state.queue->ExecuteCommandLists(1, &cmdList);
                    state.Wait();
                    copyCmdList->Reset(state.commandAllocators[state.version], nullptr);
                }
                copyCmdList->Release();
                stagingResource->Release();
                state.largeCopyCmdListAllocator->Reset();
            }
            void GetData(int mipLevel, int layer, int xOffset, int yOffset, int width, int height, int depth,
                void* data, int bufferSize)
            {
                auto& state = RendererState::Get();
                unsigned subresourceId = layer * properties.mipLevels + mipLevel;
                int rowSize = width * StorageFormatSize(properties.format);
                int rowPitch = Align(rowSize, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
                auto resourceSize = height * rowPitch;
                auto stagingResource = state.CreateBufferResource(resourceSize, D3D12_HEAP_TYPE_READBACK,
                    D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
                
                CORELIB_ASSERT(rowSize * height == bufferSize && "Buffer size must match resource size.");

                ID3D12GraphicsCommandList* copyCmdList;
                CHECK_DX(state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr,
                    IID_PPV_ARGS(&copyCmdList)));
                if (subresourceStates[subresourceId] != D3D12_RESOURCE_STATE_COPY_SOURCE)
                {
                    D3D12_RESOURCE_BARRIER preCopyBarrier = ResourceBarrier::Transition(resource, subresourceId,
                        subresourceStates[subresourceId], D3D12_RESOURCE_STATE_COPY_SOURCE);
                    copyCmdList->ResourceBarrier(1, &preCopyBarrier);
                    subresourceStates[subresourceId] = D3D12_RESOURCE_STATE_COPY_SOURCE;
                }
                D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
                D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
                dstLoc.pResource = stagingResource;
                dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dstLoc.PlacedFootprint.Footprint.Depth = depth;
                dstLoc.PlacedFootprint.Footprint.Width = width;
                dstLoc.PlacedFootprint.Footprint.Height = height;
                dstLoc.PlacedFootprint.Footprint.Format = properties.d3dformat;
                dstLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;
                srcLoc.pResource = resource;
                srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                srcLoc.SubresourceIndex = subresourceId;
                D3D12_BOX copyBox = {};
                copyBox.left = xOffset;
                copyBox.top = yOffset;
                copyBox.front = layer;
                copyBox.right = xOffset + width;
                copyBox.bottom = yOffset + height;
                copyBox.back = layer + depth;
                copyCmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &copyBox);
                CHECK_DX(copyCmdList->Close());
                ID3D12CommandList* cmdList = copyCmdList;
                state.queue->ExecuteCommandLists(1, &cmdList);
                state.Wait();
                std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
                D3D12_RANGE mapRange;
                mapRange.Begin = 0;
                mapRange.End = resourceSize;
                void* stagingBufferPtr;
                CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
                if (rowSize != rowPitch)
                {
                    for (int i = 0; i < height; i++)
                        memcpy((char*)data + i * rowSize, (char*)stagingBufferPtr + rowPitch * i, rowSize);
                }
                else
                {
                    memcpy(data, stagingBufferPtr, resourceSize);
                }
                stagingResource->Unmap(0, &mapRange);
                copyCmdList->Release();
                stagingResource->Release();
                state.largeCopyCmdListAllocator->Reset();
            }

            void InitTexture(int width, int height, int layers, int depth, int mipLevels, StorageFormat format,
                TextureUsage usage,
                D3D12_RESOURCE_DIMENSION dimension,
                D3D12_RESOURCE_STATES initialState)
            {
                properties.width = width;
                properties.height = height;
                properties.depth = depth;
                properties.format = format;
                properties.arraySize = layers;
                properties.mipLevels = mipLevels;
                properties.dimension = dimension;
                properties.usage = usage;
                properties.d3dformat = TranslateStorageFormat(format);
                int arraySizeOrDepth = dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ? layers : depth;
                resource = CreateTextureResource(D3D12_HEAP_TYPE_DEFAULT, width, height, arraySizeOrDepth,
                    properties.mipLevels, properties.d3dformat, dimension, initialState);
                subresourceStates.SetSize(properties.mipLevels * properties.arraySize);
                for (auto& s : subresourceStates)
                    s = initialState;
            }
            void InitTexture2DFromData(int width, int height, TextureUsage usage, StorageFormat format, DataType inputType, void* data)
            {
                auto mipLevels = CoreLib::Math::Log2Ceil(CoreLib::Math::Max(width, height)) + 1;
                InitTexture(width, height, 1, 1, mipLevels, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_STATE_COPY_DEST);
               
                // Copy data
                SetData(0, 0, 0, 0, 0, width, height, 1, inputType, data, TextureUsageToInitialState(usage));

                // Build mipmaps
                BuildMipmaps();
            }
            
        };

        class Texture2D : public GameEngine::Texture2D
        {
        public:
            D3DTexture texture;
        public:
            virtual void GetSize(int& width, int& height) override
            {
                width = texture.properties.width;
                height = texture.properties.height;
            }
            virtual void SetData(int level, int width, int height, int /*samples*/, DataType inputType, void* data) override
            {
                CORELIB_ASSERT(width == CoreLib::Math::Max(1, (texture.properties.width >> level)));
                CORELIB_ASSERT(height == CoreLib::Math::Max(1, (texture.properties.height >> level)));
                texture.SetData(level, 0, 0, 0, 0, width, height, 0, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            virtual void SetData(int width, int height, int /*samples*/, DataType inputType, void* data) override
            {
                CORELIB_ASSERT(width == texture.properties.width);
                CORELIB_ASSERT(height == texture.properties.height);
                texture.SetData(0, 0, 0, 0, 0, width, height, 0, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            virtual void GetData(int mipLevel, void* data, int bufSize) override
            {
                texture.GetData(mipLevel, 0, 0, 0, texture.properties.width, texture.properties.height, 0,
                    data, bufSize);
            }
            virtual void BuildMipmaps() override
            {
                texture.BuildMipmaps();
            }
            virtual bool IsDepthStencilFormat() override
            {
                auto format = texture.properties.format;
                return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 || format == StorageFormat::Depth32;
            }
        };

        class Texture2DArray : public GameEngine::Texture2DArray
        {
        public:
            D3DTexture texture;
            Texture2DArray() {}
        public:
            virtual void GetSize(int& width, int& height, int& layers) override
            {
                width = texture.properties.width;
                height = texture.properties.height;
                layers = texture.properties.arraySize;
            }
            virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int width, int height, int layerCount, DataType inputType, void* data) override
            {
                CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.width >> mipLevel) == width);
                CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.height >> mipLevel) == height);

                texture.SetData(mipLevel, layerOffset, xOffset, yOffset, 0, width, height, layerCount, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            virtual void BuildMipmaps() override
            {
                texture.BuildMipmaps();
            }
            virtual bool IsDepthStencilFormat() override
            {
                auto format = texture.properties.format;
                return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 || format == StorageFormat::Depth32;
            }
        };

        class Texture3D : public GameEngine::Texture3D
        {
        public:
            D3DTexture texture;
            Texture3D() {}

        public:
            virtual void GetSize(int& width, int& height, int& depth) override
            {
                width = texture.properties.width;
                height = texture.properties.height;
                depth = texture.properties.depth;
            }
            virtual void SetData(int mipLevel, int xOffset, int yOffset, int zOffset, int width, int height, int depth, DataType inputType, void* data) override
            {
                texture.SetData(mipLevel, 0, xOffset, yOffset, zOffset, width, height, depth, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            virtual bool IsDepthStencilFormat() override
            {
                auto format = texture.properties.format;
                return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 || format == StorageFormat::Depth32;
            }
        };

        class TextureCube : public GameEngine::TextureCube
        {
        public:
            D3DTexture texture;
            TextureCube() {}

        public:
            virtual void GetSize(int& size) override
            {
                size = texture.properties.width;
            }
            virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int width, int height, int layerCount, DataType inputType, void* data) override
            {
                CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.width >> mipLevel) == width);
                CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.height >> mipLevel) == height);

                texture.SetData(mipLevel, layerOffset, xOffset, yOffset, 0, width, height, layerCount, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            virtual bool IsDepthStencilFormat() override
            {
                auto format = texture.properties.format;
                return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 || format == StorageFormat::Depth32;
            }
        };

        class TextureCubeArray : public GameEngine::TextureCubeArray
        {
        public:
            D3DTexture texture;
            TextureCubeArray() {}
        public:
            virtual void GetSize(int& size, int& layerCount) override
            {
                size = texture.properties.width;
                layerCount = texture.properties.arraySize / 6;
            }
            virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int width, int height, int layerCount, DataType inputType, void* data) override
            {
                CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.width >> mipLevel) == width);
                CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.height >> mipLevel) == height);

                texture.SetData(mipLevel, layerOffset, xOffset, yOffset, 0, width, height, layerCount, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
            }
            virtual bool IsDepthStencilFormat() override
            {
                auto format = texture.properties.format;
                return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 || format == StorageFormat::Depth32;
            }
        };

        class TextureSampler : public GameEngine::TextureSampler
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

        class Shader : public GameEngine::Shader
        {
        public:
            Shader() {};
        };

        class FrameBuffer : public GameEngine::FrameBuffer
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

        class Fence : public GameEngine::Fence
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

        class RenderTargetLayout : public GameEngine::RenderTargetLayout
        {
        public:
            RenderTargetLayout() {}
        public:
            virtual GameEngine::FrameBuffer* CreateFrameBuffer(const RenderAttachments& attachments) override
            {
                return new FrameBuffer(attachments);
            }
        };

        class DescriptorSetLayout : public GameEngine::DescriptorSetLayout
        {
        public:
            CoreLib::List<DescriptorLayout> descriptors;
            DescriptorSetLayout(CoreLib::ArrayView<DescriptorLayout> descs)
            {
                descriptors.AddRange(descs);
            }
        };

        struct DescriptorNode
        {
            DescriptorLayout layout;
            DescriptorAddress address;
        };

        class DescriptorSet : public GameEngine::DescriptorSet
        {
        private:
            DescriptorAddress resourceAddr, samplerAddr;
            int resourceCount, samplerCount;
            List<DescriptorNode> descriptors;
        public:
            DescriptorSet(DescriptorSetLayout* layout)
            {
                auto& state = RendererState::Get();
                resourceCount = 0;
                samplerCount = 0;
                descriptors.Reserve(layout->descriptors.Count());
                for (auto& desc : layout->descriptors)
                {
                    DescriptorNode node;
                    node.layout = desc;
                    node.address.cpuHandle.ptr = 0;
                    switch (desc.Type)
                    {
                    case BindingType::Sampler:
                        // temporarily store offset in cpuHandle.ptr
                        node.address.cpuHandle.ptr = samplerCount;
                        samplerCount++;
                        break;
                    case BindingType::StorageBuffer:
                    case BindingType::StorageTexture:
                    case BindingType::Texture:
                    case BindingType::UniformBuffer:
                        node.address.cpuHandle.ptr = resourceCount;
                        resourceCount += desc.ArraySize;
                        break;
                    case BindingType::Unused:
                        break;
                    }
                    descriptors.Add(node);
                }
                resourceAddr = state.resourceDescHeap.Alloc(resourceCount);
                samplerAddr = state.samplerDescHeap.Alloc(samplerCount);
                for (auto& node : descriptors)
                {
                    auto offset = node.address.cpuHandle.ptr;
                    // Fill in true address
                    if (node.layout.Type == BindingType::Sampler)
                    {
                        node.address.cpuHandle.ptr = samplerAddr.cpuHandle.ptr 
                            + offset * state.samplerDescHeap.handleIncrementSize;
                        node.address.gpuHandle.ptr = samplerAddr.gpuHandle.ptr
                            + offset * state.samplerDescHeap.handleIncrementSize;
                    }
                    else
                    {
                        node.address.cpuHandle.ptr = resourceAddr.cpuHandle.ptr
                            + offset * state.resourceDescHeap.handleIncrementSize;
                        node.address.gpuHandle.ptr = resourceAddr.gpuHandle.ptr
                            + offset * state.resourceDescHeap.handleIncrementSize;
                    }
                }
            }
            ~DescriptorSet()
            {
                auto& state = RendererState::Get();
                state.resourceDescHeap.Free(resourceAddr, resourceCount);
                state.samplerDescHeap.Free(samplerAddr, samplerCount);
            }
        public:
            virtual void BeginUpdate() override {}
            virtual void Update(int /*location*/, GameEngine::Texture* /*texture*/, TextureAspect /*aspect*/) override
            {

            }
            virtual void Update(int /*location*/, CoreLib::ArrayView<GameEngine::Texture*> /*texture*/, TextureAspect /*aspect*/) override
            {

            }
            virtual void UpdateStorageImage(int /*location*/, CoreLib::ArrayView<GameEngine::Texture*> /*texture*/, TextureAspect /*aspect*/) override
            {
            }
            virtual void Update(int /*location*/, GameEngine::TextureSampler* /*sampler*/) override
            {
            }
            virtual void Update(int /*location*/, GameEngine::Buffer* /*buffer*/, int /*offset*/, int /*length*/) override
            {
            }
            virtual void EndUpdate() override
            {
            }
        };

        class Pipeline : public GameEngine::Pipeline
        {
        public:
            Pipeline() {}
        };

        class PipelineBuilder : public GameEngine::PipelineBuilder
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

        class CommandBuffer : public GameEngine::CommandBuffer
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
            virtual void ClearAttachments(GameEngine::FrameBuffer* /*frameBuffer*/) override {}
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
                    CHECK_DX(state.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(&state.largeCopyCmdListAllocator)));

                    state.resourceDescHeap.Create(state.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ResourceDescriptorHeapSize, true);
                    state.samplerDescHeap.Create(state.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SamplerDescriptorHeapSize, true);
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
            virtual void ClearTexture(GameEngine::Texture2D* /*texture*/) override
            {
            }
            virtual void BeginJobSubmission() override
            {
            }
            virtual void QueueRenderPass(GameEngine::FrameBuffer* /*frameBuffer*/, CoreLib::ArrayView<GameEngine::CommandBuffer*> /*commands*/,
                PipelineBarriers /*barriers*/) override
            {
            }
            virtual void QueueComputeTask(GameEngine::Pipeline* /*computePipeline*/, GameEngine::DescriptorSet* /*descriptorSet*/,
                int /*x*/, int /*y*/, int /*z*/, PipelineBarriers /*barriers*/) override
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
            virtual void Blit(GameEngine::Texture2D* /*dstImage*/, GameEngine::Texture2D* /*srcImage*/,
                VectorMath::Vec2i /*destOffset*/, bool /*flipSrc*/) override
            {
            }
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
            virtual GameEngine::Texture2D* CreateTexture2D(String name, int width, int height, StorageFormat format, DataType type, void* data) override
            {
                auto result = new Texture2D();
                result->texture.InitTexture2DFromData(width, height, TextureUsage::Sampled, format, type, data);
                return result;
            }
            virtual GameEngine::Texture2D* CreateTexture2D(String name, TextureUsage usage, int width, int height, int mipLevelCount, StorageFormat format) override
            {
                auto result = new Texture2D();
                result->texture.InitTexture(width, height, 1, 1, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D, 
                    D3DTexture::TextureUsageToInitialState(usage));
                return result;
            }
            virtual GameEngine::Texture2D* CreateTexture2D(String name, TextureUsage usage, int width, int height, int mipLevelCount, StorageFormat format, DataType type, CoreLib::ArrayView<void*> mipLevelData) override
            {
                auto result = new Texture2D();
                result->texture.InitTexture(width, height, 1, 1, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                for (int i = 0; i < mipLevelCount; i++)
                {
                    result->texture.SetData(i, 0, 0, 0, 0, CoreLib::Math::Max(1, (width >> i)), CoreLib::Math::Max(1, (height >> i)), 1, type, mipLevelData[i], D3DTexture::TextureUsageToInitialState(usage));
                }
                return result;
            }
            virtual GameEngine::Texture2DArray* CreateTexture2DArray(String name, TextureUsage usage, int width, int height, int layers, int mipLevelCount, StorageFormat format) override
            {
                auto result = new Texture2DArray();
                result->texture.InitTexture(width, height, layers, 1, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                    D3DTexture::TextureUsageToInitialState(usage));
                return result;
            }
            virtual GameEngine::TextureCube* CreateTextureCube(String name, TextureUsage usage, int size, int mipLevelCount, StorageFormat format) override
            {
                auto result = new TextureCube();
                result->texture.InitTexture(size, size, 6, 1, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                    D3DTexture::TextureUsageToInitialState(usage));
                return result;
            }
            virtual GameEngine::TextureCubeArray* CreateTextureCubeArray(String name, TextureUsage usage, int size, int mipLevelCount, int cubemapCount, StorageFormat format) override
            {
                auto result = new TextureCubeArray();
                result->texture.InitTexture(size, size, 6 * cubemapCount, 1, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                    D3DTexture::TextureUsageToInitialState(usage));
                return result;
            }
            virtual GameEngine::Texture3D* CreateTexture3D(String name, TextureUsage usage, int width, int height, int depth, int mipLevelCount, StorageFormat format) override
            {
                auto result = new Texture3D();
                result->texture.InitTexture(width, height, 1, depth, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE3D,
                    D3DTexture::TextureUsageToInitialState(usage));
                return result;
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
            virtual GameEngine::DescriptorSetLayout* CreateDescriptorSetLayout(CoreLib::ArrayView<DescriptorLayout> descriptors) override
            {
                return new DescriptorSetLayout(descriptors);
            }
            virtual GameEngine::DescriptorSet* CreateDescriptorSet(GameEngine::DescriptorSetLayout* layout) override
            {
                return new DescriptorSet(dynamic_cast<D3DRenderer::DescriptorSetLayout*>(layout));
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