#include "../GameEngineCore/HardwareRenderer.h"
#include "CoreLib/WinForm/Debug.h"
#include "CoreLib/VectorMath.h"
#include "CoreLib/PerformanceCounter.h"
#include "../Engine.h"
#include "CoreLib/ShortList.h"
#include "CoreLib/LibIO.h"
#include "volk.h"
#include "vulkan.hpp"
#include <mutex>

// Only execute actions of DEBUG_ONLY in DEBUG mode
#if _DEBUG
#define DEBUG_ONLY(x) { x; }
#define USE_VALIDATION_LAYER 1
#else
#define DEBUG_ONLY(x) {}
#endif

#ifdef __linux__
#undef Always
#undef None
#endif

using namespace GameEngine;
using namespace CoreLib::IO;
using namespace CoreLib;

namespace VK
{
	const int TargetVulkanVersion_Major = 1;
	const int TargetVulkanVersion_Minor = 0;
    const int MaxRenderThreads = 4;
    int MaxThreadCount = MaxRenderThreads;
	unsigned int GpuId = 0;

    thread_local int renderThreadId = -1;

	namespace VkDebug
	{
		// Print device info
		void PrintDeviceInfo(const std::vector<vk::PhysicalDevice>& physicalDevices);

		// Debug callback direct to stderr
		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugReportFlagsEXT      flags,
			VkDebugReportObjectTypeEXT objectType,
			uint64_t                   object,
			size_t                     location,
			int32_t                    messageCode,
			const char*                pLayerPrefix,
			const char*                pMessage,
			void*                      pUserData);

		// VkDebug Implementation
		void PrintDeviceInfo(const std::vector<vk::PhysicalDevice>& physicalDevices)
		{
			for (uint32_t i = 0; i < physicalDevices.size(); i++)
			{
				// Print out device properties
				vk::PhysicalDeviceProperties deviceProperties = physicalDevices.at(i).getProperties();
				printf("Device ID:      %d\n", deviceProperties.deviceID);
				printf("Driver Version: %d\n", deviceProperties.driverVersion);
				printf("Device Name:    %s\n", deviceProperties.deviceName);
				printf("Device Type:    %d\n", static_cast<int>(deviceProperties.deviceType));
				printf("API Version:    %d.%d.%d\n",
					VK_VERSION_MAJOR(deviceProperties.apiVersion),
					VK_VERSION_MINOR(deviceProperties.apiVersion),
					VK_VERSION_PATCH(deviceProperties.apiVersion));

				printf("\n");

				// Print out device queue info
				std::vector<vk::QueueFamilyProperties> familyProperties = physicalDevices.at(i).getQueueFamilyProperties();

				for (size_t j = 0; j < familyProperties.size(); j++) {
					printf("Count of Queues: %d\n", familyProperties[j].queueCount);
					printf("Supported operations on this queue: %s\n", to_string(familyProperties[j].queueFlags).c_str());
					printf("\n");
				}

				// Print out device memory properties
				vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevices.at(i).getMemoryProperties();
				printf("Memory Type Count: %d\n", memoryProperties.memoryTypeCount);
				for (size_t j = 0; j < memoryProperties.memoryTypeCount; j++)
				{
					auto type = memoryProperties.memoryTypes[j];
					printf("(Heap %d) %s\n", type.heapIndex, to_string(type.propertyFlags).c_str());
				}
				printf("\n");

				printf("Memory Heap Count: %d\n", memoryProperties.memoryHeapCount);
				for (size_t j = 0; j < memoryProperties.memoryHeapCount; j++)
				{
					auto heap = memoryProperties.memoryHeaps[j];
					printf("(Heap %zu)\n", j);
					printf("\tsize: %u MB\n", static_cast<unsigned>(heap.size / 1024 / 1024));
					printf("\tflags:%s\n", to_string(heap.flags).c_str());
					printf("\n");
				}
				printf("\n");

				// Print out device features
				printf("Supported Features:\n");
				vk::PhysicalDeviceFeatures deviceFeatures = physicalDevices.at(i).getFeatures();
				if (deviceFeatures.shaderClipDistance == VK_TRUE) printf("Shader Clip Distance\n");
				if (deviceFeatures.textureCompressionBC == VK_TRUE) printf("BC Texture Compression\n");

				printf("\n");

				// Print out device limits
				printf("Device Limits:\n");
				printf("Max Vertex Input Attributes: %u\n", deviceProperties.limits.maxVertexInputAttributes);
				printf("Max Push Constants Size: %u\n", deviceProperties.limits.maxPushConstantsSize);

				// Readability
				printf("\n---\n\n");
			}
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugReportFlagsEXT      flags,
			VkDebugReportObjectTypeEXT objectType,
			uint64_t                   /*object*/,
			size_t                     /*location*/,
			int32_t                    messageCode,
			const char*                pLayerPrefix,
			const char*                pMessage,
			void*                      /*pUserData*/)
		{
			if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
				CoreLib::Diagnostics::Debug::Write("ERROR");
			if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
				CoreLib::Diagnostics::Debug::Write("WARNING");
			if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
				CoreLib::Diagnostics::Debug::Write("PERFORMANCE");
			if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
				CoreLib::Diagnostics::Debug::Write("INFO");
			if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
				CoreLib::Diagnostics::Debug::Write("DEBUG");

			CoreLib::Diagnostics::Debug::Write(" [");
			CoreLib::Diagnostics::Debug::Write(pLayerPrefix);
			CoreLib::Diagnostics::Debug::Write("] ");

			CoreLib::Diagnostics::Debug::Write("(");
			CoreLib::Diagnostics::Debug::Write(to_string((vk::DebugReportObjectTypeEXT)objectType).c_str());
			CoreLib::Diagnostics::Debug::Write(") ");
			CoreLib::Diagnostics::Debug::Write((long long)messageCode);
			CoreLib::Diagnostics::Debug::Write(" ");
			CoreLib::Diagnostics::Debug::WriteLine(pMessage);
			return VK_FALSE;
		}
	}

	class DescriptorPoolObject : public CoreLib::Object
	{
	public:
		vk::DescriptorPool pool;
		DescriptorPoolObject();
		~DescriptorPoolObject();
	};

	/*
	* Internal Vulkan state
	*/
	class RendererState
	{
		//TODO: check class for multithreading safety
	private:
		bool initialized = false;
		int rendererCount = 0;
		vk::DebugReportCallbackEXT callback;

		vk::Instance instance;
		vk::PhysicalDevice physicalDevice;
		vk::Device device;
		vk::DescriptorSetLayout emptyDescriptorSetLayout;
		vk::DescriptorSet emptyDescriptorSet;
		vk::CommandPool swapchainCommandPool;
		CoreLib::Array<vk::CommandPool, MaxRenderThreads> renderCommandPools;
        CoreLib::Array<CoreLib::RefPtr<CoreLib::List<CoreLib::List<vk::CommandBuffer>>>, MaxRenderThreads> renderCommandBufferPools;
        int currentBufferVersions[MaxRenderThreads] = {};
        int renderCommandBufferAllocPtrs[MaxRenderThreads] = {};

		int renderQueueIndex;
		int transferQueueIndex;
		vk::Queue presentQueue;
		CoreLib::Array<vk::Queue, MaxRenderThreads> renderQueues;
		vk::Queue transferQueue;

		vk::PipelineCache pipelineCache;
        CoreLib::String pipelineCacheLocation;

		CoreLib::Array<CoreLib::RefPtr<CoreLib::List<DescriptorPoolObject*>>, MaxRenderThreads> descriptorPoolChains;

		RendererState() {}

		static vk::CommandBuffer GetTempCommandBuffer(vk::CommandPool cmdPool, CoreLib::List<CoreLib::List<vk::CommandBuffer>> & bufferPool, int & allocPtr)
		{
			if (allocPtr == bufferPool[State().currentBufferVersions[renderThreadId]].Count())
			{
				bufferPool[State().currentBufferVersions[renderThreadId]].Add(CreateCommandBuffer(cmdPool, vk::CommandBufferLevel::ePrimary));
			}
			return bufferPool[State().currentBufferVersions[renderThreadId]][allocPtr++];
		}

		static RendererState& State()
		{
			static RendererState singleton;
			return singleton;
		}

		static void CreateDebugCallback()
		{
			vk::DebugReportCallbackCreateInfoEXT callbackCreateInfo(
				vk::DebugReportFlagsEXT(
					//vk::DebugReportFlagBitsEXT::eInformation |
					vk::DebugReportFlagBitsEXT::eDebug |
					vk::DebugReportFlagBitsEXT::ePerformanceWarning |
					vk::DebugReportFlagBitsEXT::eWarning |
					vk::DebugReportFlagBitsEXT::eError
				),
				&VkDebug::DebugCallback,
				nullptr // userdata
			);

			State().callback = State().instance.createDebugReportCallbackEXT(callbackCreateInfo);
		}

		static void CreateInstance()
		{
			// Application and Instance Info
			vk::ApplicationInfo appInfo = vk::ApplicationInfo()
				.setPApplicationName("lll")
				.setPEngineName("Engine")
				.setEngineVersion(1)
				.setApiVersion(VK_API_VERSION_1_0);

			// Enabled Layers
			CoreLib::List<const char*> enabledInstanceLayers;
#if _DEBUG
			bool hasValidationLayer = false;
			auto supportedLayers = vk::enumerateInstanceLayerProperties();
			for (auto & l : supportedLayers)
				if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
				{
					hasValidationLayer = true;
					break;
				}
#if USE_VALIDATION_LAYER
			if (hasValidationLayer)
				enabledInstanceLayers.Add("VK_LAYER_KHRONOS_validation");
#endif
#endif
			// Enabled Extensions
			CoreLib::List<const char*> enabledInstanceExtensions;
			enabledInstanceExtensions.Add(VK_KHR_SURFACE_EXTENSION_NAME);
            enabledInstanceExtensions.Add(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#if _WIN32
			enabledInstanceExtensions.Add(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
			enabledInstanceExtensions.Add(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
			DEBUG_ONLY(enabledInstanceExtensions.Add(VK_EXT_DEBUG_REPORT_EXTENSION_NAME));

			// Instance Create Info
			vk::InstanceCreateInfo instInfo = vk::InstanceCreateInfo()
				.setPApplicationInfo(&appInfo)
				.setEnabledLayerCount(enabledInstanceLayers.Count())
				.setPpEnabledLayerNames(enabledInstanceLayers.Buffer())
				.setEnabledExtensionCount(enabledInstanceExtensions.Count())
				.setPpEnabledExtensionNames(enabledInstanceExtensions.Buffer());

			// Create the instance
			State().instance = vk::createInstance(instInfo);
			
			// Load instance level function pointers
			volkLoadInstance((VkInstance)(State().instance));

			// Create callbacks
			DEBUG_ONLY(CreateDebugCallback());
		}

		static void SelectPhysicalDevice()
		{
			std::vector<vk::PhysicalDevice> physicalDevices = State().instance.enumeratePhysicalDevices();
			//DEBUG_ONLY(VkDebug::PrintDeviceInfo(physicalDevices));
			if (GpuId >= physicalDevices.size())
				GpuId = 0;
			State().physicalDevice = physicalDevices[GpuId];
			printf("Using GPU %d\n", GpuId);
		}

		static void SelectPhysicalDevice(vk::SurfaceKHR surface)
		{
			std::vector<vk::PhysicalDevice> physicalDevices = State().instance.enumeratePhysicalDevices();
			//DEBUG_ONLY(VkDebug::PrintDeviceInfo(physicalDevices));

			int k = 0;
			for (auto physDevice : physicalDevices)
			{
				for (size_t i = 0; i < physDevice.getQueueFamilyProperties().size(); i++)
				{
					if (physDevice.getSurfaceSupportKHR((uint32_t)i, surface))
					{
						State().physicalDevice = physDevice;
						GpuId = k;
						printf("Using GPU %d\n", GpuId);
						return;
					}
				}
				k++;
			}
		}

		static void CreateDevice()
		{
			// Initialize Queues
			CoreLib::List<float> renderQueuePriorities;
			CoreLib::List<float> transferQueuePriorities;

			renderQueuePriorities.Add(1.0f);

			std::vector<vk::QueueFamilyProperties> queueFamilyProperties = PhysicalDevice().getQueueFamilyProperties();
            MaxThreadCount = Math::Min(MaxThreadCount, (int)queueFamilyProperties[0].queueCount);
            for (int i = 0; i < MaxThreadCount - 1; i++)
			{
				renderQueuePriorities.Add(1.0f);
			}
			//transferQueuePriorities.Add(1.0f);

			//TODO: improve
			int renderQueueFamilyIndex = 0;
			int transferQueueFamilyIndex = 0;
			//NOTE: When changing this, resources created by the transferQueue will also need to have
			//      their ownership transferred to the renderQueue, if they are created with 
			//      vk::SharingMode::eExclusive. https://www.khronos.org/registry/vulkan/specs/1.0/xhtml/vkspec.html#resources-sharing

			CoreLib::List<vk::DeviceQueueCreateInfo> deviceQueueInfoVec;
			deviceQueueInfoVec.Add(
				vk::DeviceQueueCreateInfo()
				.setQueueFamilyIndex(renderQueueFamilyIndex)
				.setQueueCount(renderQueuePriorities.Count())
				.setPQueuePriorities(renderQueuePriorities.Buffer())
			);
			//deviceQueueInfoVec.Add(
			//	vk::DeviceQueueCreateInfo()
			//	.setQueueFamilyIndex(transferQueueFamilyIndex)
			//	.setQueueCount(transferQueuePriorities.Count())
			//	.setPQueuePriorities(transferQueuePriorities.Buffer())
			//);

			// Lambda to check if layer is present and then add it
			auto AddLayer = [](CoreLib::List<const char*>& enabledDeviceLayers, const char* layerName)
			{
				for (auto layer : State().physicalDevice.enumerateDeviceLayerProperties())
				{
					if (!strcmp(layerName, layer.layerName))
					{
						enabledDeviceLayers.Add(layerName);
						return;
					}
				}
				printf("Layer %s not supported\n", layerName);
			};
			CoreLib::List<const char*> enabledDeviceLayers;
#if USE_VALIDATION_LAYER
			DEBUG_ONLY(AddLayer(enabledDeviceLayers, "VK_LAYER_LUNARG_standard_validation"));
#endif
			// Lambda to check if extension is present and then add it
			auto AddExtension = [](CoreLib::List<const char*>& enabledDeviceExtensions, const char* extensionName)
			{
				for (auto extension : State().physicalDevice.enumerateDeviceExtensionProperties())
				{
					if (!strcmp(extensionName, extension.extensionName))
					{
						enabledDeviceExtensions.Add(extensionName);
						return;
					}
				}
				printf("Extension %s not supported\n", extensionName);
			};
			CoreLib::List<const char*> enabledDeviceExtensions;
			AddExtension(enabledDeviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME); 
            AddExtension(enabledDeviceExtensions, VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
			DEBUG_ONLY(AddExtension(enabledDeviceExtensions, VK_EXT_DEBUG_MARKER_EXTENSION_NAME));

			// Device Features
			vk::PhysicalDeviceFeatures enabledDeviceFeatures = State().physicalDevice.getFeatures();

			vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo()
				.setQueueCreateInfoCount(deviceQueueInfoVec.Count())
				.setPQueueCreateInfos(deviceQueueInfoVec.Buffer())
				.setEnabledLayerCount(enabledDeviceLayers.Count())
				.setPpEnabledLayerNames(enabledDeviceLayers.Buffer())
				.setEnabledExtensionCount(enabledDeviceExtensions.Count())
				.setPpEnabledExtensionNames(enabledDeviceExtensions.Buffer())
				.setPEnabledFeatures(&enabledDeviceFeatures);

			State().device = State().physicalDevice.createDevice(deviceInfo);

			// Load device level function pointers
			volkLoadDevice((VkDevice)(State().device));

			State().presentQueue = State().device.getQueue(renderQueueFamilyIndex, 0);
            State().renderQueues.SetSize(MaxThreadCount);
            for (int i = 0; i < renderQueuePriorities.Count(); i++)
            {
			    State().renderQueues[i] = State().device.getQueue(renderQueueFamilyIndex, i);
            }
			State().transferQueue = State().device.getQueue(transferQueueFamilyIndex, renderQueuePriorities.Count() - 1);//TODO: Change the index if changing family
		
		}

		static void CreateCommandPool()
		{
			vk::CommandPoolCreateInfo commandPoolCreateInfo = vk::CommandPoolCreateInfo()
				.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
				.setQueueFamilyIndex(State().renderQueueIndex);

			State().swapchainCommandPool = State().device.createCommandPool(commandPoolCreateInfo);
            
			//TODO: multiple pools for multiple threads
			vk::CommandPoolCreateInfo renderCommandPoolCreateInfo = vk::CommandPoolCreateInfo()
				.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
				.setQueueFamilyIndex(State().renderQueueIndex);

            State().renderCommandPools.SetSize(MaxThreadCount);
            for (int i = 0; i < MaxThreadCount; i++)
			    State().renderCommandPools[i] = State().device.createCommandPool(renderCommandPoolCreateInfo);

            State().renderCommandBufferPools.SetSize(MaxThreadCount);
            for (int i = 0; i < MaxThreadCount; i++)
                State().renderCommandBufferPools[i] = new List<List<vk::CommandBuffer>>();

		}

		static void CreateDescriptorPoolChain()
		{
            State().descriptorPoolChains.SetSize(MaxThreadCount);
            for (int i = 0; i < MaxThreadCount; i++)
            {
			    State().descriptorPoolChains[i] = new CoreLib::List<DescriptorPoolObject*>();
            }
		}

		// This function encapsulates all device-specific initialization
		static void InitDevice()
		{
			CreateDevice();
			CreateCommandPool();
			CreateDescriptorPoolChain();
            List<unsigned char> initialData;
            if (File::Exists(State().pipelineCacheLocation))
            {
                initialData = File::ReadAllBytes(State().pipelineCacheLocation);
            }
			vk::PipelineCacheCreateInfo createInfo = vk::PipelineCacheCreateInfo()
				.setInitialDataSize(initialData.Count())
				.setPInitialData(initialData.Buffer());

			State().pipelineCache = State().device.createPipelineCache(createInfo);
		}

		static void Init()
		{
			if (State().initialized)
				return;
            renderThreadId = 0;
			State().initialized = true;
            if (volkInitialize() != VK_SUCCESS)
			{
				printf("Fatal error: Vulkan driver not found on the current system.\n");
				exit(-1);
			}
			CreateInstance();
			SelectPhysicalDevice();
			InitDevice();
			vk::DescriptorSetLayoutCreateInfo descLayoutCreateInfo;
			descLayoutCreateInfo.setBindingCount(0);
			State().emptyDescriptorSetLayout = State().device.createDescriptorSetLayout(descLayoutCreateInfo);
			State().emptyDescriptorSet = AllocateDescriptorSet(State().emptyDescriptorSetLayout).second;
		}

		static void DestroyDevice()
		{
			State().device.destroyDescriptorSetLayout(State().emptyDescriptorSetLayout);
			State().device.destroy();
		}

		static void DestroyInstance()
		{
			DEBUG_ONLY(State().instance.destroyDebugReportCallbackEXT(State().callback));
			State().instance.destroy();
		}

		static void DestroyCommandPool()
		{
            for (int i = 0; i < MaxThreadCount; i++)
			    State().device.destroyCommandPool(State().renderCommandPools[i]);
			State().device.destroyCommandPool(State().swapchainCommandPool);

            for (auto & pool : State().renderCommandBufferPools)
                pool = nullptr;
		}

		static void DestroyDescriptorPoolChain()
		{
            for (auto & chain : State().descriptorPoolChains)
            {
			    for (auto& descriptorPool : *chain)
				    delete descriptorPool;
                chain = nullptr;
            }
		}

		// This function encapsulates all device-specific destruction
		static void UninitDevice()
		{
			State().device.waitIdle();
            if (State().pipelineCacheLocation.Length())
            {
                const size_t maxPipelineCacheSize = (64 << 20); // limit cache size to 64MB
                size_t size = 0;
                vkGetPipelineCacheData(State().device, State().PipelineCache(), &size, nullptr);
                if (size > maxPipelineCacheSize)
                    size = maxPipelineCacheSize;
                List<unsigned char> buffer;
                buffer.SetSize((int)size);
                vkGetPipelineCacheData(State().device, State().PipelineCache(), &size, buffer.Buffer());
                File::WriteAllBytes(State().pipelineCacheLocation, buffer.Buffer(), (size_t)buffer.Count());
            }
			State().device.destroyPipelineCache(State().pipelineCache);
			DestroyDescriptorPoolChain();
			DestroyCommandPool();
			DestroyDevice();
		}


		static void Destroy()
		{
			if (!State().initialized)
				return;

			UninitDevice();
			DestroyInstance();
            State().SetPipelineCacheLocation(String());
			State().initialized = false;
		};

		static void DeviceLost()
		{
			UninitDevice();
			SelectPhysicalDevice();
			InitDevice();
			//TODO: send message to user of engine that signals to recreate resources
		}

	public:
		RendererState(RendererState const&) = delete;
		void operator=(RendererState const&) = delete;

		~RendererState()
		{
			Destroy();//TODO: Does this need to be here?
		}

		// Const reference access to class variables
		static const vk::Instance& Instance()
		{
			return State().instance;
		}

		static vk::DescriptorSet GetEmptyDescriptorSet()
		{
			return State().emptyDescriptorSet;
		}

		static const vk::PhysicalDevice& PhysicalDevice()
		{
			return State().physicalDevice;
		}

		static const vk::Device& Device()
		{
			return State().device;
		}

		static void Init(int versionCount)
		{
			auto & state = State();
            for (int i = 0; i < MaxThreadCount; i++)
			    state.renderCommandBufferPools[i]->SetSize(versionCount);
		}
		static void ResetTempBufferVersion(int version)
		{
			auto & state = State();
			state.currentBufferVersions[renderThreadId] = version;
			state.renderCommandBufferAllocPtrs[renderThreadId] = 0;
		}

		static const vk::CommandPool& SwapchainCommandPool()
		{
			//TODO: do I want to expose these command pools?
			return State().swapchainCommandPool;
		}

		static const vk::CommandPool& TransferCommandPool()
		{
            return RenderCommandPool();
		}

		static const vk::CommandPool& RenderCommandPool()
		{
			return State().renderCommandPools[renderThreadId];
		}

		static vk::CommandBuffer GetTempTransferCommandBuffer()
		{
            return GetTempRenderCommandBuffer();
		}

		static vk::CommandBuffer GetTempRenderCommandBuffer()
		{
			return GetTempCommandBuffer(State().renderCommandPools[renderThreadId], *State().renderCommandBufferPools[renderThreadId], State().renderCommandBufferAllocPtrs[renderThreadId]);
		}

		static const vk::Queue& TransferQueue()
		{
			return RenderQueue();
		}

		static const vk::Queue& RenderQueue()
		{
			return State().renderQueues[renderThreadId];
		}

		static const vk::Queue& PresentQueue()
		{
			return State().presentQueue;
		}

		static const vk::PipelineCache& PipelineCache()
		{
			return State().pipelineCache;
		}

		static const vk::DescriptorPool& DescriptorPool()
		{
			if (State().descriptorPoolChains[renderThreadId]->Count() == 0)
				State().descriptorPoolChains[renderThreadId]->Add(new DescriptorPoolObject());

			return State().descriptorPoolChains[renderThreadId]->Last()->pool;
		}

		static std::pair<vk::DescriptorPool, vk::DescriptorSet> AllocateDescriptorSet(vk::DescriptorSetLayout layout, bool canTryAgain = true)
		{
			//TODO: add counter mechanism to DescriptorPoolObject so we know when to destruct
			std::pair<vk::DescriptorPool, vk::DescriptorSet> res;
			res.first = State().DescriptorPool();

			// Create Descriptor Set
			vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo = vk::DescriptorSetAllocateInfo()
				.setDescriptorPool(res.first)
				.setDescriptorSetCount(1)
				.setPSetLayouts(&layout);

			auto err = RendererState::Device().allocateDescriptorSets(&descriptorSetAllocateInfo, &res.second);
			if (err != vk::Result::eSuccess)
			{
				if (canTryAgain)
				{
					RendererState::State().descriptorPoolChains[renderThreadId]->Add(new DescriptorPoolObject());
					return AllocateDescriptorSet(layout, false);
				}
				else CORELIB_ABORT("Couldn't allocate descriptor set.");
			}
			else return res;
		}


        static void SetPipelineCacheLocation(String loc)
        {
            State().pipelineCacheLocation = loc;
        }

		// Bookkeeping for multiple instances of HardwareRenderer
		static void AddRenderer()
		{
			if (!State().initialized)
				Init();

			State().rendererCount++;
		}

		static void RemRenderer()
		{
			State().rendererCount--;

			if (RendererCount() == 0)
				Destroy();//TODO: Should I destroy the state here?
		}

		static int RendererCount()
		{
			return State().rendererCount;
		}

		// Resource creation functions
		static vk::SurfaceKHR CreateSurface(WindowHandle windowHandle)
		{
			// Create the surface.
			vk::SurfaceKHR surface;
#if _WIN32
			vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo = vk::Win32SurfaceCreateInfoKHR()
				.setHwnd((HWND)windowHandle)
				.setHinstance(GetModuleHandle(NULL));

			surface = State().instance.createWin32SurfaceKHR(surfaceCreateInfo);
#elif __linux__
			vk::XlibSurfaceCreateInfoKHR surfaceCreateInfo = vk::XlibSurfaceCreateInfoKHR()
				.setWindow(windowHandle.window)
				.setDpy((Display*)windowHandle.display);
			surface = State().instance.createXlibSurfaceKHR(surfaceCreateInfo);
#elif __ANDROID__
			vk::AndroidSurfaceCreateInfoKHR surfaceCreateInfo = vk::AndroidSurfaceCreaetInfoKHR()
				.setWindow((ANativeWindow*)window);

			surface = State().instance.createAndroidSurfaceKHR(surfaceCreateInfo);
#endif

			// Check to see if the current physical device supports the surface
			bool supported = false;
			for (unsigned int i = 0; i < State().physicalDevice.getQueueFamilyProperties().size(); i++)
			{
				if (State().physicalDevice.getSurfaceSupportKHR(i, surface))
				{
					supported = true;
					break;
				}
			}

			// If not supported, recreate logical device with a compatible physical device
			if (!supported)
			{
				printf("GPU %d does not support current surface\n", GpuId);
				UninitDevice();
				SelectPhysicalDevice(surface);
				InitDevice();
				//TODO: Notify user to recreate any resources they have created prior to this point
			}

			return surface;
		}

		//TODO: I should probably wrap command buffers in a class that contain both the handle and a reference to the command pool created from
		static vk::CommandBuffer CreateCommandBuffer(vk::CommandPool commandPool, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary)
		{
			vk::CommandBufferAllocateInfo commandBufferAllocateInfo = vk::CommandBufferAllocateInfo()
				.setCommandPool(commandPool)
				.setLevel(level)
				.setCommandBufferCount(1);

			vk::CommandBuffer commandBuffer;

			//TODO: lock mutex on commandPool or allocate commandPool per thread
			commandBuffer = State().device.allocateCommandBuffers(commandBufferAllocateInfo).front();
			//TODO: unlock mutex on commandPool

			return commandBuffer;
		}
		static vk::CommandBuffer DestroyCommandBuffer(vk::CommandPool commandPool, vk::CommandBuffer commandBuffer)
		{
			//TODO: lock mutex on commandPool
			State().device.freeCommandBuffers(commandPool, commandBuffer);
			//TODO: unlock mutex on commandPool

			return commandBuffer;
		}
	};

	DescriptorPoolObject::DescriptorPoolObject() {
		CoreLib::List<vk::DescriptorPoolSize> poolSizes;
		poolSizes.Add(vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 200));
		poolSizes.Add(vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 20000));
		poolSizes.Add(vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 5000));
		poolSizes.Add(vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2500));
        poolSizes.Add(vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 32));

		vk::DescriptorPoolCreateInfo poolCreateInfo = vk::DescriptorPoolCreateInfo()
			.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
			.setMaxSets(5000)
			.setPoolSizeCount(poolSizes.Count())
			.setPPoolSizes(poolSizes.Buffer());

		pool = RendererState::Device().createDescriptorPool(poolCreateInfo);
	}
	DescriptorPoolObject::~DescriptorPoolObject()
	{
		if (pool) RendererState::Device().destroyDescriptorPool(pool);
	}

	/*
	* Interface to Vulkan translation functions
	*/
	vk::Format TranslateStorageFormat(StorageFormat format)
	{
		switch (format)
		{
		case StorageFormat::R_F16: return vk::Format::eR16Sfloat;
		case StorageFormat::R_F32: return vk::Format::eR32Sfloat;
		case StorageFormat::R_I8: return vk::Format::eR8Uint;
		case StorageFormat::R_I16: return vk::Format::eR16Uint;
		case StorageFormat::R_8: return vk::Format::eR8Unorm;
		case StorageFormat::R_16: return vk::Format::eR16Unorm;
		case StorageFormat::Int32_Raw: return vk::Format::eR32Sint;//
		case StorageFormat::RG_F16: return vk::Format::eR16G16Sfloat;
		case StorageFormat::RG_F32: return vk::Format::eR32G32Sfloat;
		case StorageFormat::RG_I8: return vk::Format::eR8G8Uint;
		case StorageFormat::RG_8: return vk::Format::eR8G8Unorm;
		case StorageFormat::RG_16: return vk::Format::eR16G16Unorm;
		case StorageFormat::RG_I16: return vk::Format::eR16G16Uint;
		case StorageFormat::RG_I32_Raw: return vk::Format::eR32G32Sint;//
		case StorageFormat::RGBA_F16: return vk::Format::eR16G16B16A16Sfloat;
		case StorageFormat::RGBA_F32: return vk::Format::eR32G32B32A32Sfloat;
		case StorageFormat::RGBA_8: return vk::Format::eR8G8B8A8Unorm;
        case StorageFormat::RGBA_8_SRGB: return vk::Format::eR8G8B8A8Srgb;
		case StorageFormat::RGBA_16: return vk::Format::eR16G16B16A16Snorm;
		case StorageFormat::RGBA_I8: return vk::Format::eR8G8B8A8Uint;
		case StorageFormat::RGBA_I16: return vk::Format::eR16G16B16A16Uint;
		case StorageFormat::RGBA_I32_Raw: return vk::Format::eR32G32B32A32Sint;//
		case StorageFormat::BC1: return vk::Format::eBc1RgbUnormBlock;//
        case StorageFormat::BC1_SRGB: return vk::Format::eBc1RgbSrgbBlock;//
		case StorageFormat::BC5: return vk::Format::eBc5UnormBlock;
		case StorageFormat::BC3: return vk::Format::eBc3UnormBlock;
        case StorageFormat::BC6H: return vk::Format::eBc6HUfloatBlock;
		case StorageFormat::RGBA_Compressed: return vk::Format::eBc7UnormBlock;//
		case StorageFormat::R11F_G11F_B10F: return vk::Format::eB10G11R11UfloatPack32; // need to swizzle?
		case StorageFormat::RGB10_A2: return vk::Format::eA2R10G10B10UnormPack32;//
		case StorageFormat::Depth24: return vk::Format::eX8D24UnormPack32;
		case StorageFormat::Depth32: return vk::Format::eD32Sfloat;
		case StorageFormat::Depth24Stencil8: return vk::Format::eD24UnormS8Uint;
		default: CORELIB_NOT_IMPLEMENTED("TranslateStorageFormat");
		}
	};

	vk::BufferUsageFlags TranslateUsageFlags(BufferUsage usage)
	{
		switch (usage)
		{
		case BufferUsage::ArrayBuffer: return vk::BufferUsageFlagBits::eVertexBuffer;
		case BufferUsage::IndexBuffer: return vk::BufferUsageFlagBits::eIndexBuffer;
		case BufferUsage::StorageBuffer: return vk::BufferUsageFlagBits::eStorageBuffer;
		case BufferUsage::UniformBuffer: return vk::BufferUsageFlagBits::eUniformBuffer;
		default: CORELIB_NOT_IMPLEMENTED("TranslateUsageFlags");
		}
	}

	vk::Format TranslateVertexAttribute(VertexAttributeDesc attribute)
	{
		switch (attribute.Type)
		{
		case DataType::Byte:
			return attribute.Normalized ? vk::Format::eR8Unorm : vk::Format::eR8Uint;
		case DataType::Char:
			return attribute.Normalized ? vk::Format::eR8Snorm : vk::Format::eR8Sint;
		case DataType::Byte2:
			return attribute.Normalized ? vk::Format::eR8G8Unorm : vk::Format::eR8G8Uint;
		case DataType::Char2:
			return attribute.Normalized ? vk::Format::eR8G8Snorm : vk::Format::eR8G8Sint;
		case DataType::Byte3:
			return attribute.Normalized ? vk::Format::eR8G8B8Unorm : vk::Format::eR8G8B8Uint;
		case DataType::Char3:
			return attribute.Normalized ? vk::Format::eR8G8B8Snorm : vk::Format::eR8G8B8Sint;
		case DataType::Byte4:
			return attribute.Normalized ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Uint;
		case DataType::Char4:
			return attribute.Normalized ? vk::Format::eR8G8B8A8Snorm : vk::Format::eR8G8B8A8Sint;
		case DataType::Short:
			return attribute.Normalized ? vk::Format::eR16Snorm : vk::Format::eR16Sint;
		case DataType::UShort:
			return attribute.Normalized ? vk::Format::eR16Unorm : vk::Format::eR16Uint;
		case DataType::Half:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR16Sfloat;//
		case DataType::Short2:
			return attribute.Normalized ? vk::Format::eR16G16Snorm : vk::Format::eR16G16Sint;
		case DataType::UShort2:
			return attribute.Normalized ? vk::Format::eR16G16Unorm : vk::Format::eR16G16Uint;
		case DataType::Half2:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR16G16Sfloat;//
		case DataType::Short3:
			return attribute.Normalized ? vk::Format::eR16G16B16Snorm : vk::Format::eR16G16B16Sint;
		case DataType::UShort3:
			return attribute.Normalized ? vk::Format::eR16G16B16Unorm : vk::Format::eR16G16B16Uint;
		case DataType::Half3:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR16G16B16Sfloat;//
		case DataType::Short4:
			return attribute.Normalized ? vk::Format::eR16G16B16A16Snorm : vk::Format::eR16G16B16A16Sint;
		case DataType::UShort4:
			return attribute.Normalized ? vk::Format::eR16G16B16A16Unorm : vk::Format::eR16G16B16A16Uint;
		case DataType::Half4:
			return attribute.Normalized ? vk::Format::eR16G16B16A16Snorm : vk::Format::eR16G16B16A16Sfloat;//
		case DataType::Int:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32Sint;
		case DataType::UInt:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32Uint;
		case DataType::Float:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32Sfloat;
		case DataType::Int2:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32G32Sint;
		case DataType::Float2:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32G32Sfloat;
		case DataType::Int3:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32G32B32Sint;
		case DataType::Float3:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32G32B32Sfloat;
		case DataType::Int4:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32G32B32A32Sint;
		case DataType::UInt4_10_10_10_2:
			throw HardwareRendererException("Unsupported data type.");
		case DataType::Float4:
			return attribute.Normalized ? throw HardwareRendererException("Unsupported data type.") : vk::Format::eR32G32B32Sfloat;
		default:
			throw HardwareRendererException("Unimplemented data type.");
		}
	}

	vk::DescriptorType TranslateBindingType(BindingType bindType)
	{
        switch (bindType)
        {
        case BindingType::Texture:
            return vk::DescriptorType::eSampledImage;
        case BindingType::StorageTexture:
            return vk::DescriptorType::eStorageImage;
        case BindingType::Sampler:
            return vk::DescriptorType::eSampler;
        case BindingType::UniformBuffer:
            return vk::DescriptorType::eUniformBuffer;
        case BindingType::StorageBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case BindingType::RWStorageBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case BindingType::Unused:
            throw HardwareRendererException("Attempting to use unused binding");
        default:
            CORELIB_NOT_IMPLEMENTED("TranslateBindingType");
        }
	}

	vk::ShaderStageFlags TranslateStageFlags(StageFlags flag)
	{
		switch (flag)
		{
		case StageFlags::sfVertex: return vk::ShaderStageFlagBits::eVertex;
		case StageFlags::sfFragment: return vk::ShaderStageFlagBits::eFragment;
		case StageFlags::sfGraphics:return vk::ShaderStageFlagBits::eAllGraphics;
		case StageFlags::sfCompute: return vk::ShaderStageFlagBits::eCompute;
        case StageFlags::sfGraphicsAndCompute: return vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eAllGraphics;
		default: CORELIB_NOT_IMPLEMENTED("TranslateStageFlags");
		}
	}

	vk::ImageLayout LayoutFromUsage(TextureUsage usage)
	{
		//TODO: fix this function to deal with the mask correctly
		switch (usage)
		{
		case TextureUsage::ColorAttachment:
		case TextureUsage::SampledColorAttachment:
			return vk::ImageLayout::eColorAttachmentOptimal;
		case TextureUsage::DepthAttachment:
		case TextureUsage::SampledDepthAttachment:
			return vk::ImageLayout::eDepthStencilAttachmentOptimal;
		case TextureUsage::Sampled:
			return vk::ImageLayout::eShaderReadOnlyOptimal;
        case TextureUsage::Storage:
        case TextureUsage::SampledStorage:
            return vk::ImageLayout::eGeneral;
		default: CORELIB_NOT_IMPLEMENTED("LayoutFromUsage");
		}
	}

	vk::CompareOp TranslateCompareFunc(CompareFunc compareFunc)
	{
		switch (compareFunc)
		{
		case CompareFunc::Disabled: return vk::CompareOp::eNever;
		case CompareFunc::Equal: return vk::CompareOp::eEqual;
		case CompareFunc::Less: return vk::CompareOp::eLess;
		case CompareFunc::Greater: return vk::CompareOp::eGreater;
		case CompareFunc::LessEqual: return vk::CompareOp::eLessOrEqual;
		case CompareFunc::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
		case CompareFunc::NotEqual: return vk::CompareOp::eNotEqual;
		case CompareFunc::Always: return vk::CompareOp::eAlways;
		case CompareFunc::Never: return vk::CompareOp::eNever;
		default: CORELIB_NOT_IMPLEMENTED("TranslateCompareFunc");
		}
	}

	vk::StencilOp TranslateStencilOp(StencilOp stencilOp)
	{
		switch (stencilOp)
		{
		case StencilOp::Keep: return vk::StencilOp::eKeep;
		case StencilOp::Zero: return vk::StencilOp::eZero;
		case StencilOp::Replace: return vk::StencilOp::eReplace;
		case StencilOp::Increment: return vk::StencilOp::eIncrementAndClamp;
		case StencilOp::IncrementWrap: return vk::StencilOp::eIncrementAndWrap;
		case StencilOp::Decrement: return vk::StencilOp::eDecrementAndClamp;
		case StencilOp::DecrementWrap: return vk::StencilOp::eDecrementAndWrap;
		case StencilOp::Invert: return vk::StencilOp::eInvert;
		default: CORELIB_NOT_IMPLEMENTED("TranslateStencilOp");
		}
	}

	vk::CullModeFlags TranslateCullMode(CullMode mode)
	{
		switch (mode)
		{
		case CullMode::CullBackFace: return vk::CullModeFlagBits::eBack;
		case CullMode::CullFrontFace: return vk::CullModeFlagBits::eFront;
		case CullMode::Disabled: return vk::CullModeFlagBits::eNone;
		default: throw CoreLib::NotImplementedException("TranslateCullMode");
		}
	}

	vk::PrimitiveTopology TranslatePrimitiveTopology(PrimitiveType ptype)
	{
		switch (ptype)
		{
		case PrimitiveType::Triangles: return vk::PrimitiveTopology::eTriangleList;
		case PrimitiveType::Points: return vk::PrimitiveTopology::ePointList;
		case PrimitiveType::Lines: return vk::PrimitiveTopology::eLineList;
		case PrimitiveType::TriangleStrips: return vk::PrimitiveTopology::eTriangleStrip;
		case PrimitiveType::LineStrips: return vk::PrimitiveTopology::eLineStrip;
		default: CORELIB_NOT_IMPLEMENTED("TranslatePrimitiveTopology");
		}
	}

	vk::AttachmentLoadOp TranslateLoadOp(LoadOp op)
	{
		switch (op)
		{
		case LoadOp::Load: return vk::AttachmentLoadOp::eLoad;
		case LoadOp::Clear: return vk::AttachmentLoadOp::eClear;
		case LoadOp::DontCare: return vk::AttachmentLoadOp::eDontCare;
		default: CORELIB_NOT_IMPLEMENTED("TranslateLoadOp");
		}
	}

	vk::AttachmentStoreOp TranslateStoreOp(StoreOp op)
	{
		switch (op)
		{
		case StoreOp::Store: return vk::AttachmentStoreOp::eStore;
		case StoreOp::DontCare: return vk::AttachmentStoreOp::eDontCare;
		default: CORELIB_NOT_IMPLEMENTED("TranslateStoreOp");
		}
	}

	/*
	* Vulkan helper functions
	*/
	vk::AccessFlags LayoutFlags(vk::ImageLayout layout)
	{
		switch (layout)
		{
		case vk::ImageLayout::eUndefined:
			return vk::AccessFlags();
		case vk::ImageLayout::eGeneral:
			return vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eInputAttachmentRead |
				vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferRead |
				vk::AccessFlagBits::eTransferWrite;
		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eInputAttachmentRead;
		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead;
		case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eShaderRead;
		case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;
		case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;
		case vk::ImageLayout::ePreinitialized:
			return vk::AccessFlagBits::eHostWrite;
		case vk::ImageLayout::ePresentSrcKHR:
			return vk::AccessFlagBits::eMemoryRead;
		default:
			CORELIB_ABORT("Invalid ImageLayout");
		}
	}

	vk::SampleCountFlagBits SampleCount(int samples)
	{
		if (samples <= 0 || (samples & (samples - 1)) != 0)
			CORELIB_ABORT("samples must be a power of 2");

		return (vk::SampleCountFlagBits)samples;
	}

	int32_t GetMemoryType(uint32_t memoryTypeBits, vk::MemoryPropertyFlags memoryPropertyFlags)
	{
		int32_t memoryTypeIndex = 0;
		auto memProperties = RendererState::PhysicalDevice().getMemoryProperties();
		for (uint32_t k = 0; k < 32; k++)
		{
			if ((memoryTypeBits & 1) == 1)
			{
				if ((memProperties.memoryTypes[k].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags)
				{
					memoryTypeIndex = k;
					return memoryTypeIndex;
				}
			}
			memoryTypeBits >>= 1;
		}
		CORELIB_ABORT("Could not find a valid memory type index.");
	}

	vk::ShaderStageFlagBits ShaderStage(ShaderType stage)
	{
		switch (stage)
		{
		case ShaderType::VertexShader: return vk::ShaderStageFlagBits::eVertex;
		case ShaderType::FragmentShader: return vk::ShaderStageFlagBits::eFragment;
		case ShaderType::ComputeShader: return vk::ShaderStageFlagBits::eCompute;
		default: CORELIB_NOT_IMPLEMENTED("ShaderStageFlagBits");
		}
	}

	class Texture : public CoreLib::Object
	{
	public:
		StorageFormat format;
		TextureUsage usage;
		int width;
		int height;
		int depth;
		int mipLevels;
		int arrayLayers;
		int numSamples;
		bool isCubeArray = false;
		vk::Image image;
		CoreLib::List<vk::ImageView> views;
		vk::DeviceMemory memory;
		uint32_t lastLayoutCheckTaskId = 0;
		CoreLib::List<vk::ImageLayout> currentSubresourceLayouts;
		Texture(String name, TextureUsage usage, int width, int height, int depth, int mipLevels, int arrayLayers, int numSamples, StorageFormat format, 
            bool isArray,
            vk::ImageCreateFlags createFlags = vk::ImageCreateFlags())
		{
			this->usage = usage;
			this->format = format;
			this->width = width;
			this->height = height;
			this->depth = depth;
			this->mipLevels = mipLevels;
			this->arrayLayers = arrayLayers;
			this->numSamples = numSamples;
			this->currentSubresourceLayouts.SetSize(arrayLayers * mipLevels);
			for (auto& currentLayout : this->currentSubresourceLayouts)
				currentLayout = vk::ImageLayout::eUndefined;

			// Create texture resources
			vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor;
			vk::ImageUsageFlags usageFlags;
			if (!!(this->usage & TextureUsage::ColorAttachment))
			{
				aspectFlags = vk::ImageAspectFlagBits::eColor;
				usageFlags = vk::ImageUsageFlagBits::eColorAttachment;
			}
			else if (!!(this->usage & TextureUsage::DepthAttachment))
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;
				usageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment;

				if (this->format == StorageFormat::Depth24Stencil8)
					aspectFlags |= vk::ImageAspectFlagBits::eStencil;
			}
			if (!!(this->usage & TextureUsage::Sampled))
			{
				usageFlags |= vk::ImageUsageFlagBits::eSampled;
			}
            if (!!(this->usage & TextureUsage::Storage))
            {
                usageFlags |= vk::ImageUsageFlagBits::eStorage;
            }

			vk::ImageCreateInfo imageCreateInfo = vk::ImageCreateInfo()
				.setFlags(createFlags)
				.setImageType(depth == 1 ? vk::ImageType::e2D : vk::ImageType::e3D)
				.setFormat(TranslateStorageFormat(this->format))
				.setExtent(vk::Extent3D(width, height, depth))
				.setMipLevels(mipLevels)
				.setArrayLayers(arrayLayers)
				.setSamples(SampleCount(numSamples))
				.setTiling(vk::ImageTiling::eOptimal)
				.setUsage(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | usageFlags)
				.setSharingMode(vk::SharingMode::eExclusive)
				.setQueueFamilyIndexCount(0)
				.setPQueueFamilyIndices(nullptr)
				.setInitialLayout(vk::ImageLayout::eUndefined);

			image = RendererState::Device().createImage(imageCreateInfo);

			vk::MemoryRequirements imageMemoryRequirements = RendererState::Device().getImageMemoryRequirements(image);

			vk::MemoryAllocateInfo imageAllocateInfo = vk::MemoryAllocateInfo()
				.setAllocationSize(imageMemoryRequirements.size)
				.setMemoryTypeIndex(GetMemoryType(imageMemoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));

			memory = RendererState::Device().allocateMemory(imageAllocateInfo);
			RendererState::Device().bindImageMemory(image, memory, 0);

			vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
				.setAspectMask(aspectFlags)
				.setBaseMipLevel(0)
				.setLevelCount(mipLevels)
				.setBaseArrayLayer(0)
				.setLayerCount(arrayLayers);

			vk::ImageViewType viewType = vk::ImageViewType::e2D;
			if (depth == 1)
			{
                if (createFlags & vk::ImageCreateFlagBits::eCubeCompatible)
                {
                    if (isArray)
                        viewType = vk::ImageViewType::eCubeArray;
                    else
                        viewType = vk::ImageViewType::eCube;
                }
                else if (isArray)
                    viewType = vk::ImageViewType::e2DArray;
			}
			else viewType = vk::ImageViewType::e3D;

			vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setFlags(vk::ImageViewCreateFlags())
				.setImage(image)
				.setViewType(viewType)
				.setFormat(imageCreateInfo.format)
				.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA))//
				.setSubresourceRange(imageSubresourceRange);

			views.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			if (isDepthFormat(this->format))
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;

				imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(arrayLayers);

				imageViewCreateInfo = vk::ImageViewCreateInfo()
					.setFlags(vk::ImageViewCreateFlags())
					.setImage(image)
					.setViewType(viewType)
					.setFormat(imageCreateInfo.format)
					.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR))//
					.setSubresourceRange(imageSubresourceRange);

				views.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			}
			if (this->format == StorageFormat::Depth24Stencil8)
			{
				aspectFlags = vk::ImageAspectFlagBits::eStencil;

				imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(arrayLayers);

				imageViewCreateInfo = vk::ImageViewCreateInfo()
					.setFlags(vk::ImageViewCreateFlags())
					.setImage(image)
					.setViewType(viewType)
					.setFormat(imageCreateInfo.format)
					.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eR))//
					.setSubresourceRange(imageSubresourceRange);

				views.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			}
#ifdef _DEBUG
			if (vkDebugMarkerSetObjectNameEXT)
			{
				vk::DebugMarkerObjectNameInfoEXT nameInfo = vk::DebugMarkerObjectNameInfoEXT()
					.setObject((uint64_t)(VkImage)image)
					.setObjectType(vk::DebugReportObjectTypeEXT::eImage)
					.setPObjectName(name.Buffer());
				vkDebugMarkerSetObjectNameEXT(RendererState::Device(), &(VkDebugMarkerObjectNameInfoEXT&)nameInfo);
			}
#else
			CORELIB_UNUSED(name);
#endif
		}
		~Texture()
		{
			if (memory) RendererState::Device().freeMemory(memory);
			for (auto view : views) RendererState::Device().destroyImageView(view);
			if (image) RendererState::Device().destroyImage(image);
		}

		void TransferLayout(vk::ImageLayout targetLayout, List<vk::ImageMemoryBarrier>& imageBarriers)
		{
			struct LayoutTransferOp
			{
				vk::ImageSubresourceRange subresourceRange;
				vk::ImageLayout oldLayout;
			};
			CoreLib::ShortList<LayoutTransferOp> transferOps;

			vk::CommandBuffer transferCommandBuffer = RendererState::GetTempTransferCommandBuffer();

			vk::ImageAspectFlags aspectFlags;
			if (isDepthFormat(format))
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;
				if (format == StorageFormat::Depth24Stencil8)
					aspectFlags |= vk::ImageAspectFlagBits::eStencil;
			}
			else
				aspectFlags = vk::ImageAspectFlagBits::eColor;

			// Record command buffer
			transferOps.Clear();
			for (int i = 0; i < arrayLayers; i++)
			{
				for (int j = 0; j < mipLevels; j++)
				{
					int subresourceIndex = i * mipLevels + j;
					if (currentSubresourceLayouts[subresourceIndex] != targetLayout)
					{
						vk::ImageSubresourceRange range = vk::ImageSubresourceRange()
							.setAspectMask(aspectFlags)
							.setBaseMipLevel(j)
							.setLevelCount(1)
							.setBaseArrayLayer(i)
							.setLayerCount(1);
						if (transferOps.Count())
						{
							auto& last = transferOps.Last();
							if (last.subresourceRange.baseArrayLayer == (uint32_t)i &&
								last.oldLayout == currentSubresourceLayouts[subresourceIndex] &&
								last.subresourceRange.baseMipLevel + last.subresourceRange.levelCount == (uint32_t)j)
							{
								last.subresourceRange.levelCount++;
								if (transferOps.Count() > 1)
								{
									auto& last2 = transferOps[transferOps.Count() - 2];
									if (last2.oldLayout == last.oldLayout &&
										last2.subresourceRange.baseArrayLayer + last2.subresourceRange.layerCount == (uint32_t)i &&
										last2.subresourceRange.baseMipLevel == last.subresourceRange.baseMipLevel &&
										last2.subresourceRange.levelCount == last.subresourceRange.levelCount)
									{
										last2.subresourceRange.layerCount++;
										transferOps.SetSize(transferOps.Count() - 1);
									}
								}
								continue;
							}
						}
						transferOps.Add(LayoutTransferOp{ range, currentSubresourceLayouts[subresourceIndex] });
					}
				}
			}
			for (int i = 0; i < transferOps.Count(); i++)
			{
				vk::ImageMemoryBarrier textureUsageBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(LayoutFlags(transferOps[i].oldLayout))
					.setDstAccessMask(LayoutFlags(targetLayout))
					.setOldLayout(transferOps[i].oldLayout)
					.setNewLayout(targetLayout)
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(this->image)
					.setSubresourceRange(transferOps[i].subresourceRange);
				imageBarriers.Add(textureUsageBarrier);
			}
			for (auto& currentLayout : currentSubresourceLayouts)
				currentLayout = targetLayout;
		}

        DataType GetDataTypeElementType(DataType type)
        {
            return (DataType)((int)(type)&(~3));
        }

		void SetData(int level, int layer, int xOffset, int yOffset, int zOffset, int pwidth, int pheight, int pdepth, int layerCount, DataType inputType, void* data)
		{
			CORELIB_ASSERT(numSamples == 1 && "samples must be equal to 1");
			CORELIB_ASSERT(this->mipLevels > level && "Attempted to set mipmap data for invalid level");
			CORELIB_ASSERT(this->mipLevels > level && "Attempted to set layer data for invalid layer");

			vk::ImageAspectFlags aspectFlags;
			if (isDepthFormat(format))
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;
				if (format == StorageFormat::Depth24Stencil8)
					aspectFlags |= vk::ImageAspectFlagBits::eStencil;
			}
			else
				aspectFlags = vk::ImageAspectFlagBits::eColor;

			int dataTypeSize = DataTypeSize(inputType);
			List<unsigned short> translatedBuffer;
			if (data && (format == StorageFormat::R_F16 || format == StorageFormat::RG_F16 || format == StorageFormat::RGBA_F16)
                && (GetDataTypeElementType(inputType) != DataType::Half))
			{
				// transcode f32 to f16
				int channelCount = 1;
				switch (format)
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
				translatedBuffer.SetSize(pwidth * pheight * pdepth * layerCount * channelCount);
				float * src = (float*)data;
				for (int i = 0; i < translatedBuffer.Count(); i++)
					translatedBuffer[i] = FloatToHalf(src[i]);
				dataTypeSize >>= 1;
				data = (void*)translatedBuffer.Buffer();
			}
			
			// Set up staging buffer and copy data to new image
			int bufferSize = pwidth * pheight * pdepth * layerCount * dataTypeSize;
			if (format == StorageFormat::BC1 || format == StorageFormat::BC1_SRGB|| format == StorageFormat::BC5 || format == StorageFormat::BC3 || format == StorageFormat::BC6H)
			{
				int blocks = (int)(ceil(pwidth / 4.0f) * ceil(pheight / 4.0f));
				bufferSize = (format == StorageFormat::BC1||format == StorageFormat::BC1_SRGB) ? blocks * 8 : blocks * 16;
				bufferSize *= layerCount;
			}

			vk::BufferCreateInfo stagingBufferCreateInfo = vk::BufferCreateInfo()
				.setFlags(vk::BufferCreateFlags())
				.setSize(bufferSize)
				.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
				.setSharingMode(vk::SharingMode::eExclusive)
				.setQueueFamilyIndexCount(0)
				.setPQueueFamilyIndices(nullptr);

			vk::Buffer stagingBuffer = RendererState::Device().createBuffer(stagingBufferCreateInfo);

			vk::MemoryRequirements stagingBufferMemoryRequirements = RendererState::Device().getBufferMemoryRequirements(stagingBuffer);

			vk::MemoryAllocateInfo bufferAllocateInfo = vk::MemoryAllocateInfo()
				.setAllocationSize(stagingBufferMemoryRequirements.size)
				.setMemoryTypeIndex(GetMemoryType(stagingBufferMemoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible));

			vk::DeviceMemory stagingMemory = RendererState::Device().allocateMemory(bufferAllocateInfo);

			RendererState::Device().bindBufferMemory(stagingBuffer, stagingMemory, 0);

			void* stagingMappedMemory = RendererState::Device().mapMemory(stagingMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags());
			if (data)
			{
				memcpy(stagingMappedMemory, data, bufferSize);
			}
			else
			{
				memset(stagingMappedMemory, 0, bufferSize);
			}
			RendererState::Device().flushMappedMemoryRanges(vk::MappedMemoryRange(stagingMemory, 0, VK_WHOLE_SIZE));
			RendererState::Device().unmapMemory(stagingMemory);

			// Create buffer image copy information
			vk::BufferImageCopy stagingCopy = vk::BufferImageCopy()
				.setBufferOffset(0)
				.setBufferRowLength(0)
				.setBufferImageHeight(0)
				.setImageSubresource(vk::ImageSubresourceLayers().setAspectMask(aspectFlags).setMipLevel(level).setBaseArrayLayer(layer).setLayerCount(layerCount))
				.setImageOffset(vk::Offset3D(xOffset, yOffset, zOffset))
				.setImageExtent(vk::Extent3D(pwidth, pheight, pdepth));

			// Create command buffer
			//TODO: Use CommandBuffer class?
			vk::CommandBuffer transferCommandBuffer = RendererState::GetTempTransferCommandBuffer();
			// Record command buffer
			vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
				.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
				.setPInheritanceInfo(nullptr);
			transferCommandBuffer.begin(transferBeginInfo);
			List<vk::ImageMemoryBarrier> barriers;
			barriers.Clear();
			for (int i = 0; i < mipLevels; i++)
			{
				vk::ImageSubresourceRange textureSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(i)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(arrayLayers);

				vk::ImageMemoryBarrier textureCopyBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eUndefined))
					.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
					.setOldLayout(vk::ImageLayout::eUndefined)
					.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(this->image)
					.setSubresourceRange(textureSubresourceRange);
				barriers.Add(textureCopyBarrier);
			}
				
			transferCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags(),
				nullptr,
				nullptr,
				vk::ArrayProxy<const vk::ImageMemoryBarrier>(barriers.Count(), barriers.Buffer())
			);
			transferCommandBuffer.copyBufferToImage(
				stagingBuffer,
				this->image,
				vk::ImageLayout::eTransferDstOptimal,
				stagingCopy
			);

			barriers.Clear();
			for (int i = 0; i < mipLevels; i++)
			{
				vk::ImageSubresourceRange textureSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(i)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(arrayLayers);

				vk::ImageMemoryBarrier textureUsageBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
					.setDstAccessMask(LayoutFlags(LayoutFromUsage(this->usage)))
					.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
					.setNewLayout(LayoutFromUsage(this->usage))
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(this->image)
					.setSubresourceRange(textureSubresourceRange);

				for (int l = 0; l < arrayLayers; l++)
				{
					int subresourceIndex = l * mipLevels + i;
					currentSubresourceLayouts[subresourceIndex] = LayoutFromUsage(this->usage);
				}
				barriers.Add(textureUsageBarrier);
			}
			transferCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eAllCommands,
				vk::DependencyFlags(),
				nullptr,
				nullptr,
				vk::ArrayProxy<const vk::ImageMemoryBarrier>(barriers.Count(), barriers.Buffer())
			);
			transferCommandBuffer.end();

			// Submit to queue
			vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
				.setWaitSemaphoreCount(0)
				.setPWaitSemaphores(nullptr)
				.setPWaitDstStageMask(nullptr)
				.setCommandBufferCount(1)
				.setPCommandBuffers(&transferCommandBuffer)
				.setSignalSemaphoreCount(0)
				.setPSignalSemaphores(nullptr);

			RendererState::TransferQueue().submit(transferSubmitInfo, vk::Fence());
			RendererState::TransferQueue().waitIdle();

			// Destroy staging resources
			RendererState::Device().freeMemory(stagingMemory);
			RendererState::Device().destroyBuffer(stagingBuffer);
			for (auto& currentLayout : currentSubresourceLayouts)
				currentLayout = LayoutFromUsage(this->usage);
		}

		void GetData(int mipLevel, int arrayLayer, void* data, int bufSize)
		{
			CORELIB_UNUSED(bufSize);
			// Set up staging buffer and copy data to new image
			int bufferSize = 0;
			if (format == StorageFormat::BC1 || format == StorageFormat::BC1_SRGB || format == StorageFormat::BC5 || format == StorageFormat::BC3)
			{
				int blocks = (int)(ceil(width / 4.0f) * ceil(height / 4.0f));
				bufferSize = (format == StorageFormat::BC1 || format == StorageFormat::BC1_SRGB) ? blocks * 8 : blocks * 16;
			}
			else
			{
				bufferSize = width * height * StorageFormatSize(format);
			}
			CORELIB_ASSERT(bufferSize <= bufSize && "buffer size is too small");
			vk::BufferCreateInfo stagingBufferCreateInfo = vk::BufferCreateInfo()
				.setFlags(vk::BufferCreateFlags())
				.setSize(bufferSize)
				.setUsage(vk::BufferUsageFlagBits::eTransferDst)
				.setSharingMode(vk::SharingMode::eExclusive)
				.setQueueFamilyIndexCount(0)
				.setPQueueFamilyIndices(nullptr);

			vk::Buffer stagingBuffer = RendererState::Device().createBuffer(stagingBufferCreateInfo);

			vk::MemoryRequirements stagingBufferMemoryRequirements = RendererState::Device().getBufferMemoryRequirements(stagingBuffer);

			vk::MemoryAllocateInfo bufferAllocateInfo = vk::MemoryAllocateInfo()
				.setAllocationSize(stagingBufferMemoryRequirements.size)
				.setMemoryTypeIndex(GetMemoryType(stagingBufferMemoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible));

			vk::DeviceMemory stagingMemory = RendererState::Device().allocateMemory(bufferAllocateInfo);

			RendererState::Device().bindBufferMemory(stagingBuffer, stagingMemory, 0);

			// Get image aspect flags
			vk::ImageAspectFlags aspectFlags = vk::ImageAspectFlagBits::eColor;
			if (!!(usage & TextureUsage::ColorAttachment))
			{
				aspectFlags = vk::ImageAspectFlagBits::eColor;
			}
			else if (!!(usage & TextureUsage::DepthAttachment))
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;
				if (format == StorageFormat::Depth24Stencil8)
					aspectFlags |= vk::ImageAspectFlagBits::eStencil;
			}

			// Create buffer image copy information
			vk::BufferImageCopy stagingCopy = vk::BufferImageCopy()
				.setBufferOffset(0)
				.setBufferRowLength(0)
				.setBufferImageHeight(0)
				.setImageSubresource(vk::ImageSubresourceLayers().setAspectMask(aspectFlags).setMipLevel(mipLevel).setBaseArrayLayer(arrayLayer).setLayerCount(1))
				.setImageOffset(vk::Offset3D())
				.setImageExtent(vk::Extent3D(Math::Max(1, width >> mipLevel), Math::Max(1, height >> mipLevel), 1));

			// Create command buffer
			//TODO: Use CommandBuffer class?
			vk::CommandBuffer transferCommandBuffer = RendererState::CreateCommandBuffer(RendererState::TransferCommandPool());

			// Record command buffer
			vk::ImageSubresourceRange textureSubresourceRange = vk::ImageSubresourceRange()
				.setAspectMask(aspectFlags)
				.setBaseMipLevel(0)
				.setLevelCount(mipLevels)
				.setBaseArrayLayer(0)
				.setLayerCount(arrayLayers);

			int subresourceIndex = arrayLayer * mipLevels + mipLevel;

			vk::ImageMemoryBarrier textureCopyBarrier = vk::ImageMemoryBarrier()
				.setSrcAccessMask(LayoutFlags(currentSubresourceLayouts[subresourceIndex]))
				.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
				.setOldLayout(currentSubresourceLayouts[subresourceIndex])
				.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(this->image)
				.setSubresourceRange(textureSubresourceRange);

			vk::ImageMemoryBarrier textureUsageBarrier = vk::ImageMemoryBarrier()
				.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
				.setDstAccessMask(LayoutFlags(currentSubresourceLayouts[subresourceIndex]))
				.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
				.setNewLayout(currentSubresourceLayouts[subresourceIndex])
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(this->image)
				.setSubresourceRange(textureSubresourceRange);

			vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
				.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
				.setPInheritanceInfo(nullptr);

			transferCommandBuffer.begin(transferBeginInfo);
			transferCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllCommands,
				vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags(),
				nullptr,
				nullptr,
				textureCopyBarrier
			);
			transferCommandBuffer.copyImageToBuffer(
				this->image,
				vk::ImageLayout::eTransferSrcOptimal,
				stagingBuffer,
				stagingCopy
			);
			transferCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eAllCommands,
				vk::DependencyFlags(),
				nullptr,
				nullptr,
				textureUsageBarrier
			);
			transferCommandBuffer.end();

			// Submit to queue
			vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
				.setWaitSemaphoreCount(0)
				.setPWaitSemaphores(nullptr)
				.setPWaitDstStageMask(nullptr)
				.setCommandBufferCount(1)
				.setPCommandBuffers(&transferCommandBuffer)
				.setSignalSemaphoreCount(0)
				.setPSignalSemaphores(nullptr);

			RendererState::TransferQueue().submit(transferSubmitInfo, vk::Fence());
			RendererState::TransferQueue().waitIdle();
			RendererState::DestroyCommandBuffer(RendererState::TransferCommandPool(), transferCommandBuffer);

			// Map memory and copy
			assert(bufSize >= bufferSize);
			float* stagingMappedMemory = (float*)RendererState::Device().mapMemory(stagingMemory, 0, bufferSize, vk::MemoryMapFlags());
			memcpy(data, stagingMappedMemory, bufferSize);
			RendererState::Device().unmapMemory(stagingMemory);

			// Destroy staging resources
			RendererState::Device().freeMemory(stagingMemory);
			RendererState::Device().destroyBuffer(stagingBuffer);
		}

		void BuildMipmaps()
		{
			vk::ImageAspectFlags aspectFlags;
			if (isDepthFormat(format))
			{
				aspectFlags = vk::ImageAspectFlagBits::eDepth;
				if (format == StorageFormat::Depth24Stencil8)
					aspectFlags |= vk::ImageAspectFlagBits::eStencil;
			}
			else
				aspectFlags = vk::ImageAspectFlagBits::eColor;

			// Create command buffer
			vk::CommandBuffer transferCommandBuffer = RendererState::GetTempTransferCommandBuffer();

			vk::Filter blitFilter = vk::Filter::eLinear;
			switch (format)
			{
			case StorageFormat::R_I8:
			case StorageFormat::R_I16:
			case StorageFormat::Int32_Raw:
			case StorageFormat::RG_I8:
			case StorageFormat::RG_I16:
			case StorageFormat::RG_I32_Raw:
			case StorageFormat::RGBA_I8:
			case StorageFormat::RGBA_I16:
			case StorageFormat::RGBA_I32_Raw:
				blitFilter = vk::Filter::eNearest;
			default:
				break;
			}

			vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
				.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
				.setPInheritanceInfo(nullptr);

			transferCommandBuffer.begin(transferBeginInfo);

			vk::ImageSubresourceRange level0SubresourceRange = vk::ImageSubresourceRange()
				.setAspectMask(aspectFlags)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(arrayLayers);
			vk::ImageMemoryBarrier textureSrcBarrier = vk::ImageMemoryBarrier()
				.setSrcAccessMask(LayoutFlags(currentSubresourceLayouts[0]))
				.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
				.setOldLayout(currentSubresourceLayouts[0])
				.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(this->image)
				.setSubresourceRange(level0SubresourceRange);

			transferCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllCommands,
				vk::PipelineStageFlagBits::eAllCommands,
				vk::DependencyFlags(),
				nullptr,
				nullptr,
				textureSrcBarrier
			);

			for (int i = 1; i < mipLevels; i++)
			{
				// Record command buffer
				vk::ImageSubresourceRange textureSrcSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(i - 1)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(arrayLayers);
				vk::ImageSubresourceRange textureDstSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(i)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(arrayLayers);

				vk::ImageMemoryBarrier textureDstBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eUndefined))
					.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
					.setOldLayout(vk::ImageLayout::eUndefined)
					.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(this->image)
					.setSubresourceRange(textureDstSubresourceRange);

				transferCommandBuffer.pipelineBarrier(
					vk::PipelineStageFlagBits::eAllCommands,
					vk::PipelineStageFlagBits::eAllCommands,
					vk::DependencyFlags(),
					nullptr,
					nullptr,
					textureDstBarrier
				);

				vk::ImageBlit blitRegion;
				blitRegion.setSrcSubresource(vk::ImageSubresourceLayers().setAspectMask(aspectFlags).setMipLevel(i - 1).setBaseArrayLayer(0).setLayerCount(arrayLayers))
					.setSrcOffsets(std::array<vk::Offset3D, 2>{vk::Offset3D(0, 0, 0), vk::Offset3D(Math::Max(1, width >> (i - 1)), Math::Max(1, height >> (i - 1)), Math::Max(1, depth >> (i - 1)))})
					.setDstSubresource(vk::ImageSubresourceLayers().setAspectMask(aspectFlags).setMipLevel(i).setBaseArrayLayer(0).setLayerCount(arrayLayers))
					.setDstOffsets(std::array<vk::Offset3D, 2>{vk::Offset3D(0, 0, 0), vk::Offset3D(Math::Max(1, width >> i), Math::Max(1, height >> i), Math::Max(1, depth >> i))});
				// Blit texture to each mip level
				transferCommandBuffer.blitImage(
					image, vk::ImageLayout::eTransferSrcOptimal,
					image, vk::ImageLayout::eTransferDstOptimal,
					blitRegion,
					blitFilter
				);

				vk::ImageMemoryBarrier textureDstFinishBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
					.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
					.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
					.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(this->image)
					.setSubresourceRange(textureDstSubresourceRange);

				transferCommandBuffer.pipelineBarrier(
					vk::PipelineStageFlagBits::eAllCommands,
					vk::PipelineStageFlagBits::eAllCommands,
					vk::DependencyFlags(),
					nullptr,
					nullptr,
					textureDstFinishBarrier
				);
			}

			vk::ImageSubresourceRange allSubresourceRange = vk::ImageSubresourceRange()
				.setAspectMask(aspectFlags)
				.setBaseMipLevel(0)
				.setLevelCount(mipLevels)
				.setBaseArrayLayer(0)
				.setLayerCount(arrayLayers);
			vk::ImageMemoryBarrier textureDstFinishBarrier = vk::ImageMemoryBarrier()
				.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
				.setDstAccessMask(LayoutFlags(currentSubresourceLayouts[0]))
				.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
				.setNewLayout(LayoutFromUsage(this->usage))
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(this->image)
				.setSubresourceRange(allSubresourceRange);
			transferCommandBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllCommands,
				vk::PipelineStageFlagBits::eAllCommands,
				vk::DependencyFlags(),
				nullptr,
				nullptr,
				textureDstFinishBarrier
			);
			transferCommandBuffer.end();

			// Submit to queue
			vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
				.setWaitSemaphoreCount(0)
				.setPWaitSemaphores(nullptr)
				.setPWaitDstStageMask(nullptr)
				.setCommandBufferCount(1)
				.setPCommandBuffers(&transferCommandBuffer)
				.setSignalSemaphoreCount(0)
				.setPSignalSemaphores(nullptr);

			RendererState::TransferQueue().submit(transferSubmitInfo, vk::Fence());
			for (auto& currentLayout : currentSubresourceLayouts)
				currentLayout = LayoutFromUsage(this->usage);
		}
		void* Ptr()
		{
			return this;
		}
	};

	class Texture2D : public VK::Texture, public GameEngine::Texture2D
	{
		//TODO: Need some way of determining layouts and performing transitions properly. 
	public:
		Texture2D(String name, TextureUsage usage, int width, int height, int mipLevelCount, StorageFormat format)
			: VK::Texture(name, usage, width, height, 1, mipLevelCount, 1, 1, format, false) {};

		void GetSize(int& pwidth, int& pheight) override
		{
			pwidth = width;
			pheight = height;
		}
        virtual bool IsDepthStencilFormat() override
        {
            return this->format == StorageFormat::Depth24 || this->format == StorageFormat::Depth24Stencil8 || this->format == StorageFormat::Depth32;
        }

		void SetData(int level, int pwidth, int pheight, int /*numSamples*/, DataType inputType, void* data) override
		{
			VK::Texture::SetData(level, 0, 0, 0, 0, pwidth, pheight, 1, 1, inputType, data);
		}
		void SetData(int pwidth, int pheight, int /*numSamples*/, DataType inputType, void* data) override
		{
			VK::Texture::SetData(0, 0, 0, 0, 0, pwidth, pheight, 1, 1, inputType, data);
		}
		void BuildMipmaps() override
		{
			VK::Texture::BuildMipmaps();
		}
		void GetData(int mipLevel, void * data, int bufSize) override
		{
			VK::Texture::GetData(mipLevel, 0, data, bufSize);
		}
		void* GetInternalPtr() override
		{
			return VK::Texture::Ptr();
		}
	};

	class Texture2DArray : public VK::Texture, public GameEngine::Texture2DArray
	{
	public:
		Texture2DArray(String name, TextureUsage usage, int width, int height, int mipLevels, int arrayLayers, StorageFormat newFormat)
			: VK::Texture(name, usage, width, height, 1, mipLevels, arrayLayers, 1, newFormat, true) {};

		virtual void GetSize(int& pwidth, int& pheight, int& players) override
		{
			pwidth = this->width;
			pheight = this->height;
			players = this->arrayLayers;
		}
        virtual bool IsDepthStencilFormat() override
        {
            return this->format == StorageFormat::Depth24 || this->format == StorageFormat::Depth24Stencil8 || this->format == StorageFormat::Depth32;
        }
		virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int pwidth, int pheight, int layerCount, DataType inputType, void * data) override
		{
			VK::Texture::SetData(mipLevel, layerOffset, xOffset, yOffset, 0, pwidth, pheight, 1, layerCount, inputType, data);
		}

		virtual void BuildMipmaps() override
		{
			VK::Texture::BuildMipmaps();
		}
		
		void* GetInternalPtr() override
		{
			return VK::Texture::Ptr();
		}
	};

	class TextureCube : public VK::Texture, public GameEngine::TextureCube
	{
	public:
		TextureCube(String name, TextureUsage usage, int psize, int mipLevels, StorageFormat format)
			: VK::Texture(name, usage, psize, psize, 1, mipLevels, 6, 1, format, false, vk::ImageCreateFlagBits::eCubeCompatible), size(psize)
		{
			List<float> zeroMem;
			zeroMem.SetSize(psize * psize * 6 * 4);
			for (int i = 0; i < zeroMem.Count(); i++)
				zeroMem[i] = 0.0f;
			for (int i = 0; i < mipLevels; i++)
				VK::Texture::SetData(i, 0, 0, 0, 0, psize >> i, psize >> i, 1, 6, DataType::Float4, zeroMem.Buffer());
		};
		int size = 0;
		virtual void GetSize(int & psize) override
		{
			psize = size;
		}
		virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int pwidth, int pheight, int layerCount, DataType inputType, void* data) override
		{
			VK::Texture::SetData(mipLevel, layerOffset, xOffset, yOffset, 0, pwidth, pheight, 1, layerCount, inputType, data);
		}
        virtual bool IsDepthStencilFormat() override
        {
            return this->format == StorageFormat::Depth24 || this->format == StorageFormat::Depth24Stencil8 || this->format == StorageFormat::Depth32;
        }

		void* GetInternalPtr() override
		{
			return VK::Texture::Ptr();
		}
	};

	class TextureCubeArray : public VK::Texture, public GameEngine::TextureCubeArray
	{
	public:
		int count;
		TextureCubeArray(String name, TextureUsage usage, int cubeCount, int psize, int mipLevels, StorageFormat format)
			: VK::Texture(name, usage, psize, psize, 1, mipLevels, cubeCount * 6, 1, format, true, vk::ImageCreateFlagBits::eCubeCompatible), size(psize)
		{
			this->isCubeArray = true;
			List<float> zeroMem;
			zeroMem.SetSize(psize * psize * 6 * 4 * cubeCount);
			for (int i = 0; i < zeroMem.Count(); i++)
				zeroMem[i] = 0.0f;
			for (int i = 0; i < mipLevels; i++)
				VK::Texture::SetData(i, 0, 0, 0, 0, psize >> i, psize >> i, 1, cubeCount * 6, DataType::Float4, zeroMem.Buffer());
			count = cubeCount;
		};
		int size = 0;
		virtual void GetSize(int & psize, int &pCount) override
		{
			psize = size;
			pCount = count;
		}
		virtual void SetData(int mipLevel, int xOffset, int yOffset, int layerOffset, int pwidth, int pheight, int layerCount, DataType inputType, void* data) override
		{
			VK::Texture::SetData(mipLevel, layerOffset, xOffset, yOffset, 0, pwidth, pheight, 1, layerCount, inputType, data);
		}
        virtual bool IsDepthStencilFormat() override
        {
            return this->format == StorageFormat::Depth24 || this->format == StorageFormat::Depth24Stencil8 || this->format == StorageFormat::Depth32;
        }

		void* GetInternalPtr() override
		{
			return VK::Texture::Ptr();
		}
	};

	class Texture3D : public VK::Texture, public GameEngine::Texture3D
	{
	public:
		Texture3D(String name, TextureUsage usage, int width, int height, int depth, int mipLevels, StorageFormat newFormat)
			: VK::Texture(name, usage, width, height, depth, mipLevels, 1, 1, newFormat, false) {};

		virtual void GetSize(int& pwidth, int& pheight, int& pdepth) override
		{
			pwidth = this->width;
			pheight = this->height;
			pdepth = this->depth;
		}
		virtual void SetData(int mipLevel, int xOffset, int yOffset, int zOffset, int pwidth, int pheight, int pdepth, DataType inputType, void * data) override
		{
			VK::Texture::SetData(mipLevel, 0, xOffset, yOffset, zOffset, pwidth, pheight, pdepth, 1, inputType, data);
		}
        virtual bool IsDepthStencilFormat() override
        {
            return this->format == StorageFormat::Depth24 || this->format == StorageFormat::Depth24Stencil8 || this->format == StorageFormat::Depth32;
        }

		void* GetInternalPtr() override
		{
			return VK::Texture::Ptr();
		}
	};

	class TextureSampler : public GameEngine::TextureSampler
	{
	public:
		vk::Sampler sampler;
		TextureFilter filter = TextureFilter::Nearest;
		WrapMode wrap = WrapMode::Repeat;
		CompareFunc op = CompareFunc::Disabled;

		void CreateSampler()
		{
			DestroySampler();

			// Default sampler create info
			vk::SamplerCreateInfo samplerCreateInfo = vk::SamplerCreateInfo()
				.setFlags(vk::SamplerCreateFlags())
				.setMinFilter(vk::Filter::eNearest)
				.setMagFilter(vk::Filter::eNearest)
				.setMipmapMode(vk::SamplerMipmapMode::eNearest)
				.setAddressModeU(vk::SamplerAddressMode::eRepeat)
				.setAddressModeV(vk::SamplerAddressMode::eRepeat)
				.setAddressModeW(vk::SamplerAddressMode::eRepeat)
				.setMipLodBias(0.0f)
				.setAnisotropyEnable(VK_FALSE)
				.setMaxAnisotropy(1.0f)
				.setCompareEnable(op != CompareFunc::Disabled)
				.setCompareOp(TranslateCompareFunc(op))
				.setMinLod(0.0f)
				.setMaxLod(0.0f)
				.setBorderColor(vk::BorderColor::eFloatTransparentBlack)
				.setUnnormalizedCoordinates(VK_FALSE);

			// Modify create info based on parameters
			switch (filter)
			{
			case TextureFilter::Nearest:
				samplerCreateInfo.setMinFilter(vk::Filter::eNearest);
				samplerCreateInfo.setMagFilter(vk::Filter::eNearest);
				break;
			case TextureFilter::Linear:
				samplerCreateInfo.setMinFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMagFilter(vk::Filter::eLinear);
				break;
			case TextureFilter::Trilinear:
				samplerCreateInfo.setMinFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMagFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
				samplerCreateInfo.setMaxLod(15.0f);
				break;
			case TextureFilter::Anisotropic4x:
				samplerCreateInfo.setMinFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMagFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
				samplerCreateInfo.setMaxLod(15.0f);
				samplerCreateInfo.setAnisotropyEnable(VK_TRUE);
				samplerCreateInfo.setMaxAnisotropy(4.0f);
				break;
			case TextureFilter::Anisotropic8x:
				samplerCreateInfo.setMinFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMagFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
				samplerCreateInfo.setMaxLod(15.0f);
				samplerCreateInfo.setAnisotropyEnable(VK_TRUE);
				samplerCreateInfo.setMaxAnisotropy(8.0f);
				break;
			case TextureFilter::Anisotropic16x:
				samplerCreateInfo.setMinFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMagFilter(vk::Filter::eLinear);
				samplerCreateInfo.setMipmapMode(vk::SamplerMipmapMode::eLinear);
				samplerCreateInfo.setMaxLod(15.0f);
				samplerCreateInfo.setAnisotropyEnable(VK_TRUE);
				samplerCreateInfo.setMaxAnisotropy(16.0f);
				break;
			}

			switch (wrap)
			{
			case WrapMode::Clamp:
				samplerCreateInfo.setAddressModeU(vk::SamplerAddressMode::eClampToEdge);
				samplerCreateInfo.setAddressModeV(vk::SamplerAddressMode::eClampToEdge);
				samplerCreateInfo.setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
				break;
			case WrapMode::Repeat:
				samplerCreateInfo.setAddressModeU(vk::SamplerAddressMode::eRepeat);
				samplerCreateInfo.setAddressModeV(vk::SamplerAddressMode::eRepeat);
				samplerCreateInfo.setAddressModeW(vk::SamplerAddressMode::eRepeat);
				break;
			case WrapMode::Mirror:
				samplerCreateInfo.setAddressModeU(vk::SamplerAddressMode::eMirroredRepeat);
				samplerCreateInfo.setAddressModeV(vk::SamplerAddressMode::eMirroredRepeat);
				samplerCreateInfo.setAddressModeW(vk::SamplerAddressMode::eMirroredRepeat);
				break;
			}

			sampler = RendererState::Device().createSampler(samplerCreateInfo);
		}
		void DestroySampler()
		{
			if (sampler) RendererState::Device().destroySampler(sampler);
		}
	public:
		TextureSampler()
		{
			CreateSampler();
		}
		~TextureSampler()
		{
			DestroySampler();
		}

		TextureFilter GetFilter()
		{
			return filter;
		}
		void SetFilter(TextureFilter pfilter)
		{
			filter = pfilter;

			// If anisotropy is not supported and we want anisotropic filtering, make trilinear filter
			if (!RendererState::PhysicalDevice().getFeatures().samplerAnisotropy)
			{
				if (filter == TextureFilter::Anisotropic4x ||
					filter == TextureFilter::Anisotropic8x ||
					filter == TextureFilter::Anisotropic16x)
				{
					CoreLib::Diagnostics::Debug::WriteLine("Anisotropic filtering is not supported. Changing to trilinear.");
					filter = TextureFilter::Trilinear;
				}
			}
			else
			{
				TextureFilter oldFilter = filter;
				float maxAnisotropy = RendererState::PhysicalDevice().getProperties().limits.maxSamplerAnisotropy;

				switch (filter)
				{
				case TextureFilter::Anisotropic16x:
					if (maxAnisotropy < 16.0f) filter = TextureFilter::Anisotropic8x;
					// Fall through
				case TextureFilter::Anisotropic8x:
					if (maxAnisotropy < 8.0f) filter = TextureFilter::Anisotropic4x;
					// Fall through
				case TextureFilter::Anisotropic4x:
					if (maxAnisotropy < 4.0f) filter = TextureFilter::Trilinear;
					// Fall through
				default:
					break;
				}

				if (oldFilter != filter)
				{
					CoreLib::Diagnostics::Debug::Write("Max supported anisotropy is ");
					CoreLib::Diagnostics::Debug::WriteLine(maxAnisotropy);
				}
			}

			CreateSampler();
		}
		WrapMode GetWrapMode()
		{
			return wrap;
		}
		void SetWrapMode(WrapMode pwrap)
		{
			this->wrap = pwrap;
			CreateSampler();
		}
		CompareFunc GetCompareFunc()
		{
			return op;
		}
		void SetDepthCompare(CompareFunc pop)
		{
			this->op = pop;
			CreateSampler();
		}
	};

	class BufferObject : public GameEngine::Buffer
	{
		friend class HardwareRenderer;
	public:
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::BufferUsageFlags usage;
		vk::MemoryPropertyFlags location;
		int mapOffset = 0;
		int mapSize = -1;
		int size = 0;

	public:
		BufferObject(vk::BufferUsageFlags usage, int psize, vk::MemoryPropertyFlags location)
		{
			//TODO: Should pass as input a requiredLocation and an optimalLocation?
			this->location = location;
			this->usage = usage;
			this->size = psize;

			// If we can't map the buffer, we need to specify it will be a transfer dest
			if (!(this->location & vk::MemoryPropertyFlagBits::eHostVisible))
				this->usage |= vk::BufferUsageFlagBits::eTransferDst;

			// Create a buffer with the requested size
			vk::BufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo()
				.setFlags(vk::BufferCreateFlags())
				.setSize(this->size)
				.setUsage(this->usage)
				.setSharingMode(vk::SharingMode::eExclusive)
				.setQueueFamilyIndexCount(0)
				.setPQueueFamilyIndices(nullptr);

			this->buffer = RendererState::Device().createBuffer(bufferCreateInfo);

			// Check to see how much memory we actually require
			vk::MemoryRequirements memoryRequirements = RendererState::Device().getBufferMemoryRequirements(this->buffer);
			int backingSize = (int)memoryRequirements.size;

			// If we allocate more memory than requested, recreate buffer to backing size
			if (this->size != backingSize)
			{
				this->size = backingSize;

				vk::BufferCreateInfo fullsizeBufferCreateInfo = vk::BufferCreateInfo()
					.setFlags(vk::BufferCreateFlags())
					.setSize(this->size)
					.setUsage(this->usage)
					.setSharingMode(vk::SharingMode::eExclusive)
					.setQueueFamilyIndexCount(0)
					.setPQueueFamilyIndices(nullptr);

				RendererState::Device().destroyBuffer(this->buffer);
				this->buffer = RendererState::Device().createBuffer(fullsizeBufferCreateInfo);
			}

			// Allocate memory for the buffer
			vk::MemoryRequirements fullsizeMemoryRequirements = RendererState::Device().getBufferMemoryRequirements(this->buffer);

			vk::MemoryAllocateInfo fullsizeMemoryAllocateInfo = vk::MemoryAllocateInfo()
				.setAllocationSize(fullsizeMemoryRequirements.size)
				.setMemoryTypeIndex(GetMemoryType(fullsizeMemoryRequirements.memoryTypeBits, this->location));

			this->memory = RendererState::Device().allocateMemory(fullsizeMemoryAllocateInfo);
			RendererState::Device().bindBufferMemory(this->buffer, this->memory, 0);
		}
		~BufferObject()
		{
			if (this->buffer) RendererState::Device().destroyBuffer(this->buffer);
			if (this->memory) RendererState::Device().freeMemory(this->memory);
		}
		void SetDataAsync(int offset, void* data, int psize)
		{
			if (psize == 0)
				return;
			// If the buffer is mappable, map and memcpy
			if (location & vk::MemoryPropertyFlagBits::eHostVisible)
			{
				//TODO: Should memcpy + flush in chunks?
				void* mappedMemory = Map(offset, psize);
				memcpy(mappedMemory, data, psize);
				Flush();
				Unmap();
			}
			// Otherwise, we need to use command buffers
			else
			{
				// Create command buffer
			
				vk::CommandBuffer transferCommandBuffer = RendererState::GetTempTransferCommandBuffer();

				assert(psize % 4 == 0);
				assert(offset % 4 == 0);

				// Record command buffer
				vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
					.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
					.setPInheritanceInfo(nullptr);

				// Transfer the data in chunks of <= 65536 bytes
				transferCommandBuffer.begin(transferBeginInfo);
				int remainingSize = psize;
				int dataOffset = 0;
				while (remainingSize > 65536)
				{
					transferCommandBuffer.updateBuffer(this->buffer, offset + dataOffset, 65536, static_cast<char*>(data) + dataOffset);
					remainingSize -= 65536;
					dataOffset += 65536;
				}
				transferCommandBuffer.updateBuffer(this->buffer, offset + dataOffset, remainingSize, static_cast<char*>(data) + dataOffset);
				transferCommandBuffer.end();

				// Submit to queue
				vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
					.setWaitSemaphoreCount(0)
					.setPWaitSemaphores(nullptr)
					.setPWaitDstStageMask(nullptr)
					.setCommandBufferCount(1)
					.setPCommandBuffers(&transferCommandBuffer)
					.setSignalSemaphoreCount(0)
					.setPSignalSemaphores(nullptr);
				RendererState::TransferQueue().submit(transferSubmitInfo, vk::Fence());
			}
		}
		void SetData(int offset, void* data, int psize)
		{
			CORELIB_ASSERT(data && "Initialize buffer with size preferred.");

			// If the buffer is mappable, map and memcpy
			if (location & vk::MemoryPropertyFlagBits::eHostVisible)
			{
				//TODO: Should memcpy + flush in chunks?
				void* mappedMemory = Map(offset, psize);
				memcpy(mappedMemory, data, psize);
				Flush();
				Unmap();
			}
			// Otherwise, we need to use command buffers
			else
			{
				// Create command buffer
				//TODO: Should this use a global buffer? How to handle thread safety?
				vk::CommandBuffer transferCommandBuffer = RendererState::GetTempTransferCommandBuffer();

				//TODO: Improve switching logic
				bool staging = psize > (1 << 18);
				if (staging)
				{
					// Create staging buffer
					vk::BufferCreateInfo stagingCreateInfo = vk::BufferCreateInfo()
						.setFlags(vk::BufferCreateFlags())
						.setSize(psize)
						.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
						.setSharingMode(vk::SharingMode::eExclusive)
						.setQueueFamilyIndexCount(0)
						.setPQueueFamilyIndices(nullptr);

					vk::Buffer stagingBuffer = RendererState::Device().createBuffer(stagingCreateInfo);

					vk::MemoryRequirements stagingMemoryRequirements = RendererState::Device().getBufferMemoryRequirements(stagingBuffer);

					vk::MemoryAllocateInfo stagingMemoryAllocateInfo = vk::MemoryAllocateInfo()
						.setAllocationSize(stagingMemoryRequirements.size)
						.setMemoryTypeIndex(GetMemoryType(stagingMemoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible));

					vk::DeviceMemory stagingMemory = RendererState::Device().allocateMemory(stagingMemoryAllocateInfo);
					RendererState::Device().bindBufferMemory(stagingBuffer, stagingMemory, 0);

					// Copy data to staging buffer
					void* stagingMappedMemory = RendererState::Device().mapMemory(stagingMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags());
					memcpy(stagingMappedMemory, data, psize);
					RendererState::Device().unmapMemory(stagingMemory);

					// Create copy region description
					vk::BufferCopy transferRegion = vk::BufferCopy()
						.setSrcOffset(0)
						.setDstOffset(offset)
						.setSize(psize);

					// Record command buffer
					vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
						.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
						.setPInheritanceInfo(nullptr);

					transferCommandBuffer.begin(transferBeginInfo);
					transferCommandBuffer.copyBuffer(stagingBuffer, this->buffer, transferRegion);
					transferCommandBuffer.end();

					// Submit to queue
					vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
						.setWaitSemaphoreCount(0)
						.setPWaitSemaphores(nullptr)
						.setPWaitDstStageMask(nullptr)
						.setCommandBufferCount(1)
						.setPCommandBuffers(&transferCommandBuffer)
						.setSignalSemaphoreCount(0)
						.setPSignalSemaphores(nullptr);

					RendererState::TransferQueue().submit(transferSubmitInfo, vk::Fence());
					RendererState::TransferQueue().waitIdle(); //TODO: Remove

					// Destroy staging resources
					RendererState::Device().freeMemory(stagingMemory);
					RendererState::Device().destroyBuffer(stagingBuffer);
				}
				else
				{
					assert(psize % 4 == 0);
					assert(offset % 4 == 0);

					// Record command buffer
					vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
						.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
						.setPInheritanceInfo(nullptr);

					// Transfer the data in chunks of <= 65536 bytes
					transferCommandBuffer.begin(transferBeginInfo);
					int remainingSize = psize;
					int dataOffset = 0;
					while (remainingSize > 65536)
					{
						transferCommandBuffer.updateBuffer(this->buffer, offset + dataOffset, 65536, static_cast<char*>(data) + dataOffset);
						remainingSize -= 65536;
						dataOffset += 65536;
					}
					transferCommandBuffer.updateBuffer(this->buffer, offset + dataOffset, remainingSize, static_cast<char*>(data) + dataOffset);
					transferCommandBuffer.end();

					// Submit to queue
					vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
						.setWaitSemaphoreCount(0)
						.setPWaitSemaphores(nullptr)
						.setPWaitDstStageMask(nullptr)
						.setCommandBufferCount(1)
						.setPCommandBuffers(&transferCommandBuffer)
						.setSignalSemaphoreCount(0)
						.setPSignalSemaphores(nullptr);
					RendererState::TransferQueue().submit(transferSubmitInfo, vk::Fence());
					RendererState::TransferQueue().waitIdle(); //TODO: Remove
				}
			}
		}
		void SetData(void* data, int psize)
		{
			SetData(0, data, psize);
		}
		void GetData(void * pBuffer, int offset, int psize)
		{
			//TODO: Implement
			// If the buffer is mappable, map and memcpy
			if (location & vk::MemoryPropertyFlagBits::eHostVisible)
			{
				//TODO: Should memcpy + flush in chunks?
				void* mappedMemory = Map(offset, psize);
				memcpy(pBuffer, mappedMemory, psize);
				Unmap();
			}
			else
			{
				vk::CommandBuffer transferCommandBuffer = RendererState::CreateCommandBuffer(RendererState::TransferCommandPool());

				// Create staging buffer
				vk::BufferCreateInfo stagingCreateInfo = vk::BufferCreateInfo()
					.setFlags(vk::BufferCreateFlags())
					.setSize(psize)
					.setUsage(vk::BufferUsageFlagBits::eTransferDst)
					.setSharingMode(vk::SharingMode::eExclusive)
					.setQueueFamilyIndexCount(0)
					.setPQueueFamilyIndices(nullptr);

				vk::Buffer stagingBuffer = RendererState::Device().createBuffer(stagingCreateInfo);

				vk::MemoryRequirements stagingMemoryRequirements = RendererState::Device().getBufferMemoryRequirements(stagingBuffer);

				vk::MemoryAllocateInfo stagingMemoryAllocateInfo = vk::MemoryAllocateInfo()
					.setAllocationSize(stagingMemoryRequirements.size)
					.setMemoryTypeIndex(GetMemoryType(stagingMemoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible));

				vk::DeviceMemory stagingMemory = RendererState::Device().allocateMemory(stagingMemoryAllocateInfo);
				RendererState::Device().bindBufferMemory(stagingBuffer, stagingMemory, 0);

				// Create copy region description
				vk::BufferCopy transferRegion = vk::BufferCopy()
					.setSrcOffset(0)
					.setDstOffset(offset)
					.setSize(psize);

				// Record command buffer
				vk::CommandBufferBeginInfo transferBeginInfo = vk::CommandBufferBeginInfo()
					.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
					.setPInheritanceInfo(nullptr);

				transferCommandBuffer.begin(transferBeginInfo);
				transferCommandBuffer.copyBuffer(this->buffer, stagingBuffer, transferRegion);
				transferCommandBuffer.end();

				// Submit to queue
				vk::SubmitInfo transferSubmitInfo = vk::SubmitInfo()
					.setWaitSemaphoreCount(0)
					.setPWaitSemaphores(nullptr)
					.setPWaitDstStageMask(nullptr)
					.setCommandBufferCount(1)
					.setPCommandBuffers(&transferCommandBuffer)
					.setSignalSemaphoreCount(0)
					.setPSignalSemaphores(nullptr);

				RendererState::RenderQueue().submit(transferSubmitInfo, vk::Fence());
				RendererState::RenderQueue().waitIdle(); //TODO: Remove

				// map memory and copy
				void* stagingMappedMemory = RendererState::Device().mapMemory(stagingMemory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags());
				memcpy(pBuffer, stagingMappedMemory, psize);
				RendererState::Device().unmapMemory(stagingMemory);

				RendererState::DestroyCommandBuffer(RendererState::TransferCommandPool(), transferCommandBuffer);

				// Destroy staging resources
				RendererState::Device().freeMemory(stagingMemory);
				RendererState::Device().destroyBuffer(stagingBuffer);
			}
		}
		int GetSize()
		{
			return size;
		}
		void* Map(int offset, int psize)
		{
			//TEMP
			assert(buffer);
			assert(this->mapSize == -1);
			//ENDTEMP

			// Buffer must have host visible flag to be mappable
			if (!(location & vk::MemoryPropertyFlagBits::eHostVisible))
				return nullptr;

			//TODO: We need to guarantee that all previous commands writing to this range have completed
			//      Can probably do this by associating each buffer with a fence, and waiting on the fence

			//TODO: Lock mutex for memory
			this->mapOffset = offset;
			this->mapSize = psize;

			return RendererState::Device().mapMemory(memory, offset, psize, vk::MemoryMapFlags());
		}
		void* Map()
		{
			return Map(0, this->size);
		}
		// If the buffer is not host coherent, we need to flush to guarantee writes visible to device
		void Flush(int offset, int psize)
		{
			assert(this->mapSize >= 0);

			// We only need to flush if not host coherent
			if (!(location & vk::MemoryPropertyFlagBits::eHostCoherent))
			{
				//TEMP
				assert(offset >= this->mapOffset);
				assert(offset % (RendererState::PhysicalDevice().getProperties().limits.nonCoherentAtomSize) == 0);
				assert(psize % (RendererState::PhysicalDevice().getProperties().limits.nonCoherentAtomSize) == 0);
				assert(psize <= this->mapSize || (this->mapSize == -1 && psize <= (this->size - offset)));
				//TODO: Round down offset and round up size?
				//ENDTEMP

				//TODO: We need to make sure we do this before command buffers using the memory are submitted

				vk::MappedMemoryRange memoryRange = vk::MappedMemoryRange()
					.setMemory(this->memory)
					.setOffset(offset)
					.setSize(psize);

				RendererState::Device().flushMappedMemoryRanges(memoryRange);
			}
		}
		void Flush()
		{
			Flush(this->mapOffset, this->mapSize);
		}
		void Unmap()
		{
			assert(this->mapSize >= 0);
			RendererState::Device().unmapMemory(memory);
			this->mapSize = -1;
			this->mapOffset = 0;
			//TODO: unlock mutex
		}
	};

	class Shader : public GameEngine::Shader
	{
	public:
		ShaderType stage;
		vk::ShaderModule module;

		Shader() {}
		~Shader()
		{
			Destroy();
		}

		void Create(ShaderType pstage, const char* data, int size)
		{
			Destroy();
			this->stage = pstage;
			vk::ShaderModuleCreateInfo createInfo(vk::ShaderModuleCreateFlags(), (size_t)size, (const uint32_t*)data);
			this->module = RendererState::Device().createShaderModule(createInfo);
		}

		void Destroy()
		{
			if (module) RendererState::Device().destroyShaderModule(module);
			module = vk::ShaderModule();
		}
	};

	class RenderTargetLayout : public GameEngine::RenderTargetLayout
	{
	public:
		CoreLib::List<vk::AttachmentReference> colorReferences;
		vk::AttachmentReference depthReference;
		vk::RenderPass renderPass, renderPassClear;
	private:
        void Resize(CoreLib::List<vk::AttachmentDescription> &descriptions, int size)
		{
			if (descriptions.Count() < size)
				descriptions.SetSize(size);
		}

		void SetColorAttachment(CoreLib::List<vk::AttachmentDescription> & descriptions, int binding, StorageFormat format,
            bool ignoreInitialContent, LoadOp loadOp = LoadOp::Load, StoreOp storeOp = StoreOp::Store)
		{
            Resize(descriptions, binding + 1);
            if (ignoreInitialContent && loadOp == LoadOp::Load)
                loadOp = LoadOp::DontCare;
			descriptions[binding] = vk::AttachmentDescription()
				.setFlags(vk::AttachmentDescriptionFlags())
				.setFormat(TranslateStorageFormat(format))//
				.setInitialLayout(ignoreInitialContent ? vk::ImageLayout::eUndefined : vk::ImageLayout::eColorAttachmentOptimal)//
				.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)//
				.setSamples(SampleCount(1))
				.setLoadOp(TranslateLoadOp(loadOp))
				.setStoreOp(TranslateStoreOp(storeOp))
				.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)//
				.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);//

				//if (samples > 1)
				//{
				//	//TODO: need to resolve?
				//}
		}

		void SetDepthAttachment(CoreLib::List<vk::AttachmentDescription> &descriptions, int binding, StorageFormat format,
            bool ignoreInitialContent, LoadOp loadOp = LoadOp::Load, StoreOp storeOp = StoreOp::Store)
		{
			CORELIB_ASSERT(depthReference.layout == vk::ImageLayout::eUndefined && "Only 1 depth/stencil attachment allowed.");

			Resize(descriptions, binding + 1);
            if (ignoreInitialContent && loadOp == LoadOp::Load)
                loadOp = LoadOp::DontCare;

			descriptions[binding] = vk::AttachmentDescription()
				.setFlags(vk::AttachmentDescriptionFlags())
				.setFormat(TranslateStorageFormat(format))
				.setInitialLayout(ignoreInitialContent ? vk::ImageLayout::eUndefined : vk::ImageLayout::eDepthStencilAttachmentOptimal)
				.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
				.setSamples(SampleCount(1))
				.setLoadOp(TranslateLoadOp(loadOp))
				.setStoreOp(TranslateStoreOp(storeOp))
				.setStencilLoadOp(TranslateLoadOp(loadOp))
				.setStencilStoreOp(TranslateStoreOp(storeOp));
		}
	public:
		RenderTargetLayout() {};
		RenderTargetLayout(CoreLib::ArrayView<AttachmentLayout> bindings, bool ignoreInitialContent)
		{
			depthReference.attachment = VK_ATTACHMENT_UNUSED;
            List<vk::AttachmentDescription> descriptions, descriptionsClear;
			int location = 0;
			for (auto binding : bindings)
			{
				switch (binding.Usage)
				{
				case TextureUsage::ColorAttachment:
				case TextureUsage::SampledColorAttachment:
                    SetColorAttachment(descriptions, location, binding.ImageFormat, ignoreInitialContent);
                    SetColorAttachment(descriptionsClear, location, binding.ImageFormat, ignoreInitialContent, LoadOp::Clear);

					break;
				case TextureUsage::DepthAttachment:
				case TextureUsage::SampledDepthAttachment:
                    SetDepthAttachment(descriptions, location, binding.ImageFormat, ignoreInitialContent);
                    SetDepthAttachment(
                        descriptionsClear, location, binding.ImageFormat, ignoreInitialContent, LoadOp::Clear);
					break;
				case TextureUsage::Unused:
					break;
				default:
					throw HardwareRendererException("Unsupported attachment usage");
				}
				location++;
			}

			int binding = 0;
			for (auto description : descriptions)
			{
				if (description.finalLayout == vk::ImageLayout::eColorAttachmentOptimal)
					colorReferences.Add(vk::AttachmentReference(binding, vk::ImageLayout::eColorAttachmentOptimal));
				else if (description.finalLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
					depthReference = vk::AttachmentReference(binding, vk::ImageLayout::eDepthStencilAttachmentOptimal);

				binding++;
			}

			// Create Subpass Descriptions
			//TODO: Subpasses need to be implemented
			CoreLib::List<vk::SubpassDescription> subpassDescriptions;
			subpassDescriptions.Add(
				vk::SubpassDescription()
				.setFlags(vk::SubpassDescriptionFlags())
				.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
				.setInputAttachmentCount(0)
				.setPInputAttachments(nullptr)
				.setColorAttachmentCount(colorReferences.Count())
				.setPColorAttachments(colorReferences.Buffer())
				.setPResolveAttachments(nullptr)
				.setPDepthStencilAttachment(&depthReference)
				.setPreserveAttachmentCount(0)
				.setPPreserveAttachments(nullptr)
			);

			// Create RenderPass
			vk::RenderPassCreateInfo renderPassCreateInfo = vk::RenderPassCreateInfo()
				.setFlags(vk::RenderPassCreateFlags())
				.setAttachmentCount(descriptions.Count())
				.setPAttachments(descriptions.Buffer())
				.setSubpassCount(subpassDescriptions.Count())
				.setPSubpasses(subpassDescriptions.Buffer())
				.setDependencyCount(0)
				.setPDependencies(nullptr);

			this->renderPass = RendererState::Device().createRenderPass(renderPassCreateInfo);
            renderPassCreateInfo.setPAttachments(descriptionsClear.Buffer());
			this->renderPassClear = RendererState::Device().createRenderPass(renderPassCreateInfo);

		}
		~RenderTargetLayout()
		{
			if (renderPass) RendererState::Device().destroyRenderPass(renderPass);
            if (renderPassClear)
                RendererState::Device().destroyRenderPass(renderPassClear);
		}

		virtual GameEngine::FrameBuffer* CreateFrameBuffer(const RenderAttachments& renderAttachments) override;
	};


	class FrameBuffer : public GameEngine::FrameBuffer
	{
	public:
		int width;
		int height;
		vk::Framebuffer framebuffer, framebufferClear;
		CoreLib::RefPtr<RenderTargetLayout> renderTargetLayout;
		RenderAttachments renderAttachments;
		CoreLib::Array<vk::ClearValue, 10> clearValues;
		CoreLib::List<vk::ImageView> attachmentImageViews;
		FrameBuffer() {};
		~FrameBuffer()
		{
			if (framebuffer) RendererState::Device().destroyFramebuffer(framebuffer);
            if (framebufferClear)
                RendererState::Device().destroyFramebuffer(framebufferClear);

			for (auto view : attachmentImageViews)
				RendererState::Device().destroyImageView(view);
		}
		virtual RenderAttachments& GetRenderAttachments() override
		{
			return renderAttachments;
		}
	};

	GameEngine::FrameBuffer* RenderTargetLayout::CreateFrameBuffer(const RenderAttachments& renderAttachments)
	{
#if _DEBUG
		// Ensure the RenderAttachments are compatible with this RenderTargetLayout
		for (auto colorReference : colorReferences)
		{
			TextureUsage usage = TextureUsage::ColorAttachment;
			if (renderAttachments.attachments[colorReference.attachment].handle.tex2D)
				usage = dynamic_cast<Texture2D*>(renderAttachments.attachments[colorReference.attachment].handle.tex2D)->usage;
			else if (renderAttachments.attachments[colorReference.attachment].handle.tex2DArray)
				usage = dynamic_cast<Texture2DArray*>(renderAttachments.attachments[colorReference.attachment].handle.tex2DArray)->usage;

			if (!(usage & TextureUsage::ColorAttachment))
				throw HardwareRendererException("Incompatible RenderTargetLayout and RenderAttachments");
		}
		if (depthReference.layout != vk::ImageLayout::eUndefined)
		{
			TextureUsage usage = TextureUsage::ColorAttachment;
			if (renderAttachments.attachments[depthReference.attachment].handle.tex2D)
				usage = dynamic_cast<Texture2D*>(renderAttachments.attachments[depthReference.attachment].handle.tex2D)->usage;
			else if (renderAttachments.attachments[depthReference.attachment].handle.tex2DArray)
				usage = dynamic_cast<Texture2DArray*>(renderAttachments.attachments[depthReference.attachment].handle.tex2DArray)->usage;

			if (!(usage & TextureUsage::DepthAttachment))
				throw HardwareRendererException("Incompatible RenderTargetLayout and RenderAttachments");
		}
#endif
		FrameBuffer* result = new FrameBuffer();
		result->renderTargetLayout = this;
		result->renderAttachments = renderAttachments;
		result->attachmentImageViews.Clear();
		for (auto attachment : renderAttachments.attachments)
		{
			vk::ImageAspectFlags aspectFlags;
			if (attachment.handle.tex2D)
			{
				auto tex = dynamic_cast<Texture2D*>(attachment.handle.tex2D);
				
				if (isDepthFormat(tex->format))
				{
					aspectFlags = vk::ImageAspectFlagBits::eDepth;
					if (tex->format == StorageFormat::Depth24Stencil8)
						aspectFlags |= vk::ImageAspectFlagBits::eStencil;
				}
				else
					aspectFlags = vk::ImageAspectFlagBits::eColor;

				vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1);

				vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
					.setFlags(vk::ImageViewCreateFlags())
					.setImage(tex->image)
					.setViewType(vk::ImageViewType::e2D)
					.setFormat(TranslateStorageFormat(tex->format))
					.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA))//
					.setSubresourceRange(imageSubresourceRange);

				result->attachmentImageViews.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			}
			else if (attachment.handle.tex2DArray)
			{
				auto tex = dynamic_cast<Texture2DArray*>(attachment.handle.tex2DArray);

				if (isDepthFormat(tex->format))
				{
					aspectFlags = vk::ImageAspectFlagBits::eDepth;
					if (tex->format == StorageFormat::Depth24Stencil8)
						aspectFlags |= vk::ImageAspectFlagBits::eStencil;
				}
				else
					aspectFlags = vk::ImageAspectFlagBits::eColor; 
				
				vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(attachment.layer)
					.setLayerCount(1);

				vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
					.setFlags(vk::ImageViewCreateFlags())
					.setImage(tex->image)
					.setViewType(vk::ImageViewType::e2D)
					.setFormat(TranslateStorageFormat(tex->format))
					.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA))//
					.setSubresourceRange(imageSubresourceRange);

				result->attachmentImageViews.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			}
			else if (attachment.handle.texCube)
			{
				auto tex = dynamic_cast<TextureCube*>(attachment.handle.texCube);

				if (isDepthFormat(tex->format))
				{
					aspectFlags = vk::ImageAspectFlagBits::eDepth;
					if (tex->format == StorageFormat::Depth24Stencil8)
						aspectFlags |= vk::ImageAspectFlagBits::eStencil;
				}
				else
					aspectFlags = vk::ImageAspectFlagBits::eColor;

				vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(attachment.level)
					.setLevelCount(1)
					.setBaseArrayLayer((int)attachment.face)
					.setLayerCount(1);

				vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
					.setFlags(vk::ImageViewCreateFlags())
					.setImage(tex->image)
					.setViewType(vk::ImageViewType::e2D)
					.setFormat(TranslateStorageFormat(tex->format))
					.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA))//
					.setSubresourceRange(imageSubresourceRange);

				result->attachmentImageViews.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			}
			else if (attachment.handle.texCubeArray)
			{
				auto tex = dynamic_cast<TextureCubeArray*>(attachment.handle.texCubeArray);

				if (isDepthFormat(tex->format))
				{
					aspectFlags = vk::ImageAspectFlagBits::eDepth;
					if (tex->format == StorageFormat::Depth24Stencil8)
						aspectFlags |= vk::ImageAspectFlagBits::eStencil;
				}
				else
					aspectFlags = vk::ImageAspectFlagBits::eColor;

				vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(aspectFlags)
					.setBaseMipLevel(attachment.level)
					.setLevelCount(1)
					.setBaseArrayLayer((int)attachment.face + attachment.layer * 6)
					.setLayerCount(1);

				vk::ImageViewCreateInfo imageViewCreateInfo = vk::ImageViewCreateInfo()
					.setFlags(vk::ImageViewCreateFlags())
					.setImage(tex->image)
					.setViewType(vk::ImageViewType::e2D)
					.setFormat(TranslateStorageFormat(tex->format))
					.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA))//
					.setSubresourceRange(imageSubresourceRange);

				result->attachmentImageViews.Add(RendererState::Device().createImageView(imageViewCreateInfo));
			}
            if (aspectFlags == vk::ImageAspectFlagBits::eColor)
            {
                result->clearValues.Add(vk::ClearValue(vk::ClearColorValue()));
            }
            else
            {
                result->clearValues.Add(vk::ClearDepthStencilValue(1.0f, 0U));
            }
		}

		vk::FramebufferCreateInfo framebufferCreateInfo = vk::FramebufferCreateInfo()
			.setFlags(vk::FramebufferCreateFlags())
			.setRenderPass(renderPass)
			.setAttachmentCount(result->attachmentImageViews.Count())
			.setPAttachments(result->attachmentImageViews.Buffer())
			.setWidth(renderAttachments.width)
			.setHeight(renderAttachments.height)
			.setLayers(1);

		result->framebuffer = RendererState::Device().createFramebuffer(framebufferCreateInfo);
        framebufferCreateInfo.setRenderPass(renderPassClear);
        result->framebufferClear = RendererState::Device().createFramebuffer(framebufferCreateInfo);
		
		result->width = renderAttachments.width;
		result->height = renderAttachments.height;
		return result;
	}

	class DescriptorSetLayout : public GameEngine::DescriptorSetLayout
	{
	public:
		CoreLib::List<vk::DescriptorSetLayoutBinding> layoutBindings;
		vk::DescriptorSetLayout layout;
#if _DEBUG
		CoreLib::ArrayView<GameEngine::DescriptorLayout> descriptors;
#endif

		DescriptorSetLayout(CoreLib::ArrayView<GameEngine::DescriptorLayout> descriptors)
		{
#if _DEBUG
			this->descriptors = descriptors;

			CoreLib::Array<int, 32> usedDescriptors;
			usedDescriptors.SetSize(32);
			for (auto& desc : usedDescriptors)
				desc = false;
#endif

			for (auto& desc : descriptors)
			{
#if _DEBUG
				if (usedDescriptors[desc.Location] != false)
					throw HardwareRendererException("Descriptor location already in use.");
				usedDescriptors[desc.Location] = true;
#endif
				layoutBindings.Add(
					vk::DescriptorSetLayoutBinding()
					.setBinding(desc.Location)
					.setDescriptorType(TranslateBindingType(desc.Type))
					.setDescriptorCount(desc.ArraySize)
					.setStageFlags(TranslateStageFlags(desc.Stages))
					.setPImmutableSamplers(nullptr)
				);
			}

			vk::DescriptorSetLayoutCreateInfo createInfo = vk::DescriptorSetLayoutCreateInfo()
				.setFlags(vk::DescriptorSetLayoutCreateFlags())
				.setBindingCount(layoutBindings.Count())
				.setPBindings(layoutBindings.Buffer());

			layout = RendererState::Device().createDescriptorSetLayout(createInfo);
		}
		~DescriptorSetLayout()
		{
			RendererState::Device().destroyDescriptorSetLayout(layout);
		}
	};

	struct TextureUse
	{
		VK::Texture* texture;
		vk::ImageLayout targetLayout;
	};

	class DescriptorSet : public GameEngine::DescriptorSet
	{
	public:
		//TODO: previous implementation in PipelineInstance would keep track of
		// previously bound descriptors and then create new imageInfo/bufferInfo
		// and swap them with the old one, as well as only updating the descriptors
		// that had changed. Should this do that too?
		vk::DescriptorSet descriptorSet;
		vk::DescriptorPool descriptorPool;
		CoreLib::RefPtr<DescriptorSetLayout> descriptorSetLayout;
		CoreLib::List<vk::DescriptorImageInfo> imageInfo;
		CoreLib::List<vk::DescriptorBufferInfo> bufferInfo;
		CoreLib::List<vk::WriteDescriptorSet> writeDescriptorSets;
		CoreLib::List<CoreLib::List<TextureUse>> textureUses;
	public:
		DescriptorSet() {}
		DescriptorSet(DescriptorSetLayout* layout)
		{
			// We need to keep a ref pointer to the layout because we need to have
			// the layout available when we call vkUpdateDescriptorSets (2.3.1)
			descriptorSetLayout = layout;

			std::pair<vk::DescriptorPool, vk::DescriptorSet> res = RendererState::AllocateDescriptorSet(layout->layout);
			descriptorPool = res.first;
			descriptorSet = res.second;
		}
		~DescriptorSet()
		{
			if (descriptorSet) RendererState::Device().freeDescriptorSets(descriptorPool, descriptorSet);
		}

		virtual void BeginUpdate() override
		{
			imageInfo.Clear();
			bufferInfo.Clear();
			writeDescriptorSets.Clear();
		}

        virtual void Update(int location, ArrayView<GameEngine::Texture*> textures, TextureAspect aspect) override
        {
            int imageInfoStart = imageInfo.Count();
			if (textureUses.Count() <= location)
				textureUses.SetSize(location + 1);
			textureUses[location].Clear();
            for (auto texture : textures)
            {
                if (texture)
                {
                    VK::Texture* internalTexture = dynamic_cast<VK::Texture*>(texture);
					if (internalTexture->usage != TextureUsage::Sampled)
					{
						// Track layout of this texture
						textureUses[location].Add(TextureUse{ internalTexture, vk::ImageLayout::eShaderReadOnlyOptimal });
					}
                    vk::ImageView view = internalTexture->views[0];
                    if (isDepthFormat(internalTexture->format)/* && aspect == TextureAspect::Depth*/)
                        view = internalTexture->views[1];
                    if (internalTexture->format == StorageFormat::Depth24Stencil8 &&  aspect == TextureAspect::Stencil)
                        view = internalTexture->views[2];

                    imageInfo.Add(
                        vk::DescriptorImageInfo()
                        .setSampler(vk::Sampler())
                        .setImageView(view)
                        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    );
                }
                else
                    imageInfo.Add(
                        vk::DescriptorImageInfo()
                        .setSampler(vk::Sampler())
                        .setImageView(vk::ImageView())
                        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    );
            }

            writeDescriptorSets.Add(
                vk::WriteDescriptorSet()
                .setDstSet(this->descriptorSet)
                .setDstBinding(location)
                .setDstArrayElement(0)
                .setDescriptorCount(textures.Count())
                .setDescriptorType(vk::DescriptorType::eSampledImage)
                .setPImageInfo(imageInfo.Buffer() + imageInfoStart)
                .setPBufferInfo(nullptr)
                .setPTexelBufferView(nullptr)
            );
        }

        virtual void UpdateStorageImage(int location, ArrayView<GameEngine::Texture*> textures, TextureAspect aspect) override
        {
            int imageInfoStart = imageInfo.Count();
			if (textureUses.Count() <= location)
				textureUses.SetSize(location + 1);
			textureUses[location].Clear();
            for (auto texture : textures)
            {
                if (texture)
                {
                    VK::Texture* internalTexture = dynamic_cast<VK::Texture*>(texture);
					// Track layout of this texture
					textureUses[location].Add(TextureUse{ internalTexture, vk::ImageLayout::eGeneral });
                    vk::ImageView view = internalTexture->views[0];
                    if (isDepthFormat(internalTexture->format) /*&& aspect == TextureAspect::Depth*/)
                        view = internalTexture->views[1];
                    if (internalTexture->format == StorageFormat::Depth24Stencil8 &&  aspect == TextureAspect::Stencil)
                        view = internalTexture->views[2];

                    imageInfo.Add(
                        vk::DescriptorImageInfo()
                        .setSampler(vk::Sampler())
                        .setImageView(view)
                        .setImageLayout(vk::ImageLayout::eGeneral)//
                    );
                }
                else
                    imageInfo.Add(
                        vk::DescriptorImageInfo()
                        .setSampler(vk::Sampler())
                        .setImageView(vk::ImageView())
                        .setImageLayout(vk::ImageLayout::eGeneral)//
                    );
            }

            writeDescriptorSets.Add(
                vk::WriteDescriptorSet()
                .setDstSet(this->descriptorSet)
                .setDstBinding(location)
                .setDstArrayElement(0)
                .setDescriptorCount(textures.Count())
                .setDescriptorType(vk::DescriptorType::eStorageImage)
                .setPImageInfo(imageInfo.Buffer() + imageInfoStart)
                .setPBufferInfo(nullptr)
                .setPTexelBufferView(nullptr)
            );
        }

		virtual void Update(int location, GameEngine::Texture* texture, TextureAspect aspect) override
		{
            Update(location, MakeArrayView(texture), aspect);
		}

		virtual void Update(int location, GameEngine::TextureSampler* sampler) override
		{
			VK::TextureSampler* internalSampler = reinterpret_cast<VK::TextureSampler*>(sampler);
			
			imageInfo.Add(
				vk::DescriptorImageInfo()
				.setSampler(internalSampler->sampler)
				.setImageView(vk::ImageView())
				.setImageLayout(vk::ImageLayout::eUndefined)
			);

			writeDescriptorSets.Add(
				vk::WriteDescriptorSet()
				.setDstSet(this->descriptorSet)
				.setDstBinding(location)
				.setDstArrayElement(0)
				.setDescriptorCount(1)
				.setDescriptorType(vk::DescriptorType::eSampler)
				.setPImageInfo(&imageInfo.Last())
				.setPBufferInfo(nullptr)
				.setPTexelBufferView(nullptr)
			);
		}

		virtual void Update(int location, GameEngine::Buffer* buffer, int offset = 0, int length = -1) override
		{
			VK::BufferObject* internalBuffer = reinterpret_cast<VK::BufferObject*>(buffer);

			vk::DescriptorType descriptorType;
			if (internalBuffer->usage & vk::BufferUsageFlagBits::eUniformBuffer)
				descriptorType = vk::DescriptorType::eUniformBuffer;
			else
				descriptorType = vk::DescriptorType::eStorageBuffer;

			int range = (length == -1) ? internalBuffer->size : length;
			bufferInfo.Add(
				vk::DescriptorBufferInfo()
				.setBuffer(internalBuffer->buffer)
				.setOffset(offset)
				.setRange(range)
			);

			writeDescriptorSets.Add(
				vk::WriteDescriptorSet()
				.setDstSet(this->descriptorSet)
				.setDstBinding(location)
				.setDstArrayElement(0)
				.setDescriptorCount(1)
				.setDescriptorType(descriptorType)
				.setPImageInfo(nullptr)
				.setPBufferInfo(&bufferInfo.Last())
				.setPTexelBufferView(nullptr)
			);
		}

		virtual void EndUpdate() override
		{
			if (writeDescriptorSets.Count() > 0)
				RendererState::Device().updateDescriptorSets(
					vk::ArrayProxy<const vk::WriteDescriptorSet>(writeDescriptorSets.Count(), writeDescriptorSets.Buffer()), 
					nullptr);
		}
	};

	class PipelineBuilder;

	class Pipeline : public GameEngine::Pipeline
	{
	public:
#if _DEBUG
		CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> descriptorSets;
		bool isGraphics = true;
#endif
		int descSetCount = 0;
		vk::PipelineLayout pipelineLayout;
		vk::Pipeline pipeline;
		vk::PipelineBindPoint pipelineBindPoint;
	public:
		Pipeline() = default;
		Pipeline(RenderTargetLayout* renderTargetLayout, PipelineBuilder* pipelineBuilder);
		~Pipeline()
		{
			if (pipeline) RendererState::Device().destroyPipeline(pipeline);
			if (pipelineLayout) RendererState::Device().destroyPipelineLayout(pipelineLayout);
		}
	};

	class PipelineBuilder : public GameEngine::PipelineBuilder
	{
	public:
#if _DEBUG
		CoreLib::String debugName;
		CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> descriptorSets;
#endif

		CoreLib::List<vk::DescriptorSetLayout> setLayouts;
		CoreLib::List<vk::PushConstantRange> pushConstantRanges;//TODO:
		CoreLib::List<vk::PipelineShaderStageCreateInfo> shaderStages;
		CoreLib::List<vk::VertexInputBindingDescription> vertexBindingDescriptions;
		CoreLib::List<vk::VertexInputAttributeDescription> vertexAttributeDescriptions;
	public:
		virtual void SetShaders(CoreLib::ArrayView<GameEngine::Shader*> shaders) override
		{
			shaderStages.Clear();

#if _DEBUG
			bool vertPresent = false;
			bool tescControlPresent = false;
			bool tescEvalPresent = false;
			bool geometryPresent = false;
			bool fragPresent = false;
#endif
			for (auto shader : shaders)
			{
#if _DEBUG
				// Ensure that the device supports requested shader stages
				if (ShaderStage(dynamic_cast<VK::Shader*>(shader)->stage) == vk::ShaderStageFlagBits::eGeometry)
					assert(RendererState::PhysicalDevice().getFeatures().geometryShader);
				if (ShaderStage(dynamic_cast<VK::Shader*>(shader)->stage) == vk::ShaderStageFlagBits::eTessellationControl)
					assert(RendererState::PhysicalDevice().getFeatures().tessellationShader);
				if (ShaderStage(dynamic_cast<VK::Shader*>(shader)->stage) == vk::ShaderStageFlagBits::eTessellationEvaluation)
					assert(RendererState::PhysicalDevice().getFeatures().tessellationShader);

				// Ensure only one of any shader stage is present in the requested shader stage
				switch (ShaderStage(dynamic_cast<VK::Shader*>(shader)->stage))
				{
				case vk::ShaderStageFlagBits::eVertex:
					assert(vertPresent == false);
					vertPresent = true;
					break;
				case vk::ShaderStageFlagBits::eTessellationControl:
					assert(tescControlPresent == false);
					tescControlPresent = true;
					break;
				case vk::ShaderStageFlagBits::eTessellationEvaluation:
					assert(tescEvalPresent == false);
					tescEvalPresent = true;
					break;
				case vk::ShaderStageFlagBits::eGeometry:
					assert(geometryPresent == false);
					geometryPresent = true;
					break;
				case vk::ShaderStageFlagBits::eFragment:
					assert(fragPresent == false);
					fragPresent = true;
					break;
				case vk::ShaderStageFlagBits::eCompute:
					throw HardwareRendererException("Can't use compute shader in graphics pipeline");
					break;
				default:
					throw HardwareRendererException("Unknown shader stage");
				}
#endif

				shaderStages.Add(
					vk::PipelineShaderStageCreateInfo()
					.setFlags(vk::PipelineShaderStageCreateFlagBits())
					.setStage(ShaderStage(reinterpret_cast<VK::Shader*>(shader)->stage))
					.setModule(reinterpret_cast<VK::Shader*>(shader)->module)
					.setPName("main")
					.setPSpecializationInfo(nullptr)
				);
			}
		}
		virtual void SetVertexLayout(VertexFormat vertexFormat) override
		{
			//TODO: Improve
			int location = 0;
			int maxOffset = -1;
			int stride = 0;

			vertexAttributeDescriptions.Clear();
			vertexBindingDescriptions.Clear();

			for (auto attribute : vertexFormat.Attributes)
			{
				vertexAttributeDescriptions.Add(
					vk::VertexInputAttributeDescription()
					.setLocation(attribute.Location)
					.setBinding(0)
					.setFormat(TranslateVertexAttribute(attribute))
					.setOffset(attribute.StartOffset)
				);

				location++;
				if (attribute.StartOffset > maxOffset)
				{
					maxOffset = attribute.StartOffset;
					stride = maxOffset + DataTypeSize(attribute.Type);
				}
			}

			if (vertexAttributeDescriptions.Count() > 0)
			{
				vertexBindingDescriptions.Add(
					vk::VertexInputBindingDescription()
					.setBinding(0)
					.setStride(stride)
					.setInputRate(vk::VertexInputRate::eVertex)//TODO: per instance data?
				);
			}
		}
		virtual void SetBindingLayout(CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> pDescriptorSets) override
		{
#if _DEBUG
			this->descriptorSets = pDescriptorSets;
#endif
			for (auto& set : pDescriptorSets)
			{
				if (set) //TODO: ?
					setLayouts.Add(reinterpret_cast<VK::DescriptorSetLayout*>(set)->layout);
			}
		}
		virtual void SetDebugName(CoreLib::String name) override
		{
#if _DEBUG
			debugName = name;
#endif
		}
		virtual Pipeline* ToPipeline(GameEngine::RenderTargetLayout* renderTargetLayout) override
		{
			return new Pipeline(reinterpret_cast<RenderTargetLayout*>(renderTargetLayout), this);
		}
		virtual Pipeline* CreateComputePipeline(CoreLib::ArrayView<GameEngine::DescriptorSetLayout*> pDescriptorSets, GameEngine::Shader* shader) override
		{
			Pipeline * result = new Pipeline();
#if _DEBUG
			result->descriptorSets = pDescriptorSets;
#endif
			result->pipelineBindPoint = vk::PipelineBindPoint::eCompute;
			List<vk::DescriptorSetLayout> descSetLayouts;
			for (auto& set : pDescriptorSets)
			{
				if (set)
					descSetLayouts.Add(reinterpret_cast<VK::DescriptorSetLayout*>(set)->layout);
			}
			// Create Pipeline Layout
			vk::PipelineLayoutCreateInfo layoutCreateInfo = vk::PipelineLayoutCreateInfo()
				.setFlags(vk::PipelineLayoutCreateFlags())
				.setSetLayoutCount(pDescriptorSets.Count())
				.setPSetLayouts(descSetLayouts.Buffer())
				.setPushConstantRangeCount(0)
				.setPPushConstantRanges(nullptr);
#ifdef _DEBUG
			result->isGraphics = false;
#endif
            result->descSetCount = descSetLayouts.Count();
			result->pipelineLayout = RendererState::Device().createPipelineLayout(layoutCreateInfo);
			vk::ComputePipelineCreateInfo createInfo;
			createInfo.setLayout(result->pipelineLayout)
				.stage.setModule(reinterpret_cast<VK::Shader*>(shader)->module)
					  .setStage(vk::ShaderStageFlagBits::eCompute)
					  .setPName("main");
			result->pipeline = RendererState::Device().createComputePipeline(RendererState::PipelineCache(), createInfo);
			return result;
		}
	};

    vk::PolygonMode TranslatePolygonMode(PolygonMode mode)
    {
        switch (mode)
        {
        case PolygonMode::Fill:
            return vk::PolygonMode::eFill;
        case PolygonMode::Line:
            return vk::PolygonMode::eLine;
        case PolygonMode::Point:
            return vk::PolygonMode::ePoint;
        default:
            return vk::PolygonMode::eFill;
        }
    }

	Pipeline::Pipeline(RenderTargetLayout* renderTargetLayout, PipelineBuilder* pipelineBuilder)
	{
#if _DEBUG
		this->descriptorSets = pipelineBuilder->descriptorSets;
#endif
		// Create Pipeline Layout
		vk::PipelineLayoutCreateInfo layoutCreateInfo = vk::PipelineLayoutCreateInfo()
			.setFlags(vk::PipelineLayoutCreateFlags())
			.setSetLayoutCount(pipelineBuilder->setLayouts.Count())
			.setPSetLayouts(pipelineBuilder->setLayouts.Buffer())
			.setPushConstantRangeCount(pipelineBuilder->pushConstantRanges.Count())
			.setPPushConstantRanges(pipelineBuilder->pushConstantRanges.Buffer());
		this->descSetCount = pipelineBuilder->setLayouts.Count();
		this->pipelineLayout = RendererState::Device().createPipelineLayout(layoutCreateInfo);

		// Vertex Input Description
		vk::PipelineVertexInputStateCreateInfo vertexInputCreateInfo = vk::PipelineVertexInputStateCreateInfo()
			.setFlags(vk::PipelineVertexInputStateCreateFlags())
			.setVertexBindingDescriptionCount(pipelineBuilder->vertexBindingDescriptions.Count())
			.setPVertexBindingDescriptions(pipelineBuilder->vertexBindingDescriptions.Buffer())
			.setVertexAttributeDescriptionCount(pipelineBuilder->vertexAttributeDescriptions.Count())
			.setPVertexAttributeDescriptions(pipelineBuilder->vertexAttributeDescriptions.Buffer());

		// Create Input Assembly Description
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo = vk::PipelineInputAssemblyStateCreateInfo()
			.setFlags(vk::PipelineInputAssemblyStateCreateFlags())
			.setTopology(TranslatePrimitiveTopology(pipelineBuilder->FixedFunctionStates.PrimitiveTopology))
			.setPrimitiveRestartEnable(pipelineBuilder->FixedFunctionStates.PrimitiveRestartEnabled);

		// Create Viewport Description
		vk::PipelineViewportStateCreateInfo viewportCreateInfo = vk::PipelineViewportStateCreateInfo()
			.setFlags(vk::PipelineViewportStateCreateFlags())
			.setViewportCount(1)
			.setPViewports(nullptr)
			.setScissorCount(1)
			.setPScissors(nullptr);

		// Create Rasterization Description
		vk::PipelineRasterizationStateCreateInfo rasterizationCreateInfo = vk::PipelineRasterizationStateCreateInfo()
			.setFlags(vk::PipelineRasterizationStateCreateFlags())
			.setDepthClampEnable(VK_FALSE)
			.setRasterizerDiscardEnable(VK_FALSE)
			.setPolygonMode(TranslatePolygonMode(pipelineBuilder->FixedFunctionStates.PolygonFillMode))
			.setCullMode(TranslateCullMode(pipelineBuilder->FixedFunctionStates.cullMode))
			.setFrontFace(vk::FrontFace::eClockwise)
			.setDepthBiasEnable(pipelineBuilder->FixedFunctionStates.EnablePolygonOffset)
			.setDepthBiasConstantFactor(pipelineBuilder->FixedFunctionStates.PolygonOffsetUnits)
			.setDepthBiasClamp(0.0f)
			.setDepthBiasSlopeFactor(pipelineBuilder->FixedFunctionStates.PolygonOffsetFactor)
			.setLineWidth(1.0f);

        VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterStateCI{};
        if (pipelineBuilder->FixedFunctionStates.ConsevativeRasterization)
        {
            conservativeRasterStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
            conservativeRasterStateCI.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
            rasterizationCreateInfo.pNext = &conservativeRasterStateCI;
        }
		// Create Multisampling Description
		//TODO: Implement multisampling
		vk::PipelineMultisampleStateCreateInfo multisampleCreateInfo = vk::PipelineMultisampleStateCreateInfo()
			.setFlags(vk::PipelineMultisampleStateCreateFlags())
			.setRasterizationSamples(vk::SampleCountFlagBits::e1)
			.setSampleShadingEnable(VK_FALSE)
			.setMinSampleShading(1.0f)
			.setPSampleMask(nullptr)
			.setAlphaToCoverageEnable(VK_FALSE)
			.setAlphaToOneEnable(VK_FALSE);

		// Create Depth Stencil Description
		vk::PipelineDepthStencilStateCreateInfo depthStencilCreateInfo = vk::PipelineDepthStencilStateCreateInfo()
			.setFlags(vk::PipelineDepthStencilStateCreateFlags())
			.setDepthTestEnable(pipelineBuilder->FixedFunctionStates.DepthCompareFunc != CompareFunc::Disabled)
			.setDepthWriteEnable(pipelineBuilder->FixedFunctionStates.DepthCompareFunc != CompareFunc::Disabled)
			.setDepthCompareOp(TranslateCompareFunc(pipelineBuilder->FixedFunctionStates.DepthCompareFunc))
			.setDepthBoundsTestEnable(VK_FALSE)
			.setMinDepthBounds(0.0f)
			.setMaxDepthBounds(1.0f)
			.setStencilTestEnable(pipelineBuilder->FixedFunctionStates.StencilCompareFunc != CompareFunc::Disabled)
			.setFront(vk::StencilOpState()
				.setCompareOp(TranslateCompareFunc(pipelineBuilder->FixedFunctionStates.StencilCompareFunc))
				.setPassOp(TranslateStencilOp(pipelineBuilder->FixedFunctionStates.StencilDepthPassOp))
				.setFailOp(TranslateStencilOp(pipelineBuilder->FixedFunctionStates.StencilFailOp))
				.setDepthFailOp(TranslateStencilOp(pipelineBuilder->FixedFunctionStates.StencilDepthFailOp))
				.setCompareMask(pipelineBuilder->FixedFunctionStates.StencilMask)
				.setWriteMask(pipelineBuilder->FixedFunctionStates.StencilMask)
				.setReference(pipelineBuilder->FixedFunctionStates.StencilReference))
			.setBack(vk::StencilOpState()
				.setCompareOp(TranslateCompareFunc(pipelineBuilder->FixedFunctionStates.StencilCompareFunc))
				.setPassOp(TranslateStencilOp(pipelineBuilder->FixedFunctionStates.StencilDepthPassOp))
				.setFailOp(TranslateStencilOp(pipelineBuilder->FixedFunctionStates.StencilFailOp))
				.setDepthFailOp(TranslateStencilOp(pipelineBuilder->FixedFunctionStates.StencilDepthFailOp))
				.setCompareMask(pipelineBuilder->FixedFunctionStates.StencilMask)
				.setWriteMask(pipelineBuilder->FixedFunctionStates.StencilMask)
				.setReference(pipelineBuilder->FixedFunctionStates.StencilReference));

		// Create Blending Description
		CoreLib::List<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
		for (int i = 0; i<renderTargetLayout->colorReferences.Count(); i++)
		{
			colorBlendAttachments.Add(
				vk::PipelineColorBlendAttachmentState()
				.setBlendEnable(pipelineBuilder->FixedFunctionStates.blendMode != BlendMode::Replace)
				.setSrcColorBlendFactor(pipelineBuilder->FixedFunctionStates.blendMode == BlendMode::AlphaBlend ? vk::BlendFactor::eSrcAlpha : vk::BlendFactor::eOne)
				.setDstColorBlendFactor(pipelineBuilder->FixedFunctionStates.blendMode == BlendMode::AlphaBlend ? vk::BlendFactor::eOneMinusSrcAlpha : vk::BlendFactor::eZero)
				.setColorBlendOp(vk::BlendOp::eAdd)
				.setSrcAlphaBlendFactor(pipelineBuilder->FixedFunctionStates.blendMode == BlendMode::AlphaBlend ? vk::BlendFactor::eSrcAlpha : vk::BlendFactor::eOne)
				.setDstAlphaBlendFactor(pipelineBuilder->FixedFunctionStates.blendMode == BlendMode::AlphaBlend ? vk::BlendFactor::eSrcAlpha : vk::BlendFactor::eZero)
				.setAlphaBlendOp(vk::BlendOp::eAdd)
				.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
			);
		}

		vk::PipelineColorBlendStateCreateInfo colorBlendCreateInfo = vk::PipelineColorBlendStateCreateInfo()
			.setFlags(vk::PipelineColorBlendStateCreateFlags())
			.setLogicOpEnable(VK_FALSE)
			.setLogicOp(vk::LogicOp::eCopy)
			.setAttachmentCount(colorBlendAttachments.Count())
			.setPAttachments(colorBlendAttachments.Count() > 0 ? colorBlendAttachments.Buffer() : nullptr)
			.setBlendConstants({ 0.0f, 0.0f, 0.0f, 0.0f });

		// Create Dynamic Description
		CoreLib::Array<vk::DynamicState,2> dynamicStates;
		dynamicStates.Add(vk::DynamicState::eViewport);
		dynamicStates.Add(vk::DynamicState::eScissor);
		vk::PipelineDynamicStateCreateInfo dynamicStateCreateInfo = vk::PipelineDynamicStateCreateInfo()
			.setFlags(vk::PipelineDynamicStateCreateFlags())
			.setDynamicStateCount(dynamicStates.Count())
			.setPDynamicStates(dynamicStates.Buffer());

		// Create Pipeline Create Info
		vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::GraphicsPipelineCreateInfo()
			.setFlags(vk::PipelineCreateFlags())
			.setStageCount(pipelineBuilder->shaderStages.Count())
			.setPStages(pipelineBuilder->shaderStages.Buffer())
			.setPVertexInputState(&vertexInputCreateInfo)
			.setPInputAssemblyState(&inputAssemblyCreateInfo)
			.setPTessellationState(nullptr)
			.setPViewportState(&viewportCreateInfo)
			.setPRasterizationState(&rasterizationCreateInfo)
			.setPMultisampleState(&multisampleCreateInfo)
			.setPDepthStencilState(reinterpret_cast<VK::RenderTargetLayout*>(renderTargetLayout)->depthReference.layout == vk::ImageLayout::eUndefined ? nullptr : &depthStencilCreateInfo)
			.setPColorBlendState(reinterpret_cast<VK::RenderTargetLayout*>(renderTargetLayout)->colorReferences.Count() == 0 ? nullptr : &colorBlendCreateInfo)
			.setPDynamicState(&dynamicStateCreateInfo)
			.setLayout(pipelineLayout)
			.setRenderPass(reinterpret_cast<VK::RenderTargetLayout*>(renderTargetLayout)->renderPass)
			.setSubpass(0)
			.setBasePipelineHandle(vk::Pipeline())
			.setBasePipelineIndex(-1);

		this->pipeline = RendererState::Device().createGraphicsPipelines(RendererState::PipelineCache(), pipelineCreateInfo)[0];
		this->pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
//#if _DEBUG
//		vk::DebugMarkerObjectNameInfoEXT nameInfo = vk::DebugMarkerObjectNameInfoEXT()
//			.setObjectType(vk::DebugReportObjectTypeEXT::ePipeline)
//			.setObject(pipeline.operator VkPipeline)
//			.setPObjectName(pipelineBuilder->debugName.Buffer());
//
//		RendererState::Device().debugMarkerSetObjectNameEXT(&nameInfo);
//#endif
	}

	class Fence : public GameEngine::Fence
	{
	public:
		vk::Fence assocFence;
	public:
		Fence()
		{
			vk::FenceCreateInfo createInfo;
			createInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
			assocFence = RendererState::Device().createFence(createInfo);
		}
		~Fence() 
		{
			RendererState::Device().destroyFence(assocFence);
		}
		virtual void Reset() override
		{
			RendererState::Device().resetFences(assocFence);
		}
		virtual void Wait() override
		{
			static int waitCounter = 0;
			while (RendererState::Device().waitForFences(
				assocFence,
				VK_TRUE,
				1
			) != vk::Result::eSuccess) {
				waitCounter++;
			};
			/*if (waitCounter > 10)
				Print("waited %d\n", waitCounter);*/
		}
	};

	class CommandBuffer : public GameEngine::CommandBuffer
	{
	public:
		const vk::CommandPool& pool;
		vk::CommandBuffer buffer;
		Pipeline* curPipeline = nullptr;
		CoreLib::Array<vk::DescriptorSet, 32> pendingDescSets;
		CoreLib::List<VK::DescriptorSet*> descSets;

		CommandBuffer(const vk::CommandPool& commandPool) : pool(commandPool)
		{
			pendingDescSets.SetSize(32);
			buffer = RendererState::CreateCommandBuffer(pool, vk::CommandBufferLevel::eSecondary);
		}
		CommandBuffer() : CommandBuffer(RendererState::RenderCommandPool()) {}

		~CommandBuffer()
		{
			RendererState::DestroyCommandBuffer(pool, buffer);
		}

		Buffer* lastVertBuffer = nullptr;
		int lastVertBufferOffset = 0;
		Buffer * lastIndexBuffer = nullptr;
		int lastIndexBufferOffset = 0;

		void ResetInternalState()
		{
			curPipeline = nullptr;
			lastVertBuffer = nullptr;
			lastIndexBuffer = nullptr;
			lastVertBufferOffset = 0;
			lastIndexBufferOffset = 0;
			descSets.Clear();
		}

		virtual void SetEventMarker(const char * /*name*/, uint32_t /*color*/) override
        {
        }

		virtual void BeginRecording(GameEngine::FrameBuffer *frameBuffer) override
		{
            vk::RenderPass renderPass;
            if (frameBuffer)
            {
                renderPass = reinterpret_cast<VK::FrameBuffer *>(frameBuffer)->renderTargetLayout->renderPass;
            }

            ResetInternalState();

            for (int k = 0; k < pendingDescSets.Count(); k++)
                pendingDescSets[k] = vk::DescriptorSet();

            vk::CommandBufferInheritanceInfo inheritanceInfo =
                vk::CommandBufferInheritanceInfo()
                    .setRenderPass(renderPass)
                    .setSubpass(0)
                    .setOcclusionQueryEnable(VK_FALSE)
                    .setQueryFlags(vk::QueryControlFlags())
                    .setPipelineStatistics(vk::QueryPipelineStatisticFlags());

            vk::CommandBufferBeginInfo commandBufferBeginInfo =
                vk::CommandBufferBeginInfo()
                    .setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse |
                              vk::CommandBufferUsageFlagBits::eRenderPassContinue)
                    .setPInheritanceInfo(&inheritanceInfo);

            buffer.begin(commandBufferBeginInfo);
		}

		virtual void EndRecording() override
		{
			buffer.end();
		}

		virtual void BindVertexBuffer(Buffer* vertexBuffer, int byteOffset) override
		{
			if (byteOffset != lastVertBufferOffset || vertexBuffer != lastVertBuffer)
			{
				buffer.bindVertexBuffers(0, reinterpret_cast<VK::BufferObject*>(vertexBuffer)->buffer, { (vk::DeviceSize)byteOffset });
				lastVertBuffer = vertexBuffer;
				lastVertBufferOffset = byteOffset;
			}
		}

		virtual void BindIndexBuffer(Buffer* indexBuffer, int byteOffset) override
		{
			if (byteOffset != lastIndexBufferOffset || indexBuffer != lastIndexBuffer)
			{
				buffer.bindIndexBuffer(reinterpret_cast<VK::BufferObject*>(indexBuffer)->buffer,
					(vk::DeviceSize)byteOffset, vk::IndexType::eUint32);
				lastIndexBuffer = indexBuffer;
				lastIndexBufferOffset = byteOffset;
			}
		}

		virtual void BindDescriptorSet(int binding, GameEngine::DescriptorSet* descSet) override
		{
			VK::DescriptorSet* internalDescriptorSet = reinterpret_cast<VK::DescriptorSet*>(descSet);
			descSets.Add(internalDescriptorSet);
			if (descSet == nullptr)
				pendingDescSets[binding] = RendererState::GetEmptyDescriptorSet();
			else
				pendingDescSets[binding] = (internalDescriptorSet->descriptorSet);
		}

		virtual void BindPipeline(GameEngine::Pipeline* pipeline) override
		{
			auto newPipeline = reinterpret_cast<VK::Pipeline*>(pipeline);
			if (curPipeline != newPipeline)
			{
				curPipeline = newPipeline;
				buffer.bindPipeline(newPipeline->pipelineBindPoint, newPipeline->pipeline);
			}
		}

		virtual void SetViewport(Viewport viewport) override
        {
            vk::Viewport vkViewport;
            vkViewport.x = viewport.x;
            vkViewport.y = viewport.y;
            vkViewport.width = viewport.w;
            vkViewport.height = viewport.h;
            vkViewport.minDepth = viewport.minZ;
            vkViewport.maxDepth = viewport.maxZ;
            buffer.setViewport(0, 1, &vkViewport);
            vk::Rect2D scissorRect = vk::Rect2D(
                vk::Offset2D((int)viewport.x, (int)viewport.y), vk::Extent2D((int)viewport.w, (int)viewport.h));
            buffer.setScissor(0, 1, &scissorRect);
        }

		virtual void DispatchCompute(int groupCountX, int groupCountY, int groupCountZ) override
		{
            buffer.bindDescriptorSets(
                curPipeline->pipelineBindPoint,
                curPipeline->pipelineLayout,
                0,
                curPipeline->descSetCount,
                pendingDescSets.Buffer(),
                0, nullptr);
			buffer.dispatch(groupCountX, groupCountY, groupCountZ);
		}

		virtual void Draw(int firstVertex, int vertexCount) override
		{
			buffer.bindDescriptorSets(
				curPipeline->pipelineBindPoint,
				curPipeline->pipelineLayout,
				0,
				curPipeline->descSetCount,
				pendingDescSets.Buffer(),
				0, nullptr);
			buffer.draw(vertexCount, 1, firstVertex, 0);
		}
		virtual void DrawInstanced(int numInstances, int firstVertex, int vertexCount) override
		{
			buffer.bindDescriptorSets(
				curPipeline->pipelineBindPoint,
				curPipeline->pipelineLayout,
				0,
				curPipeline->descSetCount,
				pendingDescSets.Buffer(),
				0, nullptr);
			buffer.draw(vertexCount, numInstances, firstVertex, 0);
		}
		virtual void DrawIndexed(int firstIndex, int indexCount) override
		{
			buffer.bindDescriptorSets(
				curPipeline->pipelineBindPoint,
				curPipeline->pipelineLayout,
				0,
				curPipeline->descSetCount,
				pendingDescSets.Buffer(),
				0, nullptr);
			buffer.drawIndexed(indexCount, 1, firstIndex, 0, 0);
		}
		virtual void DrawIndexedInstanced(int numInstances, int firstIndex, int indexCount) override
		{
			buffer.bindDescriptorSets(
				curPipeline->pipelineBindPoint,
				curPipeline->pipelineLayout,
				0,
				curPipeline->descSetCount,
				pendingDescSets.Buffer(),
				0, nullptr);
			buffer.drawIndexed(indexCount, numInstances, firstIndex, 0, 0);
		}
	};

    class VkWindowSurface : public GameEngine::WindowSurface
    {
    public:
        WindowHandle handle = {};
        int width = -1;
        int height = -1;

        vk::SurfaceKHR surface;
        vk::SwapchainKHR swapchain;

        CoreLib::List<vk::Image> images; //alternatively could call getSwapchainImages each time
        CoreLib::List<vk::CommandBuffer> presentCommandBuffers, clearCommandBuffers;
		CoreLib::List<vk::Fence> presentCommandBufferFences;
        vk::Semaphore imageAvailableSemaphore, renderFinishedSemaphore;

        VkWindowSurface(WindowHandle windowHandle, int w, int h)
        {
            handle = windowHandle;
            width = w;
            height = h;
			if (windowHandle)
            	surface = RendererState::CreateSurface(windowHandle);
            CreateSwapchain();
            CreateSemaphores();
            Clear();
        }

        ~VkWindowSurface()
        {
            UnbindWindow();
        }

        void Present(GameEngine::Texture2D * srcImage)
        {
            if (images.Count() == 0) return;
			uint32_t nextImage = 0;
			try
			{
            	nextImage = RendererState::Device().acquireNextImageKHR(swapchain, UINT64_MAX, imageAvailableSemaphore, vk::Fence()).value;
			}
			catch (vk::OutOfDateKHRError)
			{
				CreateSwapchain();
				return;
			}
            static int frameId = 0;
            frameId++;
            vk::CommandBufferBeginInfo commandBufferBeginInfo = vk::CommandBufferBeginInfo()
                .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
                .setPInheritanceInfo(nullptr);

            vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1);

            vk::ImageMemoryBarrier postPresentBarrier = vk::ImageMemoryBarrier()
                .setSrcAccessMask(LayoutFlags(vk::ImageLayout::eUndefined))
                .setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(images[nextImage])
                .setSubresourceRange(imageSubresourceRange);

            vk::ImageMemoryBarrier prePresentBarrier = vk::ImageMemoryBarrier()
                .setSrcAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
                .setDstAccessMask(LayoutFlags(vk::ImageLayout::ePresentSrcKHR))
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(images[nextImage])
                .setSubresourceRange(imageSubresourceRange);

			int cmdBufId = frameId % presentCommandBuffers.Count();
			auto cmdBuffer = presentCommandBuffers[cmdBufId];
			RendererState::Device().waitForFences(1, &presentCommandBufferFences[cmdBufId], true, 0);
			RendererState::Device().resetFences(1, &presentCommandBufferFences[cmdBufId]);
			cmdBuffer.begin(commandBufferBeginInfo); // start recording
			cmdBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eAllCommands,
                vk::DependencyFlags(),
                nullptr,
                nullptr,
                postPresentBarrier
            );

			if (srcImage == nullptr)
			{
                // If no source image, clear to debug purple
				cmdBuffer.clearColorImage(
                    images[nextImage],
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ClearColorValue(std::array<float, 4>{ 1.0f, 0.0f, 1.0f, 0.0f }),
                    imageSubresourceRange
                );
            }
            else
            {
				auto srcTexture = dynamic_cast<Texture2D*>(srcImage);
                vk::ImageMemoryBarrier textureTransferBarrier = vk::ImageMemoryBarrier()
                    .setSrcAccessMask(LayoutFlags(srcTexture->currentSubresourceLayouts[0]))
                    .setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
                    .setOldLayout(srcTexture->currentSubresourceLayouts[0])
                    .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(srcTexture->image)
                    .setSubresourceRange(imageSubresourceRange);

				cmdBuffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eAllCommands,
                    vk::PipelineStageFlagBits::eAllCommands,
                    vk::DependencyFlags(),
                    nullptr,
                    nullptr,
                    textureTransferBarrier
                );

				srcTexture->currentSubresourceLayouts[0] = vk::ImageLayout::eTransferSrcOptimal;

                // Blit
                vk::ImageSubresourceLayers subresourceLayers = vk::ImageSubresourceLayers()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(0)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1);

                std::array<vk::Offset3D, 2> srcOffsets;
                srcOffsets[0] = vk::Offset3D(0, 0, 0);
                srcOffsets[1] = vk::Offset3D(dynamic_cast<VK::Texture2D*>(srcImage)->width, dynamic_cast<VK::Texture2D*>(srcImage)->height, 1);

                std::array<vk::Offset3D, 2> dstOffsets;
                dstOffsets[0] = vk::Offset3D(0, 0, 0);
                dstOffsets[1] = vk::Offset3D(width, height, 1);

                vk::ImageBlit blitRegions = vk::ImageBlit()
                    .setSrcSubresource(subresourceLayers)
                    .setSrcOffsets(srcOffsets)
                    .setDstSubresource(subresourceLayers)
                    .setDstOffsets(dstOffsets);

				cmdBuffer.blitImage(
                     dynamic_cast<VK::Texture2D*>(srcImage)->image,
                     vk::ImageLayout::eTransferSrcOptimal,
                     images[nextImage],
                     vk::ImageLayout::eTransferDstOptimal,
                     blitRegions,
                     vk::Filter::eNearest
                );
            }

			cmdBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eAllCommands,
                vk::DependencyFlags(),
                nullptr,
                nullptr,
                prePresentBarrier
            );
			
			cmdBuffer.end(); // stop recording

            vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlags(vk::PipelineStageFlagBits::eTransfer);

            vk::SubmitInfo submitInfo = vk::SubmitInfo()
                .setWaitSemaphoreCount(1)
                .setPWaitSemaphores(&imageAvailableSemaphore)
                .setPWaitDstStageMask(&waitDstStageMask)
                .setCommandBufferCount(1)
                .setPCommandBuffers(&cmdBuffer)
                .setSignalSemaphoreCount(1)
                .setPSignalSemaphores(&renderFinishedSemaphore);
			
            RendererState::RenderQueue().submit(submitInfo, presentCommandBufferFences[cmdBufId]);

            vk::PresentInfoKHR presentInfo = vk::PresentInfoKHR()
                .setWaitSemaphoreCount(1)
                .setPWaitSemaphores(&renderFinishedSemaphore)
                .setSwapchainCount(1)
                .setPSwapchains(&swapchain)
                .setPImageIndices(&nextImage)
                .setPResults(nullptr);
			try
			{
            	RendererState::RenderQueue().presentKHR(presentInfo);
			}
			catch (vk::OutOfDateKHRError)
			{
				CreateSwapchain();
			}
		}

        void CreateSwapchain()
        {
			if (!surface)
				return;
			vkDeviceWaitIdle(RendererState::Device());
			std::vector<vk::SurfaceFormatKHR> surfaceFormats = RendererState::PhysicalDevice().getSurfaceFormatsKHR(surface);
			vk::Format format;
            vk::ColorSpaceKHR colorSpace = surfaceFormats.at(0).colorSpace;
            if ((surfaceFormats.size() == 1) && (surfaceFormats.at(0).format == vk::Format::eUndefined))
                format = vk::Format::eB8G8R8A8Unorm;
            else
                format = surfaceFormats.at(0).format;

            // Select presentation mode
            vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo; // Fifo presentation mode is guaranteed
            for (auto & mode : RendererState::PhysicalDevice().getSurfacePresentModesKHR(surface))
            {
                // If we can use mailbox, use it.
                if (mode == vk::PresentModeKHR::eMailbox)
                {
                    presentMode = mode;
                    break;
                }
            }

            vk::SurfaceCapabilitiesKHR surfaceCapabilities = RendererState::PhysicalDevice().getSurfaceCapabilitiesKHR(surface);

            unsigned int desiredSwapchainImages = 2;
            if (desiredSwapchainImages < surfaceCapabilities.minImageCount)
			{
                desiredSwapchainImages = surfaceCapabilities.minImageCount;
            }
            if (surfaceCapabilities.maxImageCount > 0 
				&& desiredSwapchainImages > surfaceCapabilities.maxImageCount)
			{
                desiredSwapchainImages = surfaceCapabilities.maxImageCount;
            }

            vk::Extent2D swapchainExtent = {};
            if (surfaceCapabilities.currentExtent.width == -1)
			{
                swapchainExtent.width = this->width;
                swapchainExtent.height = this->height;
            }
            else 
			{
                swapchainExtent = surfaceCapabilities.currentExtent;
				this->width = swapchainExtent.width;
				this->height = swapchainExtent.height;
            }

            // Select swapchain pre-transform
            // (Can be useful on tablets, etc.)
            vk::SurfaceTransformFlagBitsKHR preTransform = surfaceCapabilities.currentTransform;
            if (surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
                // Select identity transform if we can
                preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
            }

            vk::SwapchainKHR oldSwapchain = swapchain;

            vk::SwapchainCreateInfoKHR swapchainCreateInfo = vk::SwapchainCreateInfoKHR()
                .setMinImageCount(desiredSwapchainImages)
                .setSurface(surface)
                .setImageFormat(format)
                .setImageColorSpace(colorSpace)
                .setImageExtent(swapchainExtent)
                .setImageArrayLayers(1)
                .setImageUsage(vk::ImageUsageFlagBits::eTransferDst) // we only draw to screen by blit
                .setImageSharingMode(vk::SharingMode::eExclusive)
                .setQueueFamilyIndexCount(0)
                .setPQueueFamilyIndices(VK_NULL_HANDLE)
                .setPreTransform(preTransform)
                .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                .setPresentMode(presentMode)
                .setClipped(VK_TRUE)
                .setOldSwapchain(oldSwapchain);

            swapchain = RendererState::Device().createSwapchainKHR(swapchainCreateInfo);
            DestroySwapchain(oldSwapchain);
            auto vkImages = RendererState::Device().getSwapchainImagesKHR(swapchain);
            images.SetSize((int)vkImages.size());
            memcpy(images.Buffer(), &vkImages[0], vkImages.size() * sizeof(vk::Image));
			CreateCommandBuffers();
        }

        void CreateCommandBuffers()
        {
            DestroyCommandBuffers();

            vk::CommandBufferAllocateInfo commandBufferAllocateInfo = vk::CommandBufferAllocateInfo()
                .setCommandPool(RendererState::SwapchainCommandPool())
                .setLevel(vk::CommandBufferLevel::ePrimary)
                .setCommandBufferCount((uint32_t)images.Count() * 2);

            presentCommandBuffers.SetSize(commandBufferAllocateInfo.commandBufferCount);
            RendererState::Device().allocateCommandBuffers(&commandBufferAllocateInfo, presentCommandBuffers.Buffer());
			clearCommandBuffers.SetSize(commandBufferAllocateInfo.commandBufferCount);
			RendererState::Device().allocateCommandBuffers(&commandBufferAllocateInfo, clearCommandBuffers.Buffer());

			presentCommandBufferFences.SetSize(presentCommandBuffers.Count());
			vk::FenceCreateInfo fenceCreateInfo;
			fenceCreateInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
			for (int i = 0; i < presentCommandBuffers.Count(); i++)
				presentCommandBufferFences[i] = RendererState::Device().createFence(fenceCreateInfo);

			// record clear command buffers
			for (int image = 0; image < images.Count(); image++)
			{
				//TODO: see if following line is beneficial
				//commandBuffers[image].reset(vk::CommandBufferResetFlags()); // implicitly done by begin

				vk::CommandBufferBeginInfo commandBufferBeginInfo = vk::CommandBufferBeginInfo()
					.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
					.setPInheritanceInfo(nullptr);

				vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1);

				vk::ImageMemoryBarrier postPresentBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(vk::AccessFlags())
					.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
					.setOldLayout(vk::ImageLayout::eUndefined)
					.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(images[image])
					.setSubresourceRange(imageSubresourceRange);

				vk::ImageMemoryBarrier prePresentBarrier = vk::ImageMemoryBarrier()
					.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
					.setDstAccessMask(LayoutFlags(vk::ImageLayout::ePresentSrcKHR))
					.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
					.setNewLayout(vk::ImageLayout::ePresentSrcKHR)
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(images[image])
					.setSubresourceRange(imageSubresourceRange);

				clearCommandBuffers[image].begin(commandBufferBeginInfo); // start recording
				clearCommandBuffers[image].pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eTransfer,
					vk::DependencyFlags(),
					nullptr,
					nullptr,
					postPresentBarrier
				);

				clearCommandBuffers[image].clearColorImage(
					images[image],
					vk::ImageLayout::eTransferDstOptimal,
					vk::ClearColorValue(std::array<float, 4>{ 0.467f, 0.725f, 0.0f, 0.0f }),
					imageSubresourceRange
				);

				clearCommandBuffers[image].pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eBottomOfPipe,
					vk::DependencyFlags(),
					nullptr,
					nullptr,
					prePresentBarrier
				);
				clearCommandBuffers[image].end(); // stop recording
			}

			// record present command buffers
        }

        void CreateSemaphores()
        {
            DestroySemaphores();

            vk::SemaphoreCreateInfo semaphoreCreateInfo;
            imageAvailableSemaphore = RendererState::Device().createSemaphore(semaphoreCreateInfo);
			renderFinishedSemaphore = RendererState::Device().createSemaphore(semaphoreCreateInfo);
        }

        void DestroySemaphores()
        {
            if (imageAvailableSemaphore) RendererState::Device().destroySemaphore(imageAvailableSemaphore);
			if (renderFinishedSemaphore) RendererState::Device().destroySemaphore(renderFinishedSemaphore);
        }

        void DestroyCommandBuffers()
        {
			if (presentCommandBufferFences.Count())
				RendererState::Device().waitForFences(presentCommandBufferFences.Count(), presentCommandBufferFences.Buffer(), true, 0);
			for (auto fence : presentCommandBufferFences)
				RendererState::Device().destroyFence(fence);
            for (auto commandBuffer : presentCommandBuffers)
                RendererState::Device().freeCommandBuffers(RendererState::SwapchainCommandPool(), commandBuffer);
			for (auto commandBuffer : clearCommandBuffers)
				RendererState::Device().freeCommandBuffers(RendererState::SwapchainCommandPool(), commandBuffer);
			
			presentCommandBufferFences.Clear();
        }

        void DestroySwapchain()
        {
            RendererState::Device().waitIdle();
			if (swapchain)
            	DestroySwapchain(swapchain);
        }

        void DestroySwapchain(vk::SwapchainKHR pswapchain)
        {
            if (pswapchain) RendererState::Device().destroySwapchainKHR(pswapchain);// shouldn't need this if, but nvidia driver is broken.
        }
        void UnbindWindow()
        {
            DestroySemaphores();
            DestroyCommandBuffers();
            DestroySwapchain();
            if (surface) RendererState::Instance().destroySurfaceKHR(surface);
            handle = WindowHandle();
        }
        void Clear()
        {
            
        }

        virtual void Resize(int pwidth, int pheight) override
        {
            if (!handle) return;
			if (width != pwidth || height != pheight)
			{
				this->width = pwidth;
				this->height = pheight;
				CreateSwapchain();
			}
        }
        virtual void GetSize(int & w, int & h) override
        {
            w = this->width;
            h = this->height;
        }
        virtual WindowHandle GetWindowHandle() override
        {
            return this->handle;
        }
    };

	class HardwareRenderer : public GameEngine::HardwareRenderer
	{
	private:
		List<vk::Semaphore> transferSemaphores;
        CoreLib::Array<vk::CommandBuffer, MaxRenderThreads> jobSubmissionBuffers;
		List<vk::ImageMemoryBarrier> imageMemoryBarriers;

		uint32_t taskId = 0;
		std::mutex queueMutex;
	public:
        virtual TargetShadingLanguage GetShadingLanguage() override
		{
			return TargetShadingLanguage::SPIRV;
		}
		
	public:
		HardwareRenderer(String pipelineCacheLocation)
		{
            RendererState::SetPipelineCacheLocation(pipelineCacheLocation);
			RendererState::AddRenderer();
            jobSubmissionBuffers.SetSize(MaxRenderThreads);
		};
		~HardwareRenderer()
		{
			RendererState::Device().waitIdle();
			for (auto & sem : transferSemaphores)
				RendererState::Device().destroySemaphore(sem);
			RendererState::RemRenderer();
		}
        virtual void ThreadInit(int threadId) override
        {
            renderThreadId = threadId;
        }
		virtual void Init(int versionCount) override
		{
			RendererState::Init(versionCount);
		}
		virtual void ResetTempBufferVersion(int version) override
		{
			RendererState::ResetTempBufferVersion(version);
		}

		virtual WindowSurface* CreateSurface(WindowHandle windowHandle, int pwidth, int pheight) override
		{
			VkWindowSurface * rs = new VkWindowSurface(windowHandle, pwidth, pheight);
            return rs;
		}

        virtual void BeginJobSubmission() override
        {
            // Create command buffer begin info
            vk::CommandBufferBeginInfo primaryBeginInfo = vk::CommandBufferBeginInfo()
                .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
                .setPInheritanceInfo(nullptr);
            jobSubmissionBuffers[renderThreadId] = RendererState::GetTempRenderCommandBuffer();
            jobSubmissionBuffers[renderThreadId].begin(primaryBeginInfo);
        }

        virtual void EndJobSubmission(GameEngine::Fence* fence) override
        {
            auto primaryBuffer = jobSubmissionBuffers[renderThreadId];
            primaryBuffer.end();
            vk::SubmitInfo submitInfo = vk::SubmitInfo()
                .setWaitSemaphoreCount(0)
                .setPWaitSemaphores(nullptr)
                .setPWaitDstStageMask(nullptr)
                .setCommandBufferCount(1)
                .setPCommandBuffers(&primaryBuffer)
                .setSignalSemaphoreCount(0)
                .setPSignalSemaphores(nullptr);
            RendererState::RenderQueue().submit(submitInfo, fence ? ((VK::Fence*)fence)->assocFence : nullptr);
        }

		void QueuePipelineBarrier(vk::CommandBuffer cmdBuffer, PipelineBarriers barriers)
		{
			if (barriers == PipelineBarriers::Memory || barriers == PipelineBarriers::MemoryAndImage)
			{
				auto memBarrier = vk::MemoryBarrier()
					.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eHostWrite)
					.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
                cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                    vk::PipelineStageFlagBits::eAllCommands,
					vk::DependencyFlags(), 1, &memBarrier, 0, nullptr, imageMemoryBarriers.Count(), imageMemoryBarriers.Buffer());
			}
			else if (barriers == PipelineBarriers::ExecutionOnly)
			{
                cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                    vk::PipelineStageFlagBits::eAllCommands,
					vk::DependencyFlags(), 0, nullptr, 0, nullptr, 0, nullptr);
			}
		}

        virtual void QueueComputeTask(GameEngine::Pipeline* computePipeline, GameEngine::DescriptorSet* descriptorSet, 
			int x, int y, int z, PipelineBarriers barriers) override
        {
			std::lock_guard<std::mutex> lock(queueMutex);
			taskId++;
			imageMemoryBarriers.Clear();
			if (barriers == PipelineBarriers::MemoryAndImage)
			{
				auto vkDescSet = (VK::DescriptorSet*)descriptorSet;
				for (auto& textureUseList : vkDescSet->textureUses)
				{
					for (auto textureUse : textureUseList)
					{
						if (textureUse.texture->lastLayoutCheckTaskId == taskId)
							continue;
						textureUse.texture->lastLayoutCheckTaskId = taskId;
						textureUse.texture->TransferLayout(textureUse.targetLayout, imageMemoryBarriers);
					}
				}
			}
            auto primaryBuffer = jobSubmissionBuffers[renderThreadId];
			QueuePipelineBarrier(primaryBuffer, barriers);
            auto pipeline = dynamic_cast<VK::Pipeline*>(computePipeline);
            auto descSet = dynamic_cast<VK::DescriptorSet*>(descriptorSet);
            primaryBuffer.bindPipeline(pipeline->pipelineBindPoint, pipeline->pipeline);
            primaryBuffer.bindDescriptorSets(pipeline->pipelineBindPoint, pipeline->pipelineLayout, 0, 1, &descSet->descriptorSet, 0, nullptr);
            primaryBuffer.dispatch(x, y, z);
        }

		virtual void QueueRenderPass(GameEngine::FrameBuffer *frameBuffer, bool clearFrameBuffer,
            CoreLib::ArrayView<GameEngine::CommandBuffer *> commands,
			PipelineBarriers barriers) override
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			taskId++;
			imageMemoryBarriers.Clear();
			auto vkframeBuffer = (VK::FrameBuffer*)frameBuffer;
			if (barriers == PipelineBarriers::MemoryAndImage)
			{
				// Determine texture layout transfer ops that are necessary
				Array<GameEngine::Texture*, 8> attachmentTextures;
				vkframeBuffer->renderAttachments.GetTextures(attachmentTextures);
				for (auto texture : attachmentTextures)
				{
					auto vkTexture = dynamic_cast<VK::Texture*>(texture);
					if (vkTexture->lastLayoutCheckTaskId == taskId)
						continue;
					vkTexture->lastLayoutCheckTaskId = taskId;
					if (texture->IsDepthStencilFormat())
						vkTexture->TransferLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal, imageMemoryBarriers);
					else
						vkTexture->TransferLayout(vk::ImageLayout::eColorAttachmentOptimal, imageMemoryBarriers);
				}
				for (auto cmdBuf : commands)
				{
					auto vkCmdBuf = (VK::CommandBuffer*)cmdBuf;
					for (auto descSet : vkCmdBuf->descSets)
					{
						for (auto& textureUseList : descSet->textureUses)
							for (auto textureUse : textureUseList)
							{
								if (textureUse.texture->lastLayoutCheckTaskId == taskId)
									continue;
								textureUse.texture->lastLayoutCheckTaskId = taskId;
								textureUse.texture->TransferLayout(textureUse.targetLayout, imageMemoryBarriers);
							}
					}
				}
			}

			// Create render pass begin info
            vk::RenderPassBeginInfo renderPassBeginInfo =
                vk::RenderPassBeginInfo()
                    .setRenderPass(clearFrameBuffer ? vkframeBuffer->renderTargetLayout->renderPassClear
                                                    : vkframeBuffer->renderTargetLayout->renderPass)
                    .setFramebuffer(clearFrameBuffer ? vkframeBuffer->framebufferClear : vkframeBuffer->framebuffer)
                    .setRenderArea(vk::Rect2D()
                                       .setOffset(vk::Offset2D(0, 0))
                                       .setExtent(vk::Extent2D(reinterpret_cast<VK::FrameBuffer *>(frameBuffer)->width,
                                           reinterpret_cast<VK::FrameBuffer *>(frameBuffer)->height)))
                    .setClearValueCount(vkframeBuffer->clearValues.Count())
                    .setPClearValues(vkframeBuffer->clearValues.Buffer());

			// Aggregate secondary command buffers
			CoreLib::List<vk::CommandBuffer> renderPassCommandBuffers;

			for (auto& buffer : commands)
			{
				auto internalBuffer = static_cast<VK::CommandBuffer*>(buffer);
				renderPassCommandBuffers.Add(internalBuffer->buffer);
			}

			// Record primary command buffer
            auto primaryBuffer = jobSubmissionBuffers[renderThreadId];
			QueuePipelineBarrier(primaryBuffer, barriers);
			if (renderPassCommandBuffers.Count() > 0)
            {
				primaryBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eSecondaryCommandBuffers);
				primaryBuffer.executeCommands(renderPassCommandBuffers.Count(), renderPassCommandBuffers.Buffer());
				primaryBuffer.endRenderPass();
			}
		}

		virtual void Wait() override
		{
			RendererState::Device().waitIdle();
		}
        virtual void Blit(GameEngine::Texture2D *dstImage, GameEngine::Texture2D *srcImage, VectorMath::Vec2i dstOffset,
            SourceFlipMode flipSrc) override
		{
            auto primaryBuffer = jobSubmissionBuffers[renderThreadId];

			vk::ImageSubresourceRange imageSubresourceRange = vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1);

			auto dstTexture = dynamic_cast<VK::Texture*>(dstImage);

			vk::ImageMemoryBarrier preBlitBarrier = vk::ImageMemoryBarrier()
				.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eUndefined))
				.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferDstOptimal))
				.setOldLayout(dstTexture->currentSubresourceLayouts[0])
				.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(dstTexture->image)
				.setSubresourceRange(imageSubresourceRange);

			dstTexture->currentSubresourceLayouts[0] = vk::ImageLayout::eTransferDstOptimal;
						
			auto srcTexture = dynamic_cast<Texture2D*>(srcImage);

			vk::ImageMemoryBarrier textureTransferBarrier = vk::ImageMemoryBarrier()
				.setSrcAccessMask(LayoutFlags(vk::ImageLayout::eColorAttachmentOptimal))
				.setDstAccessMask(LayoutFlags(vk::ImageLayout::eTransferSrcOptimal))
				.setOldLayout(srcTexture->currentSubresourceLayouts[0])
				.setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(srcTexture->image)
				.setSubresourceRange(imageSubresourceRange);

			primaryBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eAllCommands,
				vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlags(),
				0, nullptr,
				0, nullptr,
				2, MakeArray(preBlitBarrier, textureTransferBarrier).Buffer());

			// Blit
			vk::ImageSubresourceLayers subresourceLayers = vk::ImageSubresourceLayers()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setMipLevel(0)
				.setBaseArrayLayer(0)
				.setLayerCount(1);

			int srcWidth, srcHeight, destWidth, destHeight;
			dynamic_cast<VK::Texture2D*>(srcImage)->GetSize(srcWidth, srcHeight);
			dynamic_cast<VK::Texture2D*>(dstImage)->GetSize(destWidth, destHeight);

			std::array<vk::Offset3D, 2> srcOffsets;
			if (flipSrc != SourceFlipMode::None)
			{
				srcOffsets[0] = vk::Offset3D(0, srcHeight, 0);
				srcOffsets[1] = vk::Offset3D(srcWidth, 0, 1);
			}
			else
			{
				srcOffsets[0] = vk::Offset3D(0, 0, 0);
				srcOffsets[1] = vk::Offset3D(srcWidth, srcHeight, 1);
			}
			std::array<vk::Offset3D, 2> dstOffsets;
			dstOffsets[0] = vk::Offset3D(dstOffset.x, dstOffset.y, 0);
			dstOffsets[1] = vk::Offset3D(dstOffset.x + srcWidth, dstOffset.y + srcHeight, 1);
			int wFix = 0, hFix = 0;
			if (dstOffsets[1].x > destWidth)
				wFix = destWidth - dstOffsets[1].x;
			if (dstOffsets[1].y > destHeight)
				hFix = destHeight - dstOffsets[1].y;
			dstOffsets[1].x += wFix;
			dstOffsets[1].y += hFix;
			srcOffsets[1].x += wFix;
			srcOffsets[1].y += hFix;

			vk::ImageBlit blitRegions = vk::ImageBlit()
				.setSrcSubresource(subresourceLayers)
				.setSrcOffsets(srcOffsets)
				.setDstSubresource(subresourceLayers)
				.setDstOffsets(dstOffsets);

			primaryBuffer.blitImage(
				srcTexture->image,
				vk::ImageLayout::eTransferSrcOptimal,
				dstTexture->image,
				vk::ImageLayout::eTransferDstOptimal,
				blitRegions,
				vk::Filter::eNearest
			);

			srcTexture->currentSubresourceLayouts[0] = vk::ImageLayout::eTransferSrcOptimal;
		}
		virtual void Present(GameEngine::WindowSurface *surface, GameEngine::Texture2D* srcImage) override
		{
          	((VkWindowSurface*)surface)->Present(srcImage);
		}

		virtual BufferObject* CreateBuffer(BufferUsage usage, int size, const BufferStructureInfo* structInfo) override
		{
			CORELIB_UNUSED(structInfo);
			return new BufferObject(TranslateUsageFlags(usage) | vk::BufferUsageFlagBits::eTransferSrc, size, vk::MemoryPropertyFlagBits::eDeviceLocal);
		}

		virtual BufferObject* CreateMappedBuffer(BufferUsage usage, int size, const BufferStructureInfo* structInfo) override
		{
			CORELIB_UNUSED(structInfo);
			return new BufferObject(TranslateUsageFlags(usage), size, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		}

		Texture2D* CreateTexture2D(CoreLib::String name, int pwidth, int pheight, StorageFormat format, DataType dataType, void* data) override
		{
			TextureUsage usage;
			usage = TextureUsage::Sampled;
			int mipLevelCount = (int)Math::Log2Floor(Math::Max(pwidth, pheight)) + 1;
			Texture2D* res = new Texture2D(name, usage, pwidth, pheight, mipLevelCount, format);
			res->SetData(pwidth, pheight, 1, dataType, data);
			res->BuildMipmaps();
			return res;
		}

		Texture2D* CreateTexture2D(CoreLib::String name, TextureUsage usage, int pwidth, int pheight, int mipLevelCount, StorageFormat format) override
		{
			Texture2D* res = new Texture2D(name, usage, pwidth, pheight, mipLevelCount, format);
			return res;
		}

		Texture2D* CreateTexture2D(CoreLib::String name, TextureUsage usage, int pwidth, int pheight, int mipLevelCount, StorageFormat format, DataType dataType, CoreLib::ArrayView<void*> mipLevelData) override
		{
			Texture2D* res = new Texture2D(name, usage, pwidth, pheight, mipLevelCount, format);
			for (int level = 0; level < mipLevelCount; level++)
				res->SetData(level, Math::Max(pwidth >> level, 1), Math::Max(pheight >> level, 1), 1, dataType, mipLevelData[level]);
			return res;
		}

		Texture2DArray* CreateTexture2DArray(CoreLib::String name, TextureUsage usage, int w, int h, int layers, int mipLevelCount, StorageFormat format) override
		{
			Texture2DArray* res = new Texture2DArray(name, usage, w, h, mipLevelCount, layers, format);
			return res;
		}

		TextureCube* CreateTextureCube(CoreLib::String name, TextureUsage usage, int size, int mipLevelCount, StorageFormat format) override
		{
			TextureCube* res = new TextureCube(name, usage, size, mipLevelCount, format);
			return res;
		}

		virtual TextureCubeArray* CreateTextureCubeArray(CoreLib::String name, TextureUsage usage, int size, int mipLevelCount, int cubemapCount, StorageFormat format) override
		{
			TextureCubeArray * rs = new TextureCubeArray(name, usage, cubemapCount, size, mipLevelCount, format);
			return rs;
		}

		Texture3D* CreateTexture3D(CoreLib::String name, TextureUsage usage, int w, int h, int d, int mipLevelCount, StorageFormat format) override
		{
			Texture3D* res = new Texture3D(name, usage, w, h, d, mipLevelCount, format);
			return res;
		}

		TextureSampler* CreateTextureSampler() override
		{
			return new TextureSampler();
		}

		Shader* CreateShader(ShaderType stage, const char* data, int size) override
		{
			Shader* result = new Shader();
			result->Create(stage, data, size);
			return result;
		}

		virtual RenderTargetLayout* CreateRenderTargetLayout(CoreLib::ArrayView<AttachmentLayout> bindings, bool ignoreInitialContent) override
		{
			return new RenderTargetLayout(bindings, ignoreInitialContent);
		}

		virtual PipelineBuilder* CreatePipelineBuilder() override
		{
			return new PipelineBuilder();
		}

		virtual DescriptorSetLayout* CreateDescriptorSetLayout(CoreLib::ArrayView<DescriptorLayout> descriptors) override
		{
			return new DescriptorSetLayout(descriptors);
		}

		virtual DescriptorSet * CreateDescriptorSet(GameEngine::DescriptorSetLayout* layout) override
		{
			return new DescriptorSet(reinterpret_cast<VK::DescriptorSetLayout*>(layout));
		}

		Fence* CreateFence() override
		{
			return new Fence();
		}

		CommandBuffer* CreateCommandBuffer() override
		{
			return new CommandBuffer();
		}

		virtual int UniformBufferAlignment() override
		{
			return (int)RendererState::PhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
		}
		virtual int StorageBufferAlignment() override
		{
			return (int)RendererState::PhysicalDevice().getProperties().limits.minStorageBufferOffsetAlignment;
		}

		virtual String GetRendererName() override
		{
			return RendererState::PhysicalDevice().getProperties().deviceName;
		}
	};
}

HardwareRenderer* GameEngine::CreateVulkanHardwareRenderer(int gpuId, String pipelineCacheLocation)
{
	VK::GpuId = gpuId;
	return new VK::HardwareRenderer(pipelineCacheLocation);
}