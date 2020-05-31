#include "HardwareRenderer.h"

#if __has_include(<d3d12.h>)

#include "CoreLib/Stream.h"
#include "CoreLib/TextIO.h"
#include "CoreLib/VariableSizeAllocator.h"
#include "CoreLib/WinForm/Debug.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <mutex>
#include <thread>

#define CHECK_DX(x) CORELIB_ASSERT(SUCCEEDED(x) && "Direct3D call error check");

using namespace CoreLib;

namespace GameEngine
{
namespace D3DRenderer
{
typedef HRESULT(__stdcall *PFN_CREATE_DXGI_FACTORY1)(REFIID, _COM_Outptr_ void **);

thread_local int renderThreadId = 0;
static constexpr int MaxRenderThreads = 8;
// Max number of command lists that can be in recording state per render thread.
static constexpr int MaxParallelCommandLists = 16;
static constexpr int D3DConstantBufferAlignment = 256;
static constexpr int D3DStorageBufferAlignment = 256;
// 64MB staging buffer per frame version
static constexpr int StagingBufferSize = 1 << 26;
// Max data size allowed to use the shared staging buffer for CPU-GPU upload.
static constexpr int SharedStagingBufferDataSizeThreshold = 1 << 20;

static constexpr int ResourceDescriptorHeapSize = 1000000;
static constexpr int SamplerDescriptorHeapSize = 512;
static constexpr int RtvDescriptorHeapSize = 1024;
static constexpr int DsvDescriptorHeapSize = 1024;

class LibPIX
{
private:
    typedef HRESULT(WINAPI *PFN_BeginEventOnCommandList)(
        ID3D12GraphicsCommandList *commandList, UINT64 color, _In_ PCSTR formatString);
    typedef HRESULT(WINAPI *PFN_EndEventOnCommandList)(ID3D12GraphicsCommandList *commandList);
    typedef HRESULT(WINAPI *PFN_SetMarkerOnCommandList)(
        ID3D12GraphicsCommandList *commandList, UINT64 color, _In_ PCSTR formatString);

    PFN_BeginEventOnCommandList beginEventOnCommandList = nullptr;
    PFN_EndEventOnCommandList endEventOnCommandList = nullptr;
    PFN_SetMarkerOnCommandList setMarkerOnCommandList = nullptr;

public:
    void Load()
    {
        HMODULE module = LoadLibrary(L"WinPixEventRuntime.dll");
        if (module)
        {
            beginEventOnCommandList = (PFN_BeginEventOnCommandList)GetProcAddress(module, "PIXBeginEventOnCommandList");
            endEventOnCommandList = (PFN_EndEventOnCommandList)GetProcAddress(module, "PIXEndEventOnCommandList");
            setMarkerOnCommandList = (PFN_SetMarkerOnCommandList)GetProcAddress(module, "PIXSetMarkerOnCommandList");
        }
    }
    void BeginEventOnCommandList(ID3D12GraphicsCommandList *commandList, UINT64 color, _In_ PCSTR formatString)
    {
        if (beginEventOnCommandList)
            beginEventOnCommandList(commandList, color, formatString);
    }
    void EndEventOnCommandList(ID3D12GraphicsCommandList *commandList)
    {
        if (endEventOnCommandList)
            endEventOnCommandList(commandList);
    }
    void SetMarkerOnCommandList(ID3D12GraphicsCommandList *commandList, UINT64 color, _In_ PCSTR formatString)
    {
        if (setMarkerOnCommandList)
            setMarkerOnCommandList(commandList, color, formatString);
    }
};

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
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    ID3D12DescriptorHeap *heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHeapStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHeapStart = {};

    List<int> tempAllocationStartIndices;
    List<int> tempAllocationPtrs;
    int maxTempDescriptors;

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
    void Create(ID3D12Device *pDevice, D3D12_DESCRIPTOR_HEAP_TYPE Type, UINT numDescriptors, int tempDescriptorCount,
        int tempVersionCount, bool shaderVisible = false)
    {
        desc.Type = Type;
        desc.NumDescriptors = numDescriptors;
        desc.Flags = (shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

        CHECK_DX(pDevice->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), (void **)&heap));

        cpuHeapStart = heap->GetCPUDescriptorHandleForHeapStart();
        if (shaderVisible)
            gpuHeapStart = heap->GetGPUDescriptorHandleForHeapStart();

        handleIncrementSize = pDevice->GetDescriptorHandleIncrementSize(desc.Type);

        allocator.InitPool(numDescriptors - tempDescriptorCount * tempVersionCount);

        maxTempDescriptors = tempDescriptorCount;
        tempAllocationStartIndices.SetSize(tempVersionCount);
        tempAllocationPtrs.SetSize(tempVersionCount);
        for (int i = 0; i < tempVersionCount; i++)
        {
            tempAllocationPtrs[i] = tempAllocationStartIndices[i] =
                numDescriptors - tempDescriptorCount * (tempVersionCount - i);
        }
    }

    void Destroy()
    {
        heap->Release();
        allocator.Destroy();
        tempAllocationPtrs = decltype(tempAllocationPtrs)();
        tempAllocationStartIndices = decltype(tempAllocationStartIndices)();
    }

    DescriptorAddress Alloc(int numDescs)
    {
        if (numDescs == 0)
            return GetAddress(0);

        int offset = allocator.Alloc(numDescs);
        CORELIB_ASSERT(offset != -1 && "Descriptor allocation failed.");
        return GetAddress(offset);
    }

    void ResetTemp(int version)
    {
        tempAllocationPtrs[version] = tempAllocationStartIndices[version];
    }

    DescriptorAddress AllocTemp(int version, int numDescs)
    {
        CORELIB_ASSERT(
            tempAllocationPtrs[version] + numDescs - tempAllocationStartIndices[version] <= maxTempDescriptors);
        int slot = tempAllocationPtrs[version];
        tempAllocationPtrs[version] += numDescs;
        return GetAddress(slot);
    }

    void FreeAll()
    {
        allocator.Destroy();
        allocator.InitPool(desc.NumDescriptors);
    }

    void Free(D3D12_CPU_DESCRIPTOR_HANDLE addr, int numDescs)
    {
        if (numDescs == 0)
            return;
        int offset = (int)(addr.ptr - cpuHeapStart.ptr) / handleIncrementSize;
        allocator.Free(offset, numDescs);
    }

    void Free(DescriptorAddress addr, int numDescs)
    {
        Free(addr.cpuHandle, numDescs);
    }
};

D3D12_COMPARISON_FUNC TranslateCompareFunc(CompareFunc op)
{
    switch (op)
    {
    case CompareFunc::Always:
    case CompareFunc::Disabled:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    case CompareFunc::Equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareFunc::Greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case CompareFunc::GreaterEqual:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareFunc::Less:
        return D3D12_COMPARISON_FUNC_LESS;
    case CompareFunc::LessEqual:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareFunc::Never:
        return D3D12_COMPARISON_FUNC_NEVER;
    case CompareFunc::NotEqual:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    default:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

D3D12_CULL_MODE TranslateCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::CullBackFace:
        return D3D12_CULL_MODE_BACK;
    case CullMode::CullFrontFace:
        return D3D12_CULL_MODE_FRONT;
    case CullMode::Disabled:
        return D3D12_CULL_MODE_NONE;
    default:
        throw CoreLib::NotImplementedException("TranslateCullMode");
    }
}

D3D12_FILL_MODE TranslateFillMode(PolygonMode mode)
{
    switch (mode)
    {
    case PolygonMode::Fill:
        return D3D12_FILL_MODE_SOLID;
    case PolygonMode::Line:
        return D3D12_FILL_MODE_WIREFRAME;
    case PolygonMode::Point:
        return D3D12_FILL_MODE_WIREFRAME;
    default:
        return D3D12_FILL_MODE_SOLID;
    }
}

D3D12_STENCIL_OP TranslateStencilOp(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Keep:
        return D3D12_STENCIL_OP_KEEP;
    case StencilOp::Zero:
        return D3D12_STENCIL_OP_ZERO;
    case StencilOp::Replace:
        return D3D12_STENCIL_OP_REPLACE;
    case StencilOp::Increment:
        return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOp::IncrementWrap:
        return D3D12_STENCIL_OP_INCR;
    case StencilOp::Decrement:
        return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOp::DecrementWrap:
        return D3D12_STENCIL_OP_DECR;
    case StencilOp::Invert:
        return D3D12_STENCIL_OP_INVERT;
    default:
        CORELIB_NOT_IMPLEMENTED("TranslateStencilOp");
    }
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE TranslateTopologyType(PrimitiveType type)
{
    switch (type)
    {
    case PrimitiveType::Triangles:
    case PrimitiveType::TriangleStrips:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveType::Points:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveType::Lines:
    case PrimitiveType::LineStrips:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default:
        CORELIB_NOT_IMPLEMENTED("TranslatePrimitiveTopology");
    }
}

D3D12_PRIMITIVE_TOPOLOGY TranslatePrimitiveTopology(PrimitiveType type)
{
    switch (type)
    {
    case PrimitiveType::Triangles:
        return D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveType::TriangleStrips:
        return D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveType::Points:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveType::Lines:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveType::LineStrips:
        return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    default:
        CORELIB_NOT_IMPLEMENTED("TranslatePrimitiveTopology");
    }
}

typedef HRESULT(__stdcall *PFN_D3DCOMPILE)(LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const D3D_SHADER_MACRO *pDefines, ID3DInclude *pInclude, LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1,
    UINT Flags2, ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs);

struct CommandAllocator
{
    ID3D12CommandAllocator *allocator = nullptr;
    bool inUse = false;
};

class CommandAllocatorManager
{
public:
    CoreLib::List<CoreLib::Array<CoreLib::Array<CommandAllocator, MaxParallelCommandLists>, MaxRenderThreads>>
        commandAllocators;
    D3D12_COMMAND_LIST_TYPE commandListType;
    void Init(int versionCount, D3D12_COMMAND_LIST_TYPE type)
    {
        commandListType = type;
        commandAllocators.SetSize(versionCount);
        for (int i = 0; i < versionCount; i++)
            commandAllocators[i].SetSize(MaxRenderThreads);
    }
    CommandAllocator *GetAvailableAllocator(ID3D12Device *device, int version)
    {
        auto &list = commandAllocators[version][renderThreadId];
        for (int i = 0; i < list.Count(); i++)
        {
            if (!list[i].inUse)
                return &list[i];
        }
        if (list.Count() == list.GetCapacity())
        {
            CORELIB_ABORT("Exceeded maximum number of command lists allowed to be in recording state"
                          "at the same time.");
        }
        list.Add(CommandAllocator());
        auto &lastAllocator = list[list.Count() - 1];
        CHECK_DX(device->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&lastAllocator.allocator)));
        return &lastAllocator;
    }
    void Free()
    {
        for (auto &list : commandAllocators)
        {
            for (auto &threadList : list)
            {
                for (auto &allocator : threadList)
                    allocator.allocator->Release();
                threadList.Clear();
            }
        }
        commandAllocators = decltype(commandAllocators)();
    }
    void Reset(int version)
    {
        for (auto &list : commandAllocators[version])
        {
            for (auto &allocator : list)
            {
                CORELIB_DEBUG_ASSERT(!allocator.inUse);
                CHECK_DX(allocator.allocator->Reset());
            }
        }
    }
};

struct D3DCommandList
{
    ID3D12GraphicsCommandList *list;
    CommandAllocator *allocator;
    HRESULT Close()
    {
        allocator->inUse = false;
        return list->Close();
    }
    ID3D12GraphicsCommandList *operator->()
    {
        return list;
    }
};

class RendererState
{
public:
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandQueue *transferQueue = nullptr;

    DescriptorHeap resourceDescHeap, rtvDescHeap, dsvDescHeap, samplerDescHeap;
    IDXGIFactory4 *dxgiFactory = nullptr;
    LibPIX pix;

    int version = 0;
    CoreLib::List<ID3D12Resource *> stagingBuffers;
    CoreLib::List<long> stagingBufferAllocPtr;
    std::mutex stagingBufferMutex;
    CoreLib::List<CoreLib::List<ID3D12GraphicsCommandList *>> tempCommandLists;
    CoreLib::List<long> tempCommandListAllocPtr;
    CoreLib::List<CoreLib::List<ID3D12GraphicsCommandList *>> copyCommandLists;
    CoreLib::List<long> copyCommandListAllocPtr;
    CommandAllocatorManager tempCommandAllocatorManager, copyCommandAllocatorManager;
    ID3D12CommandAllocator *largeCopyCmdListAllocator;
    std::mutex tempCommandListMutex, largeCopyCmdListMutex;

    CoreLib::String deviceName;
    CoreLib::String cacheLocation;
    int rendererCount = 0;
    size_t videoMemorySize = 0;
    uint64_t waitFenceValue = 1;
    ID3D12Fence *waitFences[MaxRenderThreads] = {};
    HANDLE waitEvents[MaxRenderThreads] = {};
    PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE serializeVersionedRootSignature = nullptr;
    PFN_D3DCOMPILE d3dCompile = nullptr;

    D3DCommandList GetCommandList(CommandAllocatorManager *commandAllocatorManager,
        CoreLib::List<CoreLib::List<ID3D12GraphicsCommandList *>> &commandLists,
        CoreLib::List<long> &commandListAllocPtrs)
    {
        long cmdListId = 0;
        auto cmdAllocator = commandAllocatorManager->GetAvailableAllocator(device, version);
        {
            std::lock_guard<std::mutex> lock(tempCommandListMutex);
            auto &allocPtr = commandListAllocPtrs[version];
            if (allocPtr == commandLists[version].Count())
            {
                ID3D12GraphicsCommandList *commandList;
                CHECK_DX(device->CreateCommandList(0, commandAllocatorManager->commandListType, cmdAllocator->allocator,
                    nullptr, IID_PPV_ARGS(&commandList)));
                commandList->Close();
                commandLists[version].Add(commandList);
            }
            cmdListId = allocPtr;
            allocPtr++;
        }
        auto cmdList = commandLists[version][cmdListId];
        cmdList->Reset(cmdAllocator->allocator, nullptr);
        cmdAllocator->inUse = true;
        return D3DCommandList{cmdList, cmdAllocator};
    }

    D3DCommandList GetCopyCommandList()
    {
        return GetCommandList(&copyCommandAllocatorManager, copyCommandLists, copyCommandListAllocPtr);
    }

    D3DCommandList GetTempCommandList()
    {
        return GetCommandList(&tempCommandAllocatorManager, tempCommandLists, tempCommandListAllocPtr);
    }

    ID3D12Resource *CreateBufferResource(size_t sizeInBytes, D3D12_HEAP_TYPE heapType,
        D3D12_CPU_PAGE_PROPERTY cpuPageProperty, bool allowUnorderedAccess)
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
        ID3D12Resource *resource;
        D3D12_RESOURCE_STATES resState = D3D12_RESOURCE_STATE_COMMON;
        if (heapType == D3D12_HEAP_TYPE_UPLOAD)
            resState = D3D12_RESOURCE_STATE_GENERIC_READ;
        else if (heapType == D3D12_HEAP_TYPE_READBACK)
            resState = D3D12_RESOURCE_STATE_COPY_DEST;
        CHECK_DX(device->CreateCommittedResource(
            &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, resState, nullptr, IID_PPV_ARGS(&resource)));
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
        value = InterlockedIncrement(&waitFenceValue);
        CHECK_DX(transferQueue->Signal(waitFences[renderThreadId], value));
        CHECK_DX(waitFences[renderThreadId]->SetEventOnCompletion(value, waitEvents[renderThreadId]));
        WaitForSingleObject(waitEvents[renderThreadId], INFINITE);
    }
    static void Free()
    {
        auto &state = Get();
        for (int i = 0; i < MaxRenderThreads; i++)
        {
            if (state.waitFences[i])
            {
                state.waitFences[i]->Release();
                CloseHandle(state.waitEvents[i]);
            }
        }
        state.resourceDescHeap.Destroy();
        state.samplerDescHeap.Destroy();
        state.rtvDescHeap.Destroy();
        state.dsvDescHeap.Destroy();
        for (auto &cmdLists : state.tempCommandLists)
            for (auto cmdList : cmdLists)
                cmdList->Release();
        for (auto &cmdLists : state.copyCommandLists)
            for (auto cmdList : cmdLists)
                cmdList->Release();
        state.tempCommandAllocatorManager.Free();
        state.copyCommandAllocatorManager.Free();
        state.largeCopyCmdListAllocator->Release();
        for (auto stagingBuffer : state.stagingBuffers)
            stagingBuffer->Release();
        if (state.queue)
            state.queue->Release();
        if (state.transferQueue)
            state.transferQueue->Release();
        if (state.dxgiFactory)
            state.dxgiFactory->Release();
        if (state.device)
            state.device->Release();
        state.tempCommandListAllocPtr = decltype(state.tempCommandListAllocPtr)();
        state.copyCommandListAllocPtr = decltype(state.copyCommandListAllocPtr)();
        state.copyCommandAllocatorManager = decltype(state.copyCommandAllocatorManager)();
        state.tempCommandAllocatorManager = decltype(state.tempCommandAllocatorManager)();
        state.stagingBuffers = decltype(state.stagingBuffers)();
        state.stagingBufferAllocPtr = decltype(state.stagingBufferAllocPtr)();
        state.tempCommandLists = decltype(state.tempCommandLists)();
        state.copyCommandLists = decltype(state.copyCommandLists)();
        state.cacheLocation = CoreLib::String();
        state.deviceName = CoreLib::String();
    }
    static RendererState &Get()
    {
        static RendererState state;
        return state;
    }
};

class D3DResource
{
public:
    List<D3D12_RESOURCE_STATES> subresourceStates;
    ID3D12Resource *resource = nullptr;

    void TransferState(int subresourceId, D3D12_RESOURCE_STATES targetState, List<D3D12_RESOURCE_BARRIER> &barriers)
    {
        if (subresourceId == -1)
        {
            bool subresourceStatesConsistent = true;
            for (int i = 1; i < subresourceStates.Count(); i++)
            {
                if (subresourceStates[i] != subresourceStates[0])
                {
                    subresourceStatesConsistent = false;
                    break;
                }
            }
            if (subresourceStatesConsistent && subresourceStates[0] != targetState)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = resource;
                barrier.Transition.StateBefore = subresourceStates[0];
                barrier.Transition.StateAfter = targetState;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barriers.Add(barrier);
                for (int i = 0; i < subresourceStates.Count(); i++)
                    subresourceStates[i] = targetState;
            }
            else
            {
                for (int i = 0; i < subresourceStates.Count(); i++)
                {
                    if (subresourceStates[i] != targetState)
                    {
                        D3D12_RESOURCE_BARRIER barrier = {};
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition.pResource = resource;
                        barrier.Transition.StateBefore = subresourceStates[i];
                        barrier.Transition.StateAfter = targetState;
                        barrier.Transition.Subresource = i;
                        barriers.Add(barrier);
                        subresourceStates[i] = targetState;
                    }
                }
            }
            return;
        }
        if (subresourceStates[subresourceId] != targetState)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = subresourceStates[subresourceId];
            barrier.Transition.StateAfter = targetState;
            barrier.Transition.Subresource = subresourceId;
            barriers.Add(barrier);
            subresourceStates[subresourceId] = targetState;
        }
    }
};

class Buffer : public GameEngine::Buffer
{
public:
    D3DResource buffer;
    BufferStructureInfo structInfo = {};
    int bufferSize;
    bool isMappable = false;
    int mappedStart = -1;
    int mappedEnd = -1;
    BufferUsage usage;
    List<D3D12_RESOURCE_BARRIER> resourceBarriers;

public:
    Buffer(BufferUsage usage, int sizeInBytes, bool mappable, const BufferStructureInfo *pStructInfo)
    {
        auto &state = RendererState::Get();
        this->usage = usage;
        this->isMappable = mappable;
        bufferSize = Align(sizeInBytes, D3DConstantBufferAlignment);
        buffer.resource =
            state.CreateBufferResource(bufferSize, mappable ? D3D12_HEAP_TYPE_CUSTOM : D3D12_HEAP_TYPE_DEFAULT,
                mappable ? D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE : D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                usage == BufferUsage::StorageBuffer);
        buffer.subresourceStates.SetSize(1);
        buffer.subresourceStates[0] = D3D12_RESOURCE_STATE_COMMON;

        CORELIB_ASSERT(usage != BufferUsage::StorageBuffer || pStructInfo != nullptr);
        if (pStructInfo != nullptr)
        {
            structInfo = *pStructInfo;
        }
    }
    ~Buffer()
    {
        buffer.resource->Release();
    }

    // SetDataImpl: returns true if succeeded without GPU synchronization.
    bool SetDataImpl(int offset, void *data, int size)
    {
        auto &state = RendererState::Get();
        if (size == 0)
            return true;
        if (isMappable)
        {
            D3D12_RANGE range;
            range.Begin = (SIZE_T)offset;
            range.End = (SIZE_T)(size + offset);
            void *mappedPtr = nullptr;
            CHECK_DX(buffer.resource->Map(0, &range, &mappedPtr));
            memcpy((char *)mappedPtr + offset, data, size);
            buffer.resource->Unmap(0, &range);
            buffer.subresourceStates[0] = D3D12_RESOURCE_STATE_COPY_DEST;
            return true;
        }

        if (size < SharedStagingBufferDataSizeThreshold)
        {
            // Use shared staging buffer for async upload
            std::lock_guard<std::mutex> lockGuard(state.stagingBufferMutex);
            auto stagingOffset = InterlockedAdd(&(state.stagingBufferAllocPtr[state.version]), size);
            if (stagingOffset <= StagingBufferSize)
            {
                stagingOffset -= size;
                // Map and copy to staging buffer.
                D3D12_RANGE range;
                range.Begin = (SIZE_T)stagingOffset;
                range.End = (SIZE_T)(size + stagingOffset);
                void *mappedStagingPtr = nullptr;
                CHECK_DX(state.stagingBuffers[state.version]->Map(0, &range, &mappedStagingPtr));
                memcpy((char *)mappedStagingPtr + stagingOffset, data, size);
                state.stagingBuffers[state.version]->Unmap(0, &range);
                // Submit a copy command to GPU.
                auto copyCommandList = state.GetCopyCommandList();
                resourceBarriers.Clear();
                buffer.TransferState(0, D3D12_RESOURCE_STATE_COPY_DEST, resourceBarriers);
                if (resourceBarriers.Count())
                    copyCommandList->ResourceBarrier(resourceBarriers.Count(), resourceBarriers.Buffer());
                copyCommandList->CopyBufferRegion(
                    buffer.resource, offset, state.stagingBuffers[state.version], stagingOffset, size);
                CHECK_DX(copyCommandList.Close());
                ID3D12CommandList *cmdList = copyCommandList.list;
                state.transferQueue->ExecuteCommandLists(1, &cmdList);
                return true;
            }
        }
        // Data is too large to use the shared staging buffer, or the staging buffer
        // is already full.
        // Create a dedicated staging buffer and perform uploading right here.

        std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
        ID3D12Resource *stagingResource =
            state.CreateBufferResource(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
        D3D12_RANGE mapRange;
        mapRange.Begin = 0;
        mapRange.End = size;
        void *stagingBufferPtr;
        CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
        memcpy(stagingBufferPtr, data, size);
        stagingResource->Unmap(0, &mapRange);
        ID3D12GraphicsCommandList *copyCmdList;
        CHECK_DX(state.device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr, IID_PPV_ARGS(&copyCmdList)));
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer.resource;
        barrier.Transition.Subresource = 0;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        copyCmdList->ResourceBarrier(1, &barrier);
        copyCmdList->CopyBufferRegion(buffer.resource, offset, stagingResource, 0, size);
        buffer.subresourceStates[0] = D3D12_RESOURCE_STATE_COPY_DEST;
        ID3D12CommandList *cmdList = copyCmdList;
        CHECK_DX(copyCmdList->Close());
        state.queue->ExecuteCommandLists(1, &cmdList);
        state.Wait();
        copyCmdList->Release();
        stagingResource->Release();
        state.largeCopyCmdListAllocator->Reset();
        return false;
    }
    virtual void SetDataAsync(int offset, void *data, int size) override
    {
        SetDataImpl(offset, data, size);
    }
    virtual void SetData(int offset, void *data, int size) override
    {
        if (!SetDataImpl(offset, data, size))
            RendererState::Get().Wait();
    }
    virtual void SetData(void *data, int size) override
    {
        SetData(0, data, size);
    }
    virtual void GetData(void *resultBuffer, int offset, int size) override
    {
        auto &state = RendererState::Get();
        if (size == 0)
            return;
        std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
        ID3D12Resource *stagingResource =
            state.CreateBufferResource(size, D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
        ID3D12GraphicsCommandList *copyCmdList;
        CHECK_DX(state.device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr, IID_PPV_ARGS(&copyCmdList)));
        copyCmdList->CopyBufferRegion(stagingResource, 0, buffer.resource, offset, size);
        CHECK_DX(copyCmdList->Close());
        ID3D12CommandList *cmdList = copyCmdList;
        state.queue->ExecuteCommandLists(1, &cmdList);
        state.Wait();
        copyCmdList->Release();
        state.largeCopyCmdListAllocator->Reset();
        D3D12_RANGE mapRange;
        mapRange.Begin = 0;
        mapRange.End = size;
        void *stagingBufferPtr;
        CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
        memcpy(resultBuffer, stagingBufferPtr, size);
        mapRange.End = 0;
        stagingResource->Unmap(0, &mapRange);
        stagingResource->Release();
    }
    virtual int GetSize() override
    {
        return bufferSize;
    }
    virtual void *Map(int offset, int size) override
    {
        CORELIB_ASSERT(isMappable);
        CORELIB_ASSERT(mappedStart == -1 && "Nested calls to Buffer::Map() not allowed.");
        D3D12_RANGE range;
        range.Begin = (SIZE_T)offset;
        range.End = (SIZE_T)size;
        void *mappedPtr = nullptr;
        CHECK_DX(buffer.resource->Map(0, &range, &mappedPtr));
        mappedStart = offset;
        mappedEnd = offset + size;
        return mappedPtr;
    }
    virtual void *Map() override
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
        void *mappedPtr = nullptr;
        CHECK_DX(buffer.resource->Map(0, &range, &mappedPtr));
        buffer.resource->Unmap(0, &range);
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
        buffer.resource->Unmap(0, &range);
    }
};

DXGI_FORMAT TranslateStorageFormat(StorageFormat format)
{
    switch (format)
    {
    case StorageFormat::R_F16:
        return DXGI_FORMAT_R16_FLOAT;
    case StorageFormat::R_F32:
        return DXGI_FORMAT_R32_FLOAT;
    case StorageFormat::R_I8:
        return DXGI_FORMAT_R8_UINT;
    case StorageFormat::R_I16:
        return DXGI_FORMAT_R16_UINT;
    case StorageFormat::R_8:
        return DXGI_FORMAT_R8_UNORM;
    case StorageFormat::R_16:
        return DXGI_FORMAT_R16_UNORM;
    case StorageFormat::Int32_Raw:
        return DXGI_FORMAT_R32_UINT;
    case StorageFormat::RG_F16:
        return DXGI_FORMAT_R16G16_FLOAT;
    case StorageFormat::RG_F32:
        return DXGI_FORMAT_R32G32_FLOAT;
    case StorageFormat::RG_I8:
        return DXGI_FORMAT_R8G8_UINT;
    case StorageFormat::RG_8:
        return DXGI_FORMAT_R8G8_UNORM;
    case StorageFormat::RG_16:
        return DXGI_FORMAT_R16G16_UNORM;
    case StorageFormat::RG_I16:
        return DXGI_FORMAT_R16G16_UINT;
    case StorageFormat::RG_I32_Raw:
        return DXGI_FORMAT_R32G32_UINT;
    case StorageFormat::RGBA_F16:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case StorageFormat::RGBA_F32:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case StorageFormat::RGBA_8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case StorageFormat::RGBA_8_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case StorageFormat::RGBA_16:
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    case StorageFormat::RGBA_I8:
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case StorageFormat::RGBA_I16:
        return DXGI_FORMAT_R16G16B16A16_UINT;
    case StorageFormat::RGBA_I32_Raw:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case StorageFormat::BC1:
        return DXGI_FORMAT_BC1_UNORM;
    case StorageFormat::BC1_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    case StorageFormat::BC5:
        return DXGI_FORMAT_BC5_UNORM;
    case StorageFormat::BC3:
        return DXGI_FORMAT_BC3_UNORM;
    case StorageFormat::BC6H:
        return DXGI_FORMAT_BC6H_UF16;
    case StorageFormat::RGBA_Compressed:
        return DXGI_FORMAT_BC7_UNORM;
    case StorageFormat::R11F_G11F_B10F:
        return DXGI_FORMAT_R11G11B10_FLOAT;
    case StorageFormat::RGB10_A2:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case StorageFormat::Depth24:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case StorageFormat::Depth32:
        return DXGI_FORMAT_R32_FLOAT;
    case StorageFormat::Depth24Stencil8:
        return DXGI_FORMAT_R24G8_TYPELESS;
    default:
        CORELIB_NOT_IMPLEMENTED("TranslateStorageFormat");
    }
}

DXGI_FORMAT TranslateTypelessFormat(StorageFormat format)
{
    switch (format)
    {
    case StorageFormat::Depth24:
    case StorageFormat::Depth24Stencil8:
        return DXGI_FORMAT_R24G8_TYPELESS;
    case StorageFormat::Depth32:
        return DXGI_FORMAT_R32_TYPELESS;
    default:
        CORELIB_NOT_IMPLEMENTED("TranslateDepthFormat");
    }
}

DXGI_FORMAT TranslateDepthFormat(StorageFormat format)
{
    switch (format)
    {
    case StorageFormat::Depth24:
    case StorageFormat::Depth24Stencil8:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case StorageFormat::Depth32:
        return DXGI_FORMAT_D32_FLOAT;
    default:
        CORELIB_NOT_IMPLEMENTED("TranslateDepthFormat");
    }
}

int GetResourceSize(StorageFormat format, int width, int height)
{
    switch (format)
    {
    case StorageFormat::R_I16:
    case StorageFormat::R_F16:
    case StorageFormat::R_16:
    case StorageFormat::RG_8:
        return DXGI_FORMAT_R8G8_UNORM;
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
    case StorageFormat::BC5:
        return DXGI_FORMAT_BC5_UNORM;
    case StorageFormat::BC3:
        return DXGI_FORMAT_BC3_UNORM;
    case StorageFormat::BC6H:
        return DXGI_FORMAT_BC6H_UF16;
    case StorageFormat::RGBA_Compressed:
        return ((width + 3) / 4) * ((height + 3) / 4) * 16;
    default:
        CORELIB_NOT_IMPLEMENTED("GetResourceSize");
    }
}

class ResourceBarrier
{
public:
    static D3D12_RESOURCE_BARRIER Transition(
        ID3D12Resource *resource, unsigned subresourceID, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
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

class D3DTexture : public D3DResource
{
public:
    struct TextureProperties
    {
        StorageFormat format;
        TextureUsage usage;
        DXGI_FORMAT d3dformat;
        D3D12_RESOURCE_DIMENSION dimension;
        D3D12_SRV_DIMENSION defaultViewDimension;
        int width = 0;
        int height = 0;
        int depth = 0;
        int mipLevels = 0;
        int arraySize = 0;
    };

    TextureProperties properties;

    D3DTexture()
    {
    }

    ~D3DTexture()
    {
        if (resource)
            resource->Release();
    }

    void BuildMipmaps()
    {
    }

public:
    static ID3D12Resource *CreateTextureResource(D3D12_HEAP_TYPE heapType, int width, int height, int arraySize,
        int mipLevel, DXGI_FORMAT format, D3D12_RESOURCE_DIMENSION resourceDimension, D3D12_RESOURCE_STATES resState,
        D3D12_RESOURCE_FLAGS flags, bool isDepth, DXGI_FORMAT depthFormat)
    {
        auto &state = RendererState::Get();
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
        resourceDesc.Flags = flags;
        ID3D12Resource *resultResource;
        D3D12_CLEAR_VALUE optimalClearValue = {};
        if (isDepth)
        {
            optimalClearValue.Format = depthFormat;
            optimalClearValue.DepthStencil.Depth = 1.0f;
        }
        else
        {
            optimalClearValue.Format = format;
        }
        bool needClearValue =
            (flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) != 0;
        CHECK_DX(state.device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, resState,
            needClearValue ? &optimalClearValue : nullptr, IID_PPV_ARGS(&resultResource)));
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
        DataType inputType, void *data, D3D12_RESOURCE_STATES newState)
    {
        CORELIB_ASSERT((properties.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && layer == 0) ||
                       (properties.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && zOffset == 0));

        auto packedResourceSize = GetResourceSize(properties.format, width, height);
        // Half float translation
        CoreLib::List<unsigned short> translatedBuffer;
        if (data)
        {
            int dataTypeSize = DataTypeSize(inputType);
            if ((properties.format == StorageFormat::R_F16 || properties.format == StorageFormat::RG_F16 ||
                    properties.format == StorageFormat::RGBA_F16) &&
                (GetDataTypeElementType(inputType) != DataType::Half))
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
                float *src = (float *)data;
                for (int i = 0; i < translatedBuffer.Count(); i++)
                    translatedBuffer[i] = CoreLib::FloatToHalf(src[i]);
                dataTypeSize >>= 1;
                data = (void *)translatedBuffer.Buffer();
            }
        }
        // Copy data
        int slices = 1;
        if (properties.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            slices = depth;
        auto &state = RendererState::Get();
        D3D12_RESOURCE_DESC desc = resource->GetDesc();
        uint32_t numRows;
        uint64_t rowSize, totalSize;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footPrint;
        state.device->GetCopyableFootprints(&desc, mipLevel, 1, 0, &footPrint, &numRows, &rowSize, &totalSize);
        auto stagingResource =
            state.CreateBufferResource(totalSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
        std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
        unsigned subresourceId = layer * properties.mipLevels + mipLevel;
        D3D12_RANGE mapRange;
        mapRange.Begin = 0;
        mapRange.End = totalSize;
        char *stagingBufferPtr;
        ID3D12GraphicsCommandList *copyCmdList;
        CHECK_DX(stagingResource->Map(0, &mapRange, (void **)&stagingBufferPtr));
        for (int i = 0; i < slices; i++)
        {
            if (data)
            {
                if (totalSize == packedResourceSize)
                {
                    memcpy(stagingBufferPtr, data, totalSize);
                }
                else
                {
                    for (unsigned row = 0; row < numRows; row++)
                    {
                        memcpy((char *)stagingBufferPtr + footPrint.Footprint.RowPitch * row,
                            (char *)data + rowSize * row, rowSize);
                    }
                }
                data = (char *)data + packedResourceSize;
            }
            else
            {
                memset(stagingBufferPtr, 0, totalSize);
            }
            stagingBufferPtr += footPrint.Footprint.RowPitch * numRows;
        }
        stagingResource->Unmap(0, &mapRange);

        CHECK_DX(state.device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr, IID_PPV_ARGS(&copyCmdList)));
        if (subresourceStates[subresourceId] != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            D3D12_RESOURCE_BARRIER preCopyBarrier = ResourceBarrier::Transition(
                resource, subresourceId, subresourceStates[subresourceId], D3D12_RESOURCE_STATE_COPY_DEST);
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
            D3D12_RESOURCE_BARRIER postCopyBarrier =
                ResourceBarrier::Transition(resource, subresourceId, D3D12_RESOURCE_STATE_COPY_DEST, newState);
            copyCmdList->ResourceBarrier(1, &postCopyBarrier);
            subresourceStates[subresourceId] = newState;
        }
        CHECK_DX(copyCmdList->Close());
        ID3D12CommandList *cmdList = copyCmdList;
        state.queue->ExecuteCommandLists(1, &cmdList);
        state.Wait();
        state.largeCopyCmdListAllocator->Reset();
        copyCmdList->Reset(state.largeCopyCmdListAllocator, nullptr);
        copyCmdList->Release();
        stagingResource->Release();
    }
    void GetData(
        int mipLevel, int layer, int xOffset, int yOffset, int width, int height, int depth, void *data, int bufferSize)
    {
        auto &state = RendererState::Get();
        unsigned subresourceId = layer * properties.mipLevels + mipLevel;
        int rowSize = width * StorageFormatSize(properties.format);
        int rowPitch = Align(rowSize, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        auto resourceSize = height * rowPitch;
        auto stagingResource =
            state.CreateBufferResource(resourceSize, D3D12_HEAP_TYPE_READBACK, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);

        CORELIB_ASSERT(rowSize * height == bufferSize && "Buffer size must match resource size.");

        ID3D12GraphicsCommandList *copyCmdList;
        CHECK_DX(state.device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, state.largeCopyCmdListAllocator, nullptr, IID_PPV_ARGS(&copyCmdList)));
        if (subresourceStates[subresourceId] != D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            D3D12_RESOURCE_BARRIER preCopyBarrier = ResourceBarrier::Transition(
                resource, subresourceId, subresourceStates[subresourceId], D3D12_RESOURCE_STATE_COPY_SOURCE);
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
        ID3D12CommandList *cmdList = copyCmdList;
        state.queue->ExecuteCommandLists(1, &cmdList);
        state.Wait();
        std::lock_guard<std::mutex> lock(state.largeCopyCmdListMutex);
        D3D12_RANGE mapRange;
        mapRange.Begin = 0;
        mapRange.End = resourceSize;
        void *stagingBufferPtr;
        CHECK_DX(stagingResource->Map(0, &mapRange, &stagingBufferPtr));
        if (rowSize != rowPitch)
        {
            for (int i = 0; i < height; i++)
                memcpy((char *)data + i * rowSize, (char *)stagingBufferPtr + rowPitch * i, rowSize);
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

    D3D12_RESOURCE_FLAGS GetResourceFlags(TextureUsage usage)
    {
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if ((int)usage & (int)TextureUsage::Storage)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if ((int)usage & (int)TextureUsage::ColorAttachment)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if ((int)usage & (int)TextureUsage::DepthAttachment)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if ((int)usage & (int)TextureUsage::StencilAttachment)
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        return flags;
    }

    void InitTexture(int width, int height, int layers, int depth, int mipLevels, StorageFormat format,
        TextureUsage usage, D3D12_RESOURCE_DIMENSION dimension, D3D12_SRV_DIMENSION defaultViewDimension,
        D3D12_RESOURCE_STATES initialState)
    {
        bool isDepth = ((int)usage & (int)TextureUsage::DepthAttachment) != 0;
        if ((int)format >= (int)StorageFormat::RGBA_Compressed)
        {
            width = ((width + 3) >> 2) << 2;
            height = ((height + 3) >> 2) << 2;
        }
        properties.width = width;
        properties.height = height;
        properties.depth = depth;
        properties.format = format;
        properties.arraySize = layers;
        properties.mipLevels = mipLevels;
        properties.dimension = dimension;
        properties.usage = usage;
        properties.defaultViewDimension = defaultViewDimension;
        properties.d3dformat = isDepth ? TranslateTypelessFormat(format) : TranslateStorageFormat(format);
        int arraySizeOrDepth = dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ? layers : depth;
        resource = CreateTextureResource(D3D12_HEAP_TYPE_DEFAULT, width, height, arraySizeOrDepth, properties.mipLevels,
            properties.d3dformat, dimension, initialState, GetResourceFlags(usage), isDepth,
            isDepth ? TranslateDepthFormat(format) : properties.d3dformat);
        subresourceStates.SetSize(properties.mipLevels * properties.arraySize);
        for (auto &s : subresourceStates)
            s = initialState;
    }
    void InitTexture2DFromData(
        int width, int height, TextureUsage usage, StorageFormat format, DataType inputType, void *data)
    {
        auto mipLevels = CoreLib::Math::Log2Ceil(CoreLib::Math::Max(width, height)) + 1;

        InitTexture(width, height, 1, 1, mipLevels, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_RESOURCE_STATE_COPY_DEST);

        // Copy data
        SetData(
            0, 0, 0, 0, 0, properties.width, properties.height, 1, inputType, data, TextureUsageToInitialState(usage));

        // Build mipmaps
        BuildMipmaps();
    }
};

class Texture2DProxy : public GameEngine::Texture2D
{
public:
    D3DTexture texture;

public:
    Texture2DProxy(ID3D12Resource *resource, D3D12_RESOURCE_STATES state)
    {
        texture.resource = resource;
        texture.subresourceStates.Add(state);
    }
    ~Texture2DProxy()
    {
        texture.resource = nullptr;
    }
    virtual void GetSize(int & /*width*/, int & /*height*/) override
    {
        CORELIB_UNREACHABLE("unreachable");
    }
    virtual void SetData(
        int /*level*/, int /*width*/, int /*height*/, int /*samples*/, DataType /*inputType*/, void * /*data*/) override
    {
        CORELIB_UNREACHABLE("unreachable");
    }
    virtual void SetData(
        int /*width*/, int /*height*/, int /*samples*/, DataType /*inputType*/, void * /*data*/) override
    {
        CORELIB_UNREACHABLE("unreachable");
    }
    virtual void GetData(int /*mipLevel*/, void * /*data*/, int /*bufSize*/) override
    {
        CORELIB_UNREACHABLE("unreachable");
    }
    virtual void BuildMipmaps() override
    {
        CORELIB_UNREACHABLE("unreachable");
    }
    virtual bool IsDepthStencilFormat() override
    {
        auto format = texture.properties.format;
        return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 ||
               format == StorageFormat::Depth32;
    }
    virtual void *GetInternalPtr() override
    {
        return &texture;
    }
};

class Texture2D : public GameEngine::Texture2D
{
public:
    D3DTexture texture;

public:
    virtual void GetSize(int &width, int &height) override
    {
        width = texture.properties.width;
        height = texture.properties.height;
    }
    virtual void SetData(int level, int width, int height, int /*samples*/, DataType inputType, void *data) override
    {
        CORELIB_ASSERT(width == CoreLib::Math::Max(1, (texture.properties.width >> level)));
        CORELIB_ASSERT(height == CoreLib::Math::Max(1, (texture.properties.height >> level)));
        texture.SetData(level, 0, 0, 0, 0, width, height, 0, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    virtual void SetData(int width, int height, int /*samples*/, DataType inputType, void *data) override
    {
        CORELIB_ASSERT(width == texture.properties.width);
        CORELIB_ASSERT(height == texture.properties.height);
        texture.SetData(0, 0, 0, 0, 0, width, height, 0, inputType, data, D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    virtual void GetData(int mipLevel, void *data, int bufSize) override
    {
        texture.GetData(mipLevel, 0, 0, 0, texture.properties.width, texture.properties.height, 0, data, bufSize);
    }
    virtual void BuildMipmaps() override
    {
        texture.BuildMipmaps();
    }
    virtual bool IsDepthStencilFormat() override
    {
        auto format = texture.properties.format;
        return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 ||
               format == StorageFormat::Depth32;
    }
    virtual void *GetInternalPtr() override
    {
        return &texture;
    }
};

class Texture2DArray : public GameEngine::Texture2DArray
{
public:
    D3DTexture texture;
    Texture2DArray()
    {
    }

public:
    virtual void GetSize(int &width, int &height, int &layers) override
    {
        width = texture.properties.width;
        height = texture.properties.height;
        layers = texture.properties.arraySize;
    }
    virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int width, int height, int layerCount,
        DataType inputType, void *data) override
    {
        CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.width >> mipLevel) == width);
        CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.height >> mipLevel) == height);

        texture.SetData(mipLevel, layerOffset, xOffset, yOffset, 0, width, height, layerCount, inputType, data,
            D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    virtual void BuildMipmaps() override
    {
        texture.BuildMipmaps();
    }
    virtual bool IsDepthStencilFormat() override
    {
        auto format = texture.properties.format;
        return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 ||
               format == StorageFormat::Depth32;
    }
    virtual void *GetInternalPtr() override
    {
        return &texture;
    }
};

class Texture3D : public GameEngine::Texture3D
{
public:
    D3DTexture texture;
    Texture3D()
    {
    }

public:
    virtual void GetSize(int &width, int &height, int &depth) override
    {
        width = texture.properties.width;
        height = texture.properties.height;
        depth = texture.properties.depth;
    }
    virtual void SetData(int mipLevel, int xOffset, int yOffset, int zOffset, int width, int height, int depth,
        DataType inputType, void *data) override
    {
        texture.SetData(mipLevel, 0, xOffset, yOffset, zOffset, width, height, depth, inputType, data,
            D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    virtual bool IsDepthStencilFormat() override
    {
        auto format = texture.properties.format;
        return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 ||
               format == StorageFormat::Depth32;
    }
    virtual void *GetInternalPtr() override
    {
        return &texture;
    }
};

class TextureCube : public GameEngine::TextureCube
{
public:
    D3DTexture texture;
    TextureCube()
    {
    }

public:
    virtual void GetSize(int &size) override
    {
        size = texture.properties.width;
    }
    virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int width, int height, int layerCount,
        DataType inputType, void *data) override
    {
        CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.width >> mipLevel) == width);
        CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.height >> mipLevel) == height);

        texture.SetData(mipLevel, layerOffset, xOffset, yOffset, 0, width, height, layerCount, inputType, data,
            D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    virtual bool IsDepthStencilFormat() override
    {
        auto format = texture.properties.format;
        return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 ||
               format == StorageFormat::Depth32;
    }
    virtual void *GetInternalPtr() override
    {
        return &texture;
    }
};

class TextureCubeArray : public GameEngine::TextureCubeArray
{
public:
    D3DTexture texture;
    TextureCubeArray()
    {
    }

public:
    virtual void GetSize(int &size, int &layerCount) override
    {
        size = texture.properties.width;
        layerCount = texture.properties.arraySize / 6;
    }
    virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int width, int height, int layerCount,
        DataType inputType, void *data) override
    {
        CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.width >> mipLevel) == width);
        CORELIB_ASSERT(CoreLib::Math::Max(1, texture.properties.height >> mipLevel) == height);

        texture.SetData(mipLevel, layerOffset, xOffset, yOffset, 0, width, height, layerCount, inputType, data,
            D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    virtual bool IsDepthStencilFormat() override
    {
        auto format = texture.properties.format;
        return format == StorageFormat::Depth24 || format == StorageFormat::Depth24Stencil8 ||
               format == StorageFormat::Depth32;
    }
    virtual void *GetInternalPtr() override
    {
        return &texture;
    }
};

class TextureSampler : public GameEngine::TextureSampler
{
public:
    TextureFilter textureFilter = TextureFilter::Linear;
    WrapMode wrapMode = WrapMode::Repeat;
    CompareFunc compareFunc = CompareFunc::Disabled;
    D3D12_SAMPLER_DESC desc = {};
    TextureSampler()
    {
        desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        desc.MipLODBias = 0.0f;
        desc.MinLOD = 0;
        desc.MaxLOD = 25;
        desc.MaxAnisotropy = 16;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }
    void UpdateDescFilter()
    {
        desc.MaxLOD = 25;
        if (compareFunc != CompareFunc::Disabled)
        {
            switch (textureFilter)
            {
            case TextureFilter::Nearest:
                desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
                desc.MaxLOD = 0;
                break;
            case TextureFilter::Linear:
                desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
                desc.MaxLOD = 0;
                break;
            case TextureFilter::Trilinear:
                desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
                break;
            case TextureFilter::Anisotropic4x:
                desc.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
                desc.MaxAnisotropy = 4;
                break;
            case TextureFilter::Anisotropic8x:
                desc.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
                desc.MaxAnisotropy = 8;
                break;
            case TextureFilter::Anisotropic16x:
                desc.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
                desc.MaxAnisotropy = 16;
                break;
            }
            return;
        }
        switch (textureFilter)
        {
        case TextureFilter::Nearest:
            desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            desc.MaxLOD = 0;
            break;
        case TextureFilter::Linear:
            desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            desc.MaxLOD = 0;
            break;
        case TextureFilter::Trilinear:
            desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            break;
        case TextureFilter::Anisotropic4x:
            desc.Filter = D3D12_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = 4;
            break;
        case TextureFilter::Anisotropic8x:
            desc.Filter = D3D12_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = 8;
            break;
        case TextureFilter::Anisotropic16x:
            desc.Filter = D3D12_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = 16;
            break;
        }
    }

public:
    virtual TextureFilter GetFilter() override
    {
        return textureFilter;
    }
    virtual void SetFilter(TextureFilter filter) override
    {
        textureFilter = filter;
        UpdateDescFilter();
    }
    virtual WrapMode GetWrapMode() override
    {
        return wrapMode;
    }
    virtual void SetWrapMode(WrapMode mode) override
    {
        wrapMode = mode;
        switch (mode)
        {
        case WrapMode::Clamp:
            desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            break;
        case WrapMode::Mirror:
            desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
            break;
        case WrapMode::Repeat:
            desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            break;
        }
    }
    virtual CompareFunc GetCompareFunc() override
    {
        return compareFunc;
    }

    virtual void SetDepthCompare(CompareFunc op) override
    {
        compareFunc = op;
        desc.ComparisonFunc = TranslateCompareFunc(op);
        UpdateDescFilter();
    }
};

class Shader : public GameEngine::Shader
{
public:
    List<char> shaderData;
    ShaderType stage;
    Shader(ShaderType stage, const char *data, int size)
    {
        this->stage = stage;
        shaderData.AddRange(data, size);
    };
};

class FrameBuffer : public GameEngine::FrameBuffer
{
public:
    RenderAttachments renderAttachments;
    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
    D3DTexture *depthStencilTexture = nullptr;
    D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDescs[8];
    D3DTexture *renderTargetTextures[8] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    int rtvCount = 0;
    int dsvCount = 0;
    FrameBuffer(const RenderAttachments &attachments) : renderAttachments(attachments)
    {
    }

    ~FrameBuffer()
    {
        auto &state = RendererState::Get();
        for (int i = 0; i < rtvCount; i++)
            state.rtvDescHeap.Free(rtvHandles[i], 1);
        if (dsvCount)
            state.dsvDescHeap.Free(dsvHandle, 1);
    }

public:
    virtual RenderAttachments &GetRenderAttachments() override
    {
        return renderAttachments;
    }
};

struct SingleQueueFence
{
    ID3D12Fence *fence = nullptr;
    HANDLE waitEvent;
    SingleQueueFence()
    {
        CHECK_DX(RendererState::Get().device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        waitEvent = CreateEventW(nullptr, 1, 0, nullptr);
    }
    ~SingleQueueFence()
    {
        fence->Release();
        CloseHandle(waitEvent);
    }
    void Reset()
    {
        ResetEvent(waitEvent);
    }
    void Wait()
    {
        WaitForSingleObject(waitEvent, INFINITE);
    }
};

class Fence : public GameEngine::Fence
{
public:
    SingleQueueFence direct, copy;

public:
    virtual void Reset() override
    {
        direct.Reset();
        copy.Reset();
    }
    virtual void Wait() override
    {
        HANDLE events[2] = {direct.waitEvent, copy.waitEvent};
        WaitForMultipleObjects(2, events, TRUE, INFINITE);
    }
};

class RenderTargetLayout : public GameEngine::RenderTargetLayout
{
public:
    List<AttachmentLayout> attachmentLayouts;
    bool clearAttachments = false;
    RenderTargetLayout(CoreLib::ArrayView<AttachmentLayout> bindings, bool clear)
    {
        attachmentLayouts.AddRange(bindings);
        clearAttachments = clear;
    }

public:
    virtual GameEngine::FrameBuffer *CreateFrameBuffer(const RenderAttachments &attachments) override
    {
        auto framebuffer = new FrameBuffer(attachments);
        auto &state = RendererState::Get();
        CORELIB_ASSERT(attachments.attachments.Count() == attachmentLayouts.Count());

        int &rtvCount = framebuffer->rtvCount;
        for (int i = 0; i < attachmentLayouts.Count(); i++)
        {
            auto &renderTarget = attachmentLayouts[i];
            auto &attachment = attachments.attachments[i];
            if (renderTarget.Usage == TextureUsage::ColorAttachment ||
                renderTarget.Usage == TextureUsage::SampledColorAttachment)
            {
                auto &rtv = framebuffer->renderTargetViewDescs[rtvCount];
                rtv.Format = TranslateStorageFormat(renderTarget.ImageFormat);
                D3DTexture *d3dtexture = nullptr;
                if (attachment.handle.tex2D)
                {
                    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                    rtv.Texture2D.MipSlice = attachment.level;
                    rtv.Texture2D.PlaneSlice = 0;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.tex2D->GetInternalPtr());
                }
                else if (attachment.handle.tex2DArray)
                {
                    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtv.Texture2DArray.MipSlice = attachment.level;
                    rtv.Texture2DArray.FirstArraySlice = attachment.layer;
                    rtv.Texture2DArray.ArraySize = 1;
                    rtv.Texture2DArray.PlaneSlice = 0;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.tex2DArray->GetInternalPtr());
                }
                else if (attachment.handle.texCube)
                {
                    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtv.Texture2DArray.MipSlice = attachment.level;
                    rtv.Texture2DArray.FirstArraySlice = (int)attachment.face;
                    rtv.Texture2DArray.ArraySize = 1;
                    rtv.Texture2DArray.PlaneSlice = 0;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.texCube->GetInternalPtr());
                }
                else if (attachment.handle.texCubeArray)
                {
                    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtv.Texture2DArray.MipSlice = attachment.level;
                    rtv.Texture2DArray.FirstArraySlice = (int)attachment.face + attachment.layer * 6;
                    rtv.Texture2DArray.ArraySize = 1;
                    rtv.Texture2DArray.PlaneSlice = 0;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.texCubeArray->GetInternalPtr());
                }
                framebuffer->rtvHandles[rtvCount] = state.rtvDescHeap.Alloc(1).cpuHandle;
                framebuffer->renderTargetTextures[rtvCount] = d3dtexture;
                state.device->CreateRenderTargetView(d3dtexture->resource, &rtv, framebuffer->rtvHandles[rtvCount]);
                rtvCount++;
            }
            else
            {
                auto &dsv = framebuffer->depthStencilViewDesc;
                CORELIB_ASSERT(framebuffer->dsvCount == 0);
                framebuffer->dsvCount++;
                dsv.Flags = D3D12_DSV_FLAG_NONE;
                dsv.Format = TranslateDepthFormat(renderTarget.ImageFormat);
                D3DTexture *d3dtexture = nullptr;
                if (attachment.handle.tex2D)
                {
                    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                    dsv.Texture2D.MipSlice = attachment.level;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.tex2D->GetInternalPtr());
                }
                else if (attachment.handle.tex2DArray)
                {
                    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsv.Texture2DArray.MipSlice = attachment.level;
                    dsv.Texture2DArray.FirstArraySlice = attachment.layer;
                    dsv.Texture2DArray.ArraySize = 1;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.tex2DArray->GetInternalPtr());
                }
                else if (attachment.handle.texCube)
                {
                    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsv.Texture2DArray.MipSlice = attachment.level;
                    dsv.Texture2DArray.FirstArraySlice = (int)attachment.face;
                    dsv.Texture2DArray.ArraySize = 1;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.texCube->GetInternalPtr());
                }
                else if (attachment.handle.texCubeArray)
                {
                    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsv.Texture2DArray.MipSlice = attachment.level;
                    dsv.Texture2DArray.FirstArraySlice = (int)attachment.face + attachment.layer * 6;
                    dsv.Texture2DArray.ArraySize = 1;
                    d3dtexture = static_cast<D3DTexture *>(attachment.handle.texCubeArray->GetInternalPtr());
                }
                framebuffer->dsvHandle = state.dsvDescHeap.Alloc(1).cpuHandle;
                framebuffer->depthStencilTexture = d3dtexture;
                state.device->CreateDepthStencilView(d3dtexture->resource, &dsv, framebuffer->dsvHandle);
            }
        }
        return framebuffer;
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

struct ResourceUse
{
    D3DResource *resource;
    int subresourceId;
    D3D12_RESOURCE_STATES targetState;
};

class DescriptorSet : public GameEngine::DescriptorSet
{
public:
    DescriptorAddress resourceAddr, samplerAddr;
    int resourceCount, samplerCount;
    List<DescriptorNode> descriptors;
    List<List<ResourceUse>> resourceUses;

public:
    DescriptorSet(DescriptorSetLayout *layout)
    {
        auto &state = RendererState::Get();
        resourceCount = 0;
        samplerCount = 0;
        descriptors.Reserve(layout->descriptors.Count());
        resourceUses.SetSize(layout->descriptors.Count());
        for (auto &desc : layout->descriptors)
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
            case BindingType::RWStorageBuffer:
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
        for (auto &node : descriptors)
        {
            auto offset = node.address.cpuHandle.ptr;
            // Fill in true address
            if (node.layout.Type == BindingType::Sampler)
            {
                node.address.cpuHandle.ptr =
                    samplerAddr.cpuHandle.ptr + offset * state.samplerDescHeap.handleIncrementSize;
                node.address.gpuHandle.ptr =
                    samplerAddr.gpuHandle.ptr + offset * state.samplerDescHeap.handleIncrementSize;
            }
            else
            {
                node.address.cpuHandle.ptr =
                    resourceAddr.cpuHandle.ptr + offset * state.resourceDescHeap.handleIncrementSize;
                node.address.gpuHandle.ptr =
                    resourceAddr.gpuHandle.ptr + offset * state.resourceDescHeap.handleIncrementSize;
            }
        }
    }
    ~DescriptorSet()
    {
        auto &state = RendererState::Get();
        state.resourceDescHeap.Free(resourceAddr, resourceCount);
        state.samplerDescHeap.Free(samplerAddr, samplerCount);
    }
    void CreateDescriptorFromTexture(
        GameEngine::Texture *texture, TextureAspect aspect, D3D12_CPU_DESCRIPTOR_HANDLE descHandle)
    {
        auto &state = RendererState::Get();
        auto d3dTexture = static_cast<D3DTexture *>(texture->GetInternalPtr());
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = d3dTexture->properties.defaultViewDimension;
        desc.Format = d3dTexture->properties.d3dformat;
        UINT planeSlice = 0;
        if (isDepthFormat(d3dTexture->properties.format))
            desc.Format = TranslateStorageFormat(d3dTexture->properties.format);
        if (aspect == TextureAspect::Stencil && d3dTexture->properties.format == StorageFormat::Depth24Stencil8)
            planeSlice = 1;
        if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
        {
            desc.Texture2D.MipLevels = d3dTexture->properties.mipLevels;
            desc.Texture2D.ResourceMinLODClamp = 0.0f;
            desc.Texture2D.PlaneSlice = planeSlice;
            desc.Texture2D.MostDetailedMip = 0;
        }
        else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
        {
            desc.Texture2DArray.MipLevels = d3dTexture->properties.mipLevels;
            desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
            desc.Texture2DArray.PlaneSlice = planeSlice;
            desc.Texture2DArray.MostDetailedMip = 0;
            desc.Texture2DArray.ArraySize = d3dTexture->properties.arraySize;
            desc.Texture2DArray.FirstArraySlice = 0;
        }
        else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
        {
            desc.TextureCube.MipLevels = d3dTexture->properties.mipLevels;
            desc.TextureCube.ResourceMinLODClamp = 0.0f;
            desc.TextureCube.MostDetailedMip = 0;
        }
        else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY)
        {
            desc.TextureCubeArray.MipLevels = d3dTexture->properties.mipLevels;
            desc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
            desc.TextureCubeArray.MostDetailedMip = 0;
            desc.TextureCubeArray.First2DArrayFace = 0;
            desc.TextureCubeArray.NumCubes = d3dTexture->properties.arraySize / 6;
        }
        else if (desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE3D)
        {
            desc.Texture3D.MipLevels = d3dTexture->properties.mipLevels;
            desc.Texture3D.ResourceMinLODClamp = 0.0f;
            desc.Texture3D.MostDetailedMip = 0;
        }

        state.device->CreateShaderResourceView(d3dTexture->resource, &desc, descHandle);
    }

public:
    virtual void BeginUpdate() override
    {
    }
    virtual void Update(int location, GameEngine::Texture *texture, TextureAspect aspect) override
    {
        CreateDescriptorFromTexture(texture, aspect, descriptors[location].address.cpuHandle);
        resourceUses[location].Clear();

        auto d3dtexture = static_cast<D3DTexture *>(texture->GetInternalPtr());
        if (d3dtexture->properties.usage != TextureUsage::Sampled)
        {
            ResourceUse resourceUse;
            resourceUse.subresourceId = -1;
            resourceUse.resource = d3dtexture;
            resourceUse.targetState =
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            resourceUses[location].Add(resourceUse);
        }
    }
    virtual void Update(int location, CoreLib::ArrayView<GameEngine::Texture *> textures, TextureAspect aspect) override
    {
        auto &state = RendererState::Get();
        resourceUses[location].Clear();
        for (int i = 0; i < textures.Count(); i++)
        {
            auto descAddr = descriptors[location].address.cpuHandle;
            descAddr.ptr += state.resourceDescHeap.handleIncrementSize * i;
            auto d3dtexture = static_cast<D3DTexture *>(textures[i]->GetInternalPtr());
            if (d3dtexture->properties.usage != TextureUsage::Sampled)
            {
                ResourceUse resourceUse;
                resourceUse.subresourceId = -1;
                resourceUse.targetState =
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                resourceUse.resource = d3dtexture;
                resourceUses[location].Add(resourceUse);
            }
            CreateDescriptorFromTexture(textures[i], aspect, descAddr);
        }
    }
    virtual void UpdateStorageImage(
        int location, CoreLib::ArrayView<GameEngine::Texture *> textures, TextureAspect aspect) override
    {
        auto &state = RendererState::Get();
        resourceUses[location].Clear();
        for (int i = 0; i < textures.Count(); i++)
        {
            auto d3dTexture = static_cast<D3DTexture *>(textures[i]->GetInternalPtr());
            CORELIB_ASSERT(d3dTexture->properties.defaultViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D);
            ResourceUse resourceUse;
            resourceUse.subresourceId = -1;
            resourceUse.targetState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            resourceUse.resource = static_cast<D3DTexture *>(textures[i]->GetInternalPtr());
            resourceUses[location].Add(resourceUse);
            UINT planeSlice = 0;
            if (aspect == TextureAspect::Stencil && d3dTexture->properties.format == StorageFormat::Depth24Stencil8)
                planeSlice = 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
            desc.Texture2D.MipSlice = 0;
            desc.Texture2D.PlaneSlice = planeSlice;
            desc.Format = d3dTexture->properties.d3dformat;
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            auto descAddr = descriptors[location].address.cpuHandle;
            descAddr.ptr += state.resourceDescHeap.handleIncrementSize * i;
            state.device->CreateUnorderedAccessView(d3dTexture->resource, nullptr, &desc, descAddr);
        }
    }
    virtual void Update(int location, GameEngine::TextureSampler *sampler) override
    {
        auto &state = RendererState::Get();
        state.device->CreateSampler(
            &reinterpret_cast<TextureSampler *>(sampler)->desc, descriptors[location].address.cpuHandle);
    }
    virtual void Update(int location, GameEngine::Buffer *buffer, int offset, int length) override
    {
        auto &state = RendererState::Get();
        auto d3dbuffer = reinterpret_cast<Buffer *>(buffer);
        if (length == -1)
            length = d3dbuffer->bufferSize;
        auto bindingType = descriptors[location].layout.Type;
        resourceUses[location].Clear();
        if (bindingType == BindingType::RWStorageBuffer)
        {
            CORELIB_ASSERT(
                d3dbuffer->structInfo.StructureStride != 0 && offset % d3dbuffer->structInfo.StructureStride == 0);
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            desc.Buffer.FirstElement = offset / d3dbuffer->structInfo.StructureStride;
            desc.Buffer.StructureByteStride = d3dbuffer->structInfo.StructureStride;
            desc.Buffer.NumElements = d3dbuffer->structInfo.NumElements;
            desc.Buffer.CounterOffsetInBytes = 0;
            state.device->CreateUnorderedAccessView(
                d3dbuffer->buffer.resource, nullptr, &desc, descriptors[location].address.cpuHandle);
            ResourceUse resourceUse;
            resourceUse.resource = &d3dbuffer->buffer;
            resourceUse.subresourceId = 0;
            resourceUse.targetState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            resourceUses[location].Add(resourceUse);
        }
        else if (bindingType == BindingType::UniformBuffer)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
            desc.BufferLocation = d3dbuffer->buffer.resource->GetGPUVirtualAddress() + offset;
            desc.SizeInBytes = Align(length, D3DConstantBufferAlignment);
            state.device->CreateConstantBufferView(&desc, descriptors[location].address.cpuHandle);
            ResourceUse resourceUse;
            resourceUse.resource = &d3dbuffer->buffer;
            resourceUse.subresourceId = 0;
            resourceUse.targetState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        else if (bindingType == BindingType::StorageBuffer)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = offset / d3dbuffer->structInfo.StructureStride;
            desc.Buffer.StructureByteStride = d3dbuffer->structInfo.StructureStride;
            desc.Buffer.NumElements = d3dbuffer->structInfo.NumElements;
            state.device->CreateShaderResourceView(
                d3dbuffer->buffer.resource, &desc, descriptors[location].address.cpuHandle);
            ResourceUse resourceUse;
            resourceUse.resource = &d3dbuffer->buffer;
            resourceUse.subresourceId = 0;
            resourceUse.targetState =
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        else
        {
            CORELIB_ABORT("Unsupported buffer type.");
        }
    }
    virtual void EndUpdate() override
    {
    }
};

class Pipeline : public GameEngine::Pipeline
{
public:
    ID3D12PipelineState *pipelineState = nullptr;
    ID3D12RootSignature *rootSignature = nullptr;
    FixedFunctionPipelineStates FixedFunctionStates;
    VertexFormat vertexFormat;
    int descriptorTableCount = 0;

    Pipeline(ID3D12PipelineState *pipeline, ID3D12RootSignature *rootSig)
        : pipelineState(pipeline), rootSignature(rootSig)
    {
    }
    ~Pipeline()
    {
        if (pipelineState)
            pipelineState->Release();
        if (rootSignature)
            rootSignature->Release();
    }
};

class PipelineBuilder : public GameEngine::PipelineBuilder
{
private:
    CoreLib::String pipelineName;
    List<Shader *> d3dShaders;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
    List<D3D12_ROOT_PARAMETER> rootParams;
    List<List<D3D12_DESCRIPTOR_RANGE>> descRanges;
    D3D12_INPUT_LAYOUT_DESC inputDesc = {};
    List<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
    List<int> descSetBindingMapping; // DescriptorSet slot index -> D3D DescriptorTable Index.
    ID3D12RootSignature *rootSignature = nullptr;
    VertexFormat vertexFormat;

public:
    PipelineBuilder()
    {
    }
    ~PipelineBuilder()
    {
        if (rootSignature)
            rootSignature->Release();
    }

public:
    virtual void SetShaders(CoreLib::ArrayView<GameEngine::Shader *> shaders) override
    {
        d3dShaders.Clear();
        for (auto shader : shaders)
            d3dShaders.Add(reinterpret_cast<Shader *>(shader));
    }

    virtual void SetVertexLayout(VertexFormat format) override
    {
        this->vertexFormat = format;
        inputElementDescs.SetSize(format.Attributes.Count());
        inputDesc.NumElements = format.Attributes.Count();
        for (int i = 0; i < format.Attributes.Count(); i++)
        {
            D3D12_INPUT_ELEMENT_DESC element = {};
            element.SemanticIndex = format.Attributes[i].SemanticIndex;
            element.SemanticName = format.Attributes[i].Semantic.Buffer();
            element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            element.InputSlot = format.Attributes[i].Location;
            element.InstanceDataStepRate = 0;
            element.AlignedByteOffset = 0;
            bool normalized = format.Attributes[i].Normalized;
            switch (format.Attributes[i].Type)
            {
            case DataType::Byte:
                element.Format = normalized ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8_UINT;
                break;
            case DataType::Byte2:
                element.Format = normalized ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8_UINT;
                break;
            case DataType::Byte4:
                element.Format = normalized ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UINT;
                break;
            case DataType::Char:
                element.Format = normalized ? DXGI_FORMAT_R8_SNORM : DXGI_FORMAT_R8_SINT;
                break;
            case DataType::Char2:
                element.Format = normalized ? DXGI_FORMAT_R8G8_SNORM : DXGI_FORMAT_R8G8_SINT;
                break;
            case DataType::Char4:
                element.Format = normalized ? DXGI_FORMAT_R8G8B8A8_SNORM : DXGI_FORMAT_R8G8B8A8_SNORM;
                break;
            case DataType::Float:
                element.Format = DXGI_FORMAT_R32_FLOAT;
                break;
            case DataType::Float2:
                element.Format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case DataType::Float3:
                element.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case DataType::Float4:
                element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            case DataType::Half:
                element.Format = DXGI_FORMAT_R16_FLOAT;
                break;
            case DataType::Half2:
                element.Format = DXGI_FORMAT_R16G16_FLOAT;
                break;
            case DataType::Half4:
                element.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                break;
            case DataType::Int:
                element.Format = DXGI_FORMAT_R32_SINT;
                break;
            case DataType::Int2:
                element.Format = DXGI_FORMAT_R32G32_SINT;
                break;
            case DataType::Int3:
                element.Format = DXGI_FORMAT_R32G32B32_SINT;
                break;
            case DataType::Int4:
                element.Format = DXGI_FORMAT_R32G32B32A32_SINT;
                break;
            case DataType::UInt:
                element.Format = DXGI_FORMAT_R32_UINT;
                break;
            case DataType::UInt4_10_10_10_2:
                element.Format = normalized ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R10G10B10A2_UINT;
                break;
            case DataType::UShort:
                element.Format = normalized ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R16_UINT;
                break;
            case DataType::UShort2:
                element.Format = normalized ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16G16_UINT;
                break;
            case DataType::UShort4:
                element.Format = normalized ? DXGI_FORMAT_R16G16B16A16_UNORM : DXGI_FORMAT_R16G16B16A16_UINT;
                break;
            default:
                CORELIB_ABORT("Unsupported vertex attribute type.");
            }
            inputElementDescs[i] = element;
        }
        inputDesc.pInputElementDescs = inputElementDescs.Buffer();
    }

    void CreateRootSignature(CoreLib::ArrayView<GameEngine::DescriptorSetLayout *> descriptorSets, bool isGraphics)
    {
        if (rootSignature)
            rootSignature->Release();
        auto &state = RendererState::Get();
        descSetBindingMapping.SetSize(descriptorSets.Count());
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        if (isGraphics)
            rootDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        descRanges.Reserve(descriptorSets.Count() * 2);
        rootParams.Clear();
        descRanges.Clear();
        int regSpace = 0;
        for (int i = 0; i < descriptorSets.Count(); i++)
        {
            auto descSet = reinterpret_cast<DescriptorSetLayout *>(descriptorSets[i]);
            if (!descSet)
                continue;
            List<D3D12_DESCRIPTOR_RANGE> resourceRanges, samplerRanges;
            int cbvId = 0, srvId = 0, uavId = 0, samplerId = 0;
            for (auto &descriptor : descSet->descriptors)
            {
                D3D12_DESCRIPTOR_RANGE range;
                range.RegisterSpace = regSpace;
                switch (descriptor.Type)
                {
                case BindingType::UniformBuffer:
                    range.BaseShaderRegister = cbvId;
                    range.NumDescriptors = descriptor.ArraySize;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                    cbvId += descriptor.ArraySize;
                    resourceRanges.Add(range);
                    break;
                case BindingType::Texture:
                case BindingType::StorageBuffer:
                    range.BaseShaderRegister = srvId;
                    range.NumDescriptors = descriptor.ArraySize;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    srvId += descriptor.ArraySize;
                    resourceRanges.Add(range);
                    break;
                case BindingType::RWStorageBuffer:
                case BindingType::StorageTexture:
                    range.BaseShaderRegister = uavId;
                    range.NumDescriptors = descriptor.ArraySize;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    uavId += descriptor.ArraySize;
                    resourceRanges.Add(range);
                    break;
                case BindingType::Sampler:
                    range.BaseShaderRegister = samplerId;
                    range.NumDescriptors = descriptor.ArraySize;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    samplerId += descriptor.ArraySize;
                    samplerRanges.Add(range);
                    break;
                }
            }
            descSetBindingMapping[i] = rootParams.Count();
            if (resourceRanges.Count())
            {
                D3D12_ROOT_PARAMETER param;
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                param.DescriptorTable.NumDescriptorRanges = resourceRanges.Count();
                param.DescriptorTable.pDescriptorRanges = resourceRanges.Buffer();
                rootParams.Add(param);
                descRanges.Add(_Move(resourceRanges));
            }
            if (samplerRanges.Count())
            {
                D3D12_ROOT_PARAMETER param;
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                param.DescriptorTable.NumDescriptorRanges = samplerRanges.Count();
                param.DescriptorTable.pDescriptorRanges = samplerRanges.Buffer();
                rootParams.Add(param);
                descRanges.Add(_Move(samplerRanges));
            }
            regSpace++;
        }
        rootDesc.Desc_1_0.pParameters = rootParams.Buffer();
        rootDesc.Desc_1_0.NumParameters = rootParams.Count();
        ID3DBlob *rootSignatureBlob = nullptr;
        ID3DBlob *serializeError = nullptr;
        state.serializeVersionedRootSignature(&rootDesc, &rootSignatureBlob, &serializeError);
        if (serializeError)
        {
            auto errMsg = (char *)(serializeError->GetBufferPointer());
            CoreLib::Diagnostics::DebugWriter() << errMsg;
            CORELIB_ABORT("Root signature creation failure.");
        }
        CHECK_DX(state.device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
        rootSignatureBlob->Release();
    }

    virtual void SetBindingLayout(CoreLib::ArrayView<GameEngine::DescriptorSetLayout *> descriptorSets) override
    {
        CreateRootSignature(descriptorSets, true);
    }

    virtual void SetDebugName(CoreLib::String name) override
    {
        pipelineName = name;
    }

    virtual GameEngine::Pipeline *ToPipeline(GameEngine::RenderTargetLayout *renderTargetLayout) override
    {
        CORELIB_ASSERT(rootSignature != nullptr);
        auto &state = RendererState::Get();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignature;
        desc.InputLayout = inputDesc;
        for (auto shader : this->d3dShaders)
        {
            switch (shader->stage)
            {
            case ShaderType::VertexShader:
                desc.VS.pShaderBytecode = shader->shaderData.Buffer();
                desc.VS.BytecodeLength = shader->shaderData.Count();
                break;
            case ShaderType::FragmentShader:
                desc.PS.pShaderBytecode = shader->shaderData.Buffer();
                desc.PS.BytecodeLength = shader->shaderData.Count();
                break;
            case ShaderType::DomainShader:
                desc.DS.pShaderBytecode = shader->shaderData.Buffer();
                desc.DS.BytecodeLength = shader->shaderData.Count();
                break;
            case ShaderType::HullShader:
                desc.HS.pShaderBytecode = shader->shaderData.Buffer();
                desc.HS.BytecodeLength = shader->shaderData.Count();
                break;
            default:
                CORELIB_ABORT("Unsupported shader stage.");
            }
        }
        desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        desc.PrimitiveTopologyType = TranslateTopologyType(FixedFunctionStates.PrimitiveTopology);
        desc.IBStripCutValue = FixedFunctionStates.PrimitiveRestartEnabled
                                   ? D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF
                                   : D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        desc.DepthStencilState.DepthEnable = FixedFunctionStates.DepthCompareFunc != CompareFunc::Disabled;
        desc.DepthStencilState.DepthFunc = TranslateCompareFunc(FixedFunctionStates.DepthCompareFunc);
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.StencilEnable = FixedFunctionStates.StencilCompareFunc != CompareFunc::Disabled;
        desc.DepthStencilState.StencilReadMask = (UINT8)FixedFunctionStates.StencilMask;
        desc.DepthStencilState.StencilWriteMask = (UINT8)FixedFunctionStates.StencilMask;
        desc.DepthStencilState.FrontFace.StencilFunc = TranslateCompareFunc(FixedFunctionStates.StencilCompareFunc);
        desc.DepthStencilState.FrontFace.StencilDepthFailOp =
            TranslateStencilOp(FixedFunctionStates.StencilDepthFailOp);
        desc.DepthStencilState.FrontFace.StencilFailOp = TranslateStencilOp(FixedFunctionStates.StencilFailOp);
        desc.DepthStencilState.FrontFace.StencilPassOp = TranslateStencilOp(FixedFunctionStates.StencilDepthPassOp);
        desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
        desc.RasterizerState.ConservativeRaster = FixedFunctionStates.ConsevativeRasterization
                                                      ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
                                                      : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        desc.RasterizerState.FrontCounterClockwise = 1;
        desc.RasterizerState.CullMode = TranslateCullMode(FixedFunctionStates.cullMode);
        desc.RasterizerState.FillMode = TranslateFillMode(FixedFunctionStates.PolygonFillMode);
        desc.RasterizerState.SlopeScaledDepthBias =
            FixedFunctionStates.EnablePolygonOffset ? FixedFunctionStates.PolygonOffsetFactor : 0.0f;
        desc.RasterizerState.DepthBias =
            FixedFunctionStates.EnablePolygonOffset ? (int)FixedFunctionStates.PolygonOffsetUnits : 0;
        desc.RasterizerState.DepthClipEnable = 0;

        desc.BlendState.AlphaToCoverageEnable = 0;
        desc.BlendState.IndependentBlendEnable = 0;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;
        // Fill frame buffer format
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        desc.NumRenderTargets = 0;
        auto d3dRenderTargetLayout = reinterpret_cast<RenderTargetLayout *>(renderTargetLayout);
        for (auto &renderTarget : d3dRenderTargetLayout->attachmentLayouts)
        {
            if (renderTarget.Usage == TextureUsage::ColorAttachment ||
                renderTarget.Usage == TextureUsage::SampledColorAttachment)
            {
                desc.RTVFormats[desc.NumRenderTargets] = TranslateStorageFormat(renderTarget.ImageFormat);
                auto &rtBlendState = desc.BlendState.RenderTarget[desc.NumRenderTargets];
                rtBlendState.BlendEnable = FixedFunctionStates.blendMode != BlendMode::Replace;
                rtBlendState.BlendOp = D3D12_BLEND_OP_ADD;
                rtBlendState.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                if (FixedFunctionStates.blendMode == BlendMode::AlphaBlend)
                {
                    rtBlendState.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    rtBlendState.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    rtBlendState.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
                    rtBlendState.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                }
                else
                {
                    rtBlendState.SrcBlend = D3D12_BLEND_ONE;
                    rtBlendState.DestBlend = D3D12_BLEND_ONE;
                    rtBlendState.SrcBlendAlpha = D3D12_BLEND_ONE;
                    rtBlendState.DestBlendAlpha = D3D12_BLEND_ONE;
                }
                rtBlendState.RenderTargetWriteMask = 0xF;
                desc.NumRenderTargets++;
            }
            else
            {
                CORELIB_ASSERT(desc.DSVFormat == DXGI_FORMAT_UNKNOWN);
                desc.DSVFormat = TranslateDepthFormat(renderTarget.ImageFormat);
            }
        }
        ID3D12PipelineState *pipelineState = nullptr;
        CHECK_DX(state.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState)));
        auto pipeline = new Pipeline(pipelineState, rootSignature);
        pipeline->FixedFunctionStates = FixedFunctionStates;
        pipeline->vertexFormat = this->vertexFormat;
        rootSignature = nullptr;
        pipeline->descriptorTableCount = rootParams.Count();
        return pipeline;
    }

    virtual GameEngine::Pipeline *CreateComputePipeline(
        CoreLib::ArrayView<GameEngine::DescriptorSetLayout *> descriptorSets, GameEngine::Shader *shader) override
    {
        auto &state = RendererState::Get();
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        CreateRootSignature(descriptorSets, false);
        desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        desc.pRootSignature = rootSignature;
        auto d3dShader = reinterpret_cast<Shader *>(shader);
        desc.CS.BytecodeLength = d3dShader->shaderData.Count();
        desc.CS.pShaderBytecode = d3dShader->shaderData.Buffer();
        ID3D12PipelineState *pipelineState = nullptr;
        CHECK_DX(state.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineState)));
        auto pipeline = new Pipeline(pipelineState, rootSignature);
        rootSignature = nullptr;
        return pipeline;
    }
};

class CommandBuffer : public GameEngine::CommandBuffer
{
public:
    Buffer *vertexBuffer = nullptr;
    int vertexBufferOffset = 0;

    // Current descriptor set bindings.
    Array<DescriptorSet *, 16> descSetBindings;

    // All descriptor sets referenced in this command buffer.
    List<DescriptorSet *> descSets;

    Pipeline *currentPipeline = nullptr;
    bool pipelineChanged = false, descBindingChanged = false;
    bool vertexBufferChanged = false;

    bool isEmpty = true;
    List<ResourceUse> resourceUses;

public:
    ID3D12GraphicsCommandList *commandList;
    ID3D12CommandAllocator *commandAllocator;
    CommandBuffer(D3D12_COMMAND_LIST_TYPE type)
    {
        auto &state = RendererState::Get();
        CHECK_DX(state.device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));
        CHECK_DX(state.device->CreateCommandList(0, type, commandAllocator, nullptr, IID_PPV_ARGS(&commandList)));
        commandList->Close();
    }
    ~CommandBuffer()
    {
        commandList->Release();
        commandAllocator->Release();
    }
    void MaybeUpdatePipelineBindings()
    {
        if (pipelineChanged)
        {
            commandList->SetGraphicsRootSignature(currentPipeline->rootSignature);
            commandList->SetPipelineState(currentPipeline->pipelineState);
            commandList->OMSetStencilRef(currentPipeline->FixedFunctionStates.StencilReference);
            vertexBufferChanged = true;
            descBindingChanged = true;
            pipelineChanged = false;
        }
        if (descBindingChanged)
        {
            int rootIndex = 0;
            for (int i = 0; i < descSetBindings.Count(); i++)
            {
                if (rootIndex >= currentPipeline->descriptorTableCount)
                    break;
                if (!descSetBindings[i])
                    continue;
                if (descSetBindings[i]->resourceCount)
                {
                    commandList->SetGraphicsRootDescriptorTable(rootIndex, descSetBindings[i]->resourceAddr.gpuHandle);
                    rootIndex++;
                }
                if (descSetBindings[i]->samplerCount)
                {
                    commandList->SetGraphicsRootDescriptorTable(rootIndex, descSetBindings[i]->samplerAddr.gpuHandle);
                    rootIndex++;
                }
            }
            descBindingChanged = false;
        }
        if (vertexBufferChanged)
        {
            vertexBufferChanged = false;
            Array<D3D12_VERTEX_BUFFER_VIEW, 16> vbView;
            vbView.SetSize(currentPipeline->vertexFormat.Attributes.Count());
            int vertexSize = currentPipeline->vertexFormat.Size();
            for (int i = 0; i < currentPipeline->vertexFormat.Attributes.Count(); i++)
            {
                vbView[i].StrideInBytes = vertexSize;
                vbView[i].SizeInBytes = vertexBuffer->bufferSize - vertexBufferOffset -
                                        currentPipeline->vertexFormat.Attributes[i].StartOffset;
                vbView[i].BufferLocation = vertexBuffer->buffer.resource->GetGPUVirtualAddress() + vertexBufferOffset +
                                           currentPipeline->vertexFormat.Attributes[i].StartOffset;
            }
            commandList->IASetVertexBuffers(0, vbView.Count(), vbView.Buffer());
            commandList->IASetPrimitiveTopology(
                TranslatePrimitiveTopology(currentPipeline->FixedFunctionStates.PrimitiveTopology));
        }
    }

public:
    Viewport viewport;
    ID3D12DescriptorHeap *descHeaps[2];
    virtual void BeginRecording(GameEngine::FrameBuffer * /*frameBuffer*/) override
    {
        auto &state = RendererState::Get();
        CHECK_DX(commandAllocator->Reset());
        commandList->Reset(commandAllocator, nullptr);
        pipelineChanged = false;
        descBindingChanged = false;
        currentPipeline = nullptr;
        for (auto &descSet : descSetBindings)
            descSet = nullptr;
        descSets.Clear();
        vertexBuffer = nullptr;
        vertexBufferOffset = 0;
        vertexBufferChanged = false;
        descHeaps[0] = state.resourceDescHeap.heap;
        descHeaps[1] = state.samplerDescHeap.heap;
        commandList->SetDescriptorHeaps(2, descHeaps);
        isEmpty = true;
        resourceUses.Clear();
    }

    virtual void EndRecording() override
    {
        commandList->Close();
    }

    virtual void SetViewport(Viewport _viewport) override
    {
        viewport = _viewport;
    }

    virtual void SetEventMarker(const char *name, uint32_t color) override
    {
        auto &state = RendererState::Get();
        state.pix.SetMarkerOnCommandList(commandList, color, name);
    }

    virtual void BindVertexBuffer(GameEngine::Buffer *buffer, int byteOffset) override
    {
        vertexBuffer = reinterpret_cast<Buffer *>(buffer);
        vertexBufferOffset = byteOffset;
        vertexBufferChanged = true;
        ResourceUse resourceUse;
        resourceUse.resource = &vertexBuffer->buffer;
        resourceUse.subresourceId = 0;
        resourceUse.targetState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        resourceUses.Add(resourceUse);
    }
    virtual void BindIndexBuffer(GameEngine::Buffer *indexBuffer, int byteOffset) override
    {
        auto d3dbuffer = reinterpret_cast<Buffer *>(indexBuffer);
        D3D12_INDEX_BUFFER_VIEW view = {};
        view.BufferLocation = d3dbuffer->buffer.resource->GetGPUVirtualAddress() + byteOffset;
        view.Format = DXGI_FORMAT_R32_UINT;
        view.SizeInBytes = d3dbuffer->bufferSize - byteOffset;
        commandList->IASetIndexBuffer(&view);
        ResourceUse resourceUse;
        resourceUse.resource = &d3dbuffer->buffer;
        resourceUse.subresourceId = 0;
        resourceUse.targetState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        resourceUses.Add(resourceUse);
    }
    virtual void BindPipeline(GameEngine::Pipeline *pipeline) override
    {
        pipelineChanged = (reinterpret_cast<Pipeline *>(pipeline) != currentPipeline);
        currentPipeline = reinterpret_cast<Pipeline *>(pipeline);
    }
    virtual void BindDescriptorSet(int binding, GameEngine::DescriptorSet *descSet) override
    {
        auto d3dDescSet = reinterpret_cast<DescriptorSet *>(descSet);
        descSets.Add(d3dDescSet);
        for (int i = descSetBindings.Count(); i <= binding; i++)
            descSetBindings.Add(nullptr);
        descBindingChanged = d3dDescSet != descSetBindings[binding];
        descSetBindings[binding] = d3dDescSet;
    }
    virtual void Draw(int firstVertex, int vertexCount) override
    {
        if (vertexCount)
        {
            MaybeUpdatePipelineBindings();
            isEmpty = false;
            commandList->DrawInstanced(vertexCount, 1, firstVertex, 0);
        }
    }
    virtual void DrawInstanced(int numInstances, int firstVertex, int vertexCount) override
    {
        if (vertexCount)
        {
            MaybeUpdatePipelineBindings();
            commandList->DrawInstanced(vertexCount, numInstances, firstVertex, 0);
            isEmpty = false;
        }
    }
    virtual void DrawIndexed(int firstIndex, int indexCount) override
    {
        if (indexCount)
        {
            MaybeUpdatePipelineBindings();
            commandList->DrawIndexedInstanced(indexCount, 1, firstIndex, 0, 0);
            isEmpty = false;
        }
    }
    virtual void DrawIndexedInstanced(int numInstances, int firstIndex, int indexCount) override
    {
        if (indexCount)
        {
            MaybeUpdatePipelineBindings();
            commandList->DrawIndexedInstanced(indexCount, numInstances, firstIndex, 0, 0);
            isEmpty = false;
        }
    }
    virtual void DispatchCompute(int groupCountX, int groupCountY, int groupCountZ) override
    {
        MaybeUpdatePipelineBindings();
        commandList->Dispatch(groupCountX, groupCountY, groupCountZ);
        isEmpty = false;
    }
};

class WindowSurface : public GameEngine::WindowSurface
{
public:
    WindowHandle windowHandle;
    IDXGISwapChain3 *swapchain = nullptr;
    int w, h;
    void CreateSwapchain(int width, int height)
    {
        auto &state = RendererState::Get();
        this->w = width;
        this->h = height;
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Stereo = 0;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        if (swapchain)
        {
            swapchain->Release();
            swapchain = nullptr;
        }
        IDXGISwapChain1 *swapchain1 = nullptr;
        CHECK_DX(state.dxgiFactory->CreateSwapChainForHwnd(
            state.queue, (HWND)windowHandle, &desc, nullptr, nullptr, &swapchain1));
        CHECK_DX(swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain)));
        swapchain1->Release();
    }

public:
    WindowSurface(WindowHandle windowHandle, int width, int height)
    {
        this->windowHandle = windowHandle;
        CreateSwapchain(width, height);
    }
    ~WindowSurface()
    {
        if (swapchain)
            swapchain->Release();
    }

public:
    virtual WindowHandle GetWindowHandle() override
    {
        return windowHandle;
    }
    virtual void Resize(int width, int height) override
    {
        if (width != w && height != h && width > 1 && height > 1)
        {
            CreateSwapchain(width, height);
        }
    }
    virtual void GetSize(int &width, int &height) override
    {
        width = this->w;
        height = this->h;
    }
};

class HardwareRenderer : public GameEngine::HardwareRenderer
{
private:
    D3DCommandList pendingCommands[MaxRenderThreads] = {};
    List<D3D12_RESOURCE_BARRIER> pendingBarriers[MaxRenderThreads];
    ID3D12PipelineState *blitPipeline;
    ID3D12RootSignature *blitRootSignature;
    List<RefPtr<SingleQueueFence>> presentFences;
    List<RefPtr<CommandBuffer>> presentCommandBuffers;
    const char *blitShaderSrc = R"(
    Texture2D inputTexture : register(t0);
    RWTexture2D<float4> outputTexture : register(u0);
    cbuffer Params : register(b0)
    {
        uint originX;
        uint originY;
        uint flip;
    }
    [numthreads(16, 16, 1)]
    void CSMain( uint3 index : SV_DispatchThreadID )
    {
        int2 origin = int2(originX, originY);
        uint srcWidth, srcHeight, srcLevels;
        inputTexture.GetDimensions(0, srcWidth, srcHeight, srcLevels);
        if (flip)
            outputTexture[index.xy + origin] = inputTexture.Load(int3(index.x, srcHeight - index.y - 1, 0));
        else
            outputTexture[index.xy + origin] = inputTexture.Load(int3(index.xy, 0));
    }
    )";
    void InitBlitPipeline()
    {
        auto &state = RendererState::Get();
        ID3DBlob *code = nullptr, *errMsg = nullptr;
        state.d3dCompile(blitShaderSrc, strlen(blitShaderSrc), "blitShader", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0,
            &code, &errMsg);
        if (errMsg)
        {
            throw HardwareRendererException((const char *)errMsg->GetBufferPointer());
        }
        ID3DBlob *rootSignatureBlob = nullptr;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        rootSignatureDesc.Desc_1_0.NumParameters = 2;
        D3D12_ROOT_PARAMETER rootParams[2] = {};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.Num32BitValues = 3;
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 2;
        D3D12_DESCRIPTOR_RANGE descRanges[2] = {};
        descRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRanges[0].NumDescriptors = 1;
        descRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descRanges[1].NumDescriptors = 1;
        descRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        rootParams[1].DescriptorTable.pDescriptorRanges = descRanges;
        rootSignatureDesc.Desc_1_0.pParameters = rootParams;
        CHECK_DX(state.serializeVersionedRootSignature(&rootSignatureDesc, &rootSignatureBlob, nullptr));
        CHECK_DX(state.device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&blitRootSignature)));
        rootSignatureBlob->Release();
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
        pipelineDesc.CS.BytecodeLength = code->GetBufferSize();
        pipelineDesc.CS.pShaderBytecode = code->GetBufferPointer();
        pipelineDesc.pRootSignature = blitRootSignature;
        CHECK_DX(state.device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&blitPipeline)));
        if (code)
            code->Release();
        if (errMsg)
            errMsg->Release();
    }

public:
    HardwareRenderer(int gpuId, bool useSoftwareDevice, CoreLib::String cacheLocation)
    {
        auto &state = RendererState::Get();
        state.cacheLocation = cacheLocation;
        if (state.rendererCount == 0)
        {
            HMODULE dx12module = LoadLibraryEx(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!dx12module)
            {
                throw HardwareRendererException("cannot load d3d12.dll");
            }
            PFN_D3D12_CREATE_DEVICE createDevice =
                (PFN_D3D12_CREATE_DEVICE)GetProcAddress(dx12module, "D3D12CreateDevice");
            if (!createDevice)
            {
                throw HardwareRendererException("cannot load d3d12.dll");
            }
            state.serializeVersionedRootSignature = (PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE)GetProcAddress(
                dx12module, "D3D12SerializeVersionedRootSignature");
            if (!state.serializeVersionedRootSignature)
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
            HMODULE d3dCompilerModule = LoadLibrary(L"d3dcompiler_47.dll");
            if (!d3dCompilerModule)
            {
                throw HardwareRendererException("cannot load d3dcompiler_47.dll");
            }
            state.d3dCompile = (PFN_D3DCOMPILE)GetProcAddress(d3dCompilerModule, "D3DCompile");
            if (!state.d3dCompile)
            {
                throw HardwareRendererException("cannot load d3dcompiler_47.dll");
            }
            // Try to load PIX library.
            state.pix.Load();
            // Initialize D3D Context

            CHECK_DX(createDXGIFactory1(IID_PPV_ARGS(&state.dxgiFactory)));
            CoreLib::List<IDXGIAdapter1 *> adapters;
            if (useSoftwareDevice)
            {
                IDXGIAdapter1 *adapter = nullptr;
                CHECK_DX(state.dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
                adapters.Add(adapter);
            }
            else
            {
                IDXGIAdapter1 *adapter = nullptr;
                for (unsigned i = 0;; i++)
                {
                    if (DXGI_ERROR_NOT_FOUND == state.dxgiFactory->EnumAdapters1(i, &adapter))
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
                }
            }
            gpuId = CoreLib::Math::Min(adapters.Count() - 1, gpuId);
            if (gpuId < 0)
                CORELIB_ABORT("No D3D12-compatible GPU found.");

#if defined(_DEBUG)
            // Enable the debug layer.
            auto d3d12GetDebugInterface =
                (PFN_D3D12_GET_DEBUG_INTERFACE)(GetProcAddress(dx12module, "D3D12GetDebugInterface"));
            if (d3d12GetDebugInterface)
            {
                ID3D12Debug *debugController;
                if (SUCCEEDED(d3d12GetDebugInterface(IID_PPV_ARGS(&debugController))))
                {
                    debugController->EnableDebugLayer();
                    debugController->Release();
                }
            }
#endif
            CHECK_DX(createDevice(adapters[gpuId], D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state.device)));

#if defined(_DEBUG)
            ID3D12InfoQueue *d3dInfoQueue = nullptr;
            state.device->QueryInterface(IID_PPV_ARGS(&d3dInfoQueue));
            if (d3dInfoQueue)
            {
                d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
                d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
                d3dInfoQueue->Release();
            }
#endif
            // Read adapter info.
            DXGI_ADAPTER_DESC adapterDesc;
            adapters[gpuId]->GetDesc(&adapterDesc);
            state.deviceName = CoreLib::String::FromWString(adapterDesc.Description);
            state.videoMemorySize = adapterDesc.DedicatedVideoMemory;
            for (auto &adapter : adapters)
            {
                adapter->Release();
            }

            // Create CommandQueue
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            CHECK_DX(state.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&state.queue)));
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
            CHECK_DX(state.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&state.transferQueue)));

            // Create Copy command list allocator
            CHECK_DX(state.device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&state.largeCopyCmdListAllocator)));

            state.rtvDescHeap.Create(state.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RtvDescriptorHeapSize, 0, 0, false);
            state.dsvDescHeap.Create(state.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DsvDescriptorHeapSize, 0, 0, false);
        }
        state.rendererCount++;
    }
    ~HardwareRenderer()
    {
        auto &state = RendererState::Get();
        state.rendererCount--;
        blitPipeline->Release();
        blitRootSignature->Release();
        presentCommandBuffers = decltype(presentCommandBuffers)();
        presentFences = decltype(presentFences)();
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

    virtual void BeginJobSubmission() override
    {
        auto &state = RendererState::Get();
        pendingCommands[renderThreadId] = state.GetTempCommandList();
        ID3D12DescriptorHeap *descHeaps[2] = {state.resourceDescHeap.heap, state.samplerDescHeap.heap};
        pendingCommands[renderThreadId]->SetDescriptorHeaps(2, descHeaps);
    }

    virtual void QueueRenderPass(GameEngine::FrameBuffer *frameBuffer, bool clearFrameBuffer,
        CoreLib::ArrayView<GameEngine::CommandBuffer *> commands, PipelineBarriers barriers) override
    {
        auto cmdList = pendingCommands[renderThreadId];
        auto d3dframeBuffer = reinterpret_cast<FrameBuffer *>(frameBuffer);

        auto &state = RendererState::Get();
        if (barriers == PipelineBarriers::MemoryAndImage)
        {
            auto &pendingBarrierList = pendingBarriers[state.version];
            pendingBarrierList.Clear();
            // Collect texture uses in descriptor tables.
            for (auto cmd : commands)
            {
                auto d3dcmd = reinterpret_cast<CommandBuffer *>(cmd);
                for (auto descSet : d3dcmd->descSets)
                {
                    for (auto &useList : descSet->resourceUses)
                    {
                        for (auto &resUse : useList)
                        {
                            resUse.resource->TransferState(
                                resUse.subresourceId, resUse.targetState, pendingBarrierList);
                        }
                    }
                }
            }
            // Collect texture uses from framebuffer.
            for (int i = 0; i < d3dframeBuffer->renderAttachments.attachments.Count(); i++)
            {
                auto &attachment = d3dframeBuffer->renderAttachments.attachments[i];
                if (attachment.handle.tex2D)
                {
                    static_cast<D3DTexture *>(attachment.handle.tex2D->GetInternalPtr())
                        ->TransferState(0,
                            attachment.handle.tex2D->IsDepthStencilFormat() ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                                            : D3D12_RESOURCE_STATE_RENDER_TARGET,
                            pendingBarrierList);
                }
                else if (attachment.handle.tex2DArray)
                {
                    auto d3dTexture = static_cast<D3DTexture *>(attachment.handle.tex2DArray->GetInternalPtr());
                    d3dTexture->TransferState(attachment.layer * d3dTexture->properties.mipLevels + attachment.level,
                        isDepthFormat(d3dTexture->properties.format) ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                                     : D3D12_RESOURCE_STATE_RENDER_TARGET,
                        pendingBarrierList);
                }
                else if (attachment.handle.texCube)
                {
                    auto d3dTexture = static_cast<D3DTexture *>(attachment.handle.texCube->GetInternalPtr());
                    d3dTexture->TransferState(
                        (int)attachment.face * d3dTexture->properties.mipLevels + attachment.level,
                        isDepthFormat(d3dTexture->properties.format) ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                                     : D3D12_RESOURCE_STATE_RENDER_TARGET,
                        pendingBarrierList);
                }
                else if (attachment.handle.texCubeArray)
                {
                    auto d3dTexture = static_cast<D3DTexture *>(attachment.handle.texCubeArray->GetInternalPtr());
                    d3dTexture->TransferState(
                        (attachment.layer * 6 + (int)attachment.face) * d3dTexture->properties.mipLevels +
                            attachment.level,
                        isDepthFormat(d3dTexture->properties.format) ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                                                                     : D3D12_RESOURCE_STATE_RENDER_TARGET,
                        pendingBarrierList);
                }
            }
            if (pendingBarrierList.Count())
                cmdList->ResourceBarrier(pendingBarrierList.Count(), pendingBarrierList.Buffer());
        }

        cmdList->OMSetRenderTargets(d3dframeBuffer->rtvCount, d3dframeBuffer->rtvHandles, false,
            d3dframeBuffer->dsvCount ? &d3dframeBuffer->dsvHandle : nullptr);
        if (clearFrameBuffer)
        {
            if (d3dframeBuffer->dsvCount)
            {
                if (d3dframeBuffer->depthStencilTexture->properties.format == StorageFormat::Depth24Stencil8)
                {
                    cmdList->ClearDepthStencilView(d3dframeBuffer->dsvHandle,
                        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
                }
                else
                {
                    cmdList->ClearDepthStencilView(
                        d3dframeBuffer->dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                }
            }
            for (int i = 0; i < d3dframeBuffer->rtvCount; i++)
            {
                float zero[4] = {0.0f};
                cmdList->ClearRenderTargetView(d3dframeBuffer->rtvHandles[i], zero, 0, nullptr);
            }
        }
        Viewport currentViewport;
        for (auto cmd : commands)
        {
            auto d3dcmd = reinterpret_cast<CommandBuffer *>(cmd);
            if (d3dcmd->isEmpty)
                continue;
            Viewport viewport = reinterpret_cast<CommandBuffer *>(cmd)->viewport;
            if (viewport != currentViewport)
            {
                D3D12_VIEWPORT d3dviewport = {};
                d3dviewport.TopLeftX = viewport.x;
                d3dviewport.TopLeftY = viewport.y;
                d3dviewport.Height = viewport.h;
                d3dviewport.Width = viewport.w;
                d3dviewport.MinDepth = viewport.minZ;
                d3dviewport.MaxDepth = viewport.maxZ;
                cmdList->RSSetViewports(1, &d3dviewport);

                D3D12_RECT scissorRect = {};
                scissorRect.left = (int)viewport.x;
                scissorRect.right = (int)(viewport.x + viewport.w);
                scissorRect.top = (int)(viewport.y);
                scissorRect.bottom = (int)(viewport.y + viewport.h);
                cmdList->RSSetScissorRects(1, &scissorRect);
            }
            cmdList->ExecuteBundle(d3dcmd->commandList);
        }
    }

    virtual void QueueComputeTask(GameEngine::Pipeline *computePipeline, GameEngine::DescriptorSet *descriptorSet,
        int x, int y, int z, PipelineBarriers barriers) override
    {
        auto &state = RendererState::Get();
        auto cmdList = pendingCommands[renderThreadId];
        auto pipeline = reinterpret_cast<Pipeline *>(computePipeline);
        cmdList->SetPipelineState(pipeline->pipelineState);
        cmdList->SetComputeRootSignature(pipeline->rootSignature);
        int rootIndex = 0;
        auto descSet = reinterpret_cast<DescriptorSet *>(descriptorSet);
        if (descSet->resourceCount)
        {
            cmdList->SetComputeRootDescriptorTable(rootIndex, descSet->resourceAddr.gpuHandle);
            rootIndex++;
        }
        if (descSet->samplerCount)
        {
            cmdList->SetComputeRootDescriptorTable(rootIndex, descSet->samplerAddr.gpuHandle);
            rootIndex++;
        }

        // Collect texture uses in descriptor tables.
        if (barriers == PipelineBarriers::MemoryAndImage)
        {
            auto &pendingBarrierList = pendingBarriers[state.version];
            pendingBarrierList.Clear();
            for (auto &useList : descSet->resourceUses)
            {
                for (auto &resUse : useList)
                {
                    resUse.resource->TransferState(resUse.subresourceId, resUse.targetState, pendingBarrierList);
                }
            }
            if (pendingBarrierList.Count())
                cmdList->ResourceBarrier(pendingBarrierList.Count(), pendingBarrierList.Buffer());
        }

        cmdList->Dispatch(x, y, z);
    }

    virtual void EndJobSubmission(GameEngine::Fence *fence) override
    {
        auto &state = RendererState::Get();
        CHECK_DX(pendingCommands[renderThreadId].Close());
        ID3D12CommandList *cmdList = pendingCommands[renderThreadId].list;
        state.queue->ExecuteCommandLists(1, &cmdList);
        if (fence)
        {
            auto d3dfence = dynamic_cast<D3DRenderer::Fence *>(fence);
            auto value = InterlockedIncrement(&state.waitFenceValue);
            state.queue->Signal(d3dfence->direct.fence, value);
            d3dfence->direct.fence->SetEventOnCompletion(value, d3dfence->direct.waitEvent);
            state.transferQueue->Signal(d3dfence->copy.fence, value);
            d3dfence->copy.fence->SetEventOnCompletion(value, d3dfence->copy.waitEvent);
        }
    }

    virtual void Present(GameEngine::WindowSurface *surface, GameEngine::Texture2D *srcImage) override
    {
        auto &state = RendererState::Get();
        auto d3dsurface = reinterpret_cast<WindowSurface *>(surface);
        auto srcTexture = reinterpret_cast<D3DTexture *>(srcImage->GetInternalPtr());
        auto bufferIndex = d3dsurface->swapchain->GetCurrentBackBufferIndex();
        ID3D12Resource *backBuffer = nullptr;
        d3dsurface->swapchain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer));
        Texture2DProxy proxyTexture(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
        auto cmdList = presentCommandBuffers[state.version];
        presentFences[state.version]->Wait();
        presentFences[state.version]->Reset();
        cmdList->commandAllocator->Reset();
        cmdList->commandList->Reset(cmdList->commandAllocator, nullptr);
        CORELIB_ASSERT(srcTexture->properties.d3dformat == DXGI_FORMAT_R8G8B8A8_UNORM);

        D3D12_TEXTURE_COPY_LOCATION copyDestLocation = {}, copySrcLocation = {};
        copyDestLocation.pResource = backBuffer;
        auto dstDesc = backBuffer->GetDesc();

        copyDestLocation.SubresourceIndex = 0;
        copyDestLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        copySrcLocation = copyDestLocation;
        copySrcLocation.pResource = srcTexture->resource;
        int surfaceW = (int)dstDesc.Width;
        int surfaceH = (int)dstDesc.Height;
        D3D12_BOX srcBox;
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.back = 1;
        srcBox.right = Math::Min(surfaceW, srcTexture->properties.width);
        srcBox.bottom = Math::Min(surfaceH, srcTexture->properties.height);
        auto &pendingBarrierList = pendingBarriers[state.version];
        pendingBarrierList.Clear();
        proxyTexture.texture.TransferState(0, D3D12_RESOURCE_STATE_COPY_DEST, pendingBarrierList);
        srcTexture->TransferState(0, D3D12_RESOURCE_STATE_COPY_SOURCE, pendingBarrierList);
        cmdList->commandList->ResourceBarrier(pendingBarrierList.Count(), pendingBarrierList.Buffer());
        cmdList->commandList->CopyTextureRegion(&copyDestLocation, 0, 0, 0, &copySrcLocation, &srcBox);
        pendingBarrierList.Clear();
        proxyTexture.texture.TransferState(0, D3D12_RESOURCE_STATE_PRESENT, pendingBarrierList);
        cmdList->commandList->ResourceBarrier(pendingBarrierList.Count(), pendingBarrierList.Buffer());
        cmdList->commandList->Close();
        state.queue->ExecuteCommandLists(1, (ID3D12CommandList **)&cmdList->commandList);
        auto d3dfence = presentFences[state.version].Ptr();
        auto value = InterlockedIncrement(&state.waitFenceValue);
        state.queue->Signal(d3dfence->fence, value);
        d3dfence->fence->SetEventOnCompletion(value, d3dfence->waitEvent);
        backBuffer->Release();
        CHECK_DX(d3dsurface->swapchain->Present(0, 0));
    }

    void BlitImpl(ID3D12GraphicsCommandList *cmdList, GameEngine::Texture2D *dstImage, GameEngine::Texture2D *srcImage,
        VectorMath::Vec2i destOffset, bool flipSrc)
    {
        auto &state = RendererState::Get();
        auto srcTexture = static_cast<D3DTexture *>(srcImage->GetInternalPtr());
        auto dstTexture = static_cast<D3DTexture *>(dstImage->GetInternalPtr());
        cmdList->SetPipelineState(blitPipeline);
        cmdList->SetComputeRootSignature(blitRootSignature);
        uint32_t arguments[3] = {(uint32_t)destOffset.x, (uint32_t)destOffset.y, flipSrc ? 1u : 0u};
        cmdList->SetComputeRoot32BitConstants(0, 3, arguments, 0);
        auto descriptorTable = state.resourceDescHeap.AllocTemp(state.version, 2);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 0xFFFFFFFF;
        state.device->CreateShaderResourceView(srcTexture->resource, &srvDesc, descriptorTable.cpuHandle);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = dstTexture->properties.d3dformat;
        auto uavDescHandle = descriptorTable.cpuHandle;
        uavDescHandle.ptr += state.resourceDescHeap.handleIncrementSize;
        state.device->CreateUnorderedAccessView(dstTexture->resource, nullptr, &uavDesc, uavDescHandle);
        cmdList->SetComputeRootDescriptorTable(1, descriptorTable.gpuHandle);
        int width = 0, height = 0;
        srcImage->GetSize(width, height);
        auto &pendingBarrierList = pendingBarriers[state.version];
        pendingBarrierList.Clear();
        srcTexture->TransferState(0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, pendingBarrierList);
        dstTexture->TransferState(0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, pendingBarrierList);
        if (pendingBarrierList.Count())
            cmdList->ResourceBarrier(pendingBarrierList.Count(), pendingBarrierList.Buffer());
        cmdList->Dispatch(width, height, 1);
    }

    virtual void Blit(GameEngine::Texture2D *dstImage, GameEngine::Texture2D *srcImage, VectorMath::Vec2i destOffset,
        SourceFlipMode flipSrc) override
    {
        auto cmdList = pendingCommands[renderThreadId];
        BlitImpl(cmdList.list, dstImage, srcImage, destOffset, flipSrc == SourceFlipMode::Flip);
    }

    virtual void Wait() override
    {
        RendererState::Get().Wait();
    }

    virtual void Init(int versionCount) override
    {
        auto &state = RendererState::Get();

        CORELIB_ASSERT(state.stagingBuffers.Count() == 0);
        presentFences.SetSize(versionCount);
        presentCommandBuffers.SetSize(versionCount);
        for (int i = 0; i < versionCount; i++)
        {
            presentFences[i] = new SingleQueueFence();
            SetEvent(presentFences[i]->waitEvent);
            presentCommandBuffers[i] = new CommandBuffer(D3D12_COMMAND_LIST_TYPE_DIRECT);
        }
        state.resourceDescHeap.Create(
            state.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ResourceDescriptorHeapSize, 512, versionCount, true);
        state.samplerDescHeap.Create(
            state.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SamplerDescriptorHeapSize, 8, versionCount, true);

        state.stagingBuffers.SetSize(versionCount);
        state.stagingBufferAllocPtr.SetSize(versionCount);
        state.tempCommandAllocatorManager.Init(versionCount, D3D12_COMMAND_LIST_TYPE_DIRECT);
        state.copyCommandAllocatorManager.Init(versionCount, D3D12_COMMAND_LIST_TYPE_COPY);
        for (int i = 0; i < versionCount; i++)
        {
            state.stagingBuffers[i] = state.CreateBufferResource(
                StagingBufferSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, false);
        }
        state.tempCommandLists.SetSize(versionCount);
        state.tempCommandListAllocPtr.SetSize(versionCount);
        for (int i = 0; i < versionCount; i++)
        {
            state.tempCommandListAllocPtr[i] = 0;
            state.stagingBufferAllocPtr[i] = 0;
        }
        state.copyCommandLists.SetSize(versionCount);
        state.copyCommandListAllocPtr.SetSize(versionCount);
        for (int i = 0; i < versionCount; i++)
        {
            state.copyCommandListAllocPtr[i] = 0;
        }

        InitBlitPipeline();
    }

    virtual void ResetTempBufferVersion(int version) override
    {
        auto &state = RendererState::Get();
        state.version = version;
        state.stagingBufferAllocPtr[state.version] = 0;
        state.tempCommandListAllocPtr[state.version] = 0;
        state.tempCommandAllocatorManager.Reset(version);
        state.copyCommandListAllocPtr[state.version] = 0;
        state.copyCommandAllocatorManager.Reset(version);
        state.resourceDescHeap.ResetTemp(version);
        state.samplerDescHeap.ResetTemp(version);
    }
    virtual GameEngine::Fence *CreateFence() override
    {
        return new Fence();
    }

    virtual GameEngine::Buffer *CreateBuffer(
        BufferUsage usage, int sizeInBytes, const BufferStructureInfo *structInfo) override
    {
        return new Buffer(usage, sizeInBytes, false, structInfo);
    }

    virtual GameEngine::Buffer *CreateMappedBuffer(
        BufferUsage usage, int sizeInBytes, const BufferStructureInfo *structInfo) override
    {
        return new Buffer(usage, sizeInBytes, true, structInfo);
    }

    virtual GameEngine::Texture2D *CreateTexture2D(
        String name, int width, int height, StorageFormat format, DataType type, void *data) override
    {
        auto result = new Texture2D();
        result->texture.InitTexture2DFromData(width, height, TextureUsage::Sampled, format, type, data);
        return result;
    }

    virtual GameEngine::Texture2D *CreateTexture2D(
        String name, TextureUsage usage, int width, int height, int mipLevelCount, StorageFormat format) override
    {
        auto result = new Texture2D();
        result->texture.InitTexture(width, height, 1, 1, mipLevelCount, format, usage,
            D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURE2D,
            D3DTexture::TextureUsageToInitialState(usage));
        return result;
    }

    virtual GameEngine::Texture2D *CreateTexture2D(String name, TextureUsage usage, int width, int height,
        int mipLevelCount, StorageFormat format, DataType type, CoreLib::ArrayView<void *> mipLevelData) override
    {
        auto result = new Texture2D();
        result->texture.InitTexture(width, height, 1, 1, mipLevelCount, format, usage,
            D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_RESOURCE_STATE_COPY_DEST);
        for (int i = 0; i < mipLevelCount; i++)
        {
            result->texture.SetData(i, 0, 0, 0, 0, CoreLib::Math::Max(1, (width >> i)),
                CoreLib::Math::Max(1, (height >> i)), 1, type, mipLevelData[i],
                D3DTexture::TextureUsageToInitialState(usage));
        }
        return result;
    }

    virtual GameEngine::Texture2DArray *CreateTexture2DArray(String name, TextureUsage usage, int width, int height,
        int layers, int mipLevelCount, StorageFormat format) override
    {
        auto result = new Texture2DArray();
        result->texture.InitTexture(width, height, layers, 1, mipLevelCount, format, usage,
            D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
            D3DTexture::TextureUsageToInitialState(usage));
        return result;
    }

    virtual GameEngine::TextureCube *CreateTextureCube(
        String name, TextureUsage usage, int size, int mipLevelCount, StorageFormat format) override
    {
        auto result = new TextureCube();
        result->texture.InitTexture(size, size, 6, 1, mipLevelCount, format, usage, D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            D3D12_SRV_DIMENSION_TEXTURECUBE, D3DTexture::TextureUsageToInitialState(usage));
        return result;
    }

    virtual GameEngine::TextureCubeArray *CreateTextureCubeArray(
        String name, TextureUsage usage, int size, int mipLevelCount, int cubemapCount, StorageFormat format) override
    {
        auto result = new TextureCubeArray();
        result->texture.InitTexture(size, size, 6 * cubemapCount, 1, mipLevelCount, format, usage,
            D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURECUBEARRAY,
            D3DTexture::TextureUsageToInitialState(usage));
        return result;
    }

    virtual GameEngine::Texture3D *CreateTexture3D(String name, TextureUsage usage, int width, int height, int depth,
        int mipLevelCount, StorageFormat format) override
    {
        auto result = new Texture3D();
        result->texture.InitTexture(width, height, 1, depth, mipLevelCount, format, usage,
            D3D12_RESOURCE_DIMENSION_TEXTURE3D, D3D12_SRV_DIMENSION_TEXTURE3D,
            D3DTexture::TextureUsageToInitialState(usage));
        return result;
    }

    virtual GameEngine::TextureSampler *CreateTextureSampler() override
    {
        return new TextureSampler();
    }

    virtual GameEngine::Shader *CreateShader(ShaderType stage, const char *data, int size) override
    {
        return new Shader(stage, data, size);
    }

    virtual GameEngine::RenderTargetLayout *CreateRenderTargetLayout(
        CoreLib::ArrayView<AttachmentLayout> bindings, bool ignoreInitialContent) override
    {
        return new RenderTargetLayout(bindings, ignoreInitialContent);
    }

    virtual GameEngine::PipelineBuilder *CreatePipelineBuilder() override
    {
        return new PipelineBuilder();
    }

    virtual GameEngine::DescriptorSetLayout *CreateDescriptorSetLayout(
        CoreLib::ArrayView<DescriptorLayout> descriptors) override
    {
        return new DescriptorSetLayout(descriptors);
    }

    virtual GameEngine::DescriptorSet *CreateDescriptorSet(GameEngine::DescriptorSetLayout *layout) override
    {
        return new DescriptorSet(dynamic_cast<D3DRenderer::DescriptorSetLayout *>(layout));
    }

    virtual GameEngine::CommandBuffer *CreateCommandBuffer() override
    {
        return new CommandBuffer(D3D12_COMMAND_LIST_TYPE_BUNDLE);
    }

    virtual TargetShadingLanguage GetShadingLanguage() override
    {
        return TargetShadingLanguage::HLSL;
    }

    virtual int UniformBufferAlignment() override
    {
        return D3DConstantBufferAlignment;
    }

    virtual int StorageBufferAlignment() override
    {
        return D3DStorageBufferAlignment;
    }

    virtual GameEngine::WindowSurface *CreateSurface(WindowHandle windowHandle, int width, int height) override
    {
        return new WindowSurface(windowHandle, width, height);
    }

    virtual CoreLib::String GetRendererName() override
    {
        return RendererState::Get().deviceName;
    }
};
} // namespace D3DRenderer
} // namespace GameEngine

namespace GameEngine
{
HardwareRenderer *CreateD3DHardwareRenderer(int gpuId, bool useSoftwareRenderer, CoreLib::String cachePath)
{
    return new D3DRenderer::HardwareRenderer(gpuId, useSoftwareRenderer, cachePath);
}
} // namespace GameEngine

#else

namespace GameEngine
{
HardwareRenderer *CreateD3DHardwareRenderer(int /*gpuId*/, bool /*useSoftwareRenderer*/, CoreLib::String /*cachePath*/)
{
    throw HardwareRendererException("Direct3D 12 is not available on this platform.");
}
} // namespace GameEngine

#endif