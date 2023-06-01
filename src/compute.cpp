// C std
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <math.h>
// C++ std
#include <vector>
#include <string>
#include <algorithm>
// Dependencies
#include <vulkan/vulkan.h>
// Renderdoc
#include <dlfcn.h>
#include "renderdoc_app.h"
// Project
#include "aio.h"
#include "clock.h"
#include "desc.h"


static const bool VALIDATION_LAYER = true;
static const bool DEBUG_MARKERS = true;
static const char *INSTANCE_LAYERS[] = { "VK_LAYER_KHRONOS_validation" };
static const char *INSTANCE_EXTENSIONS[] = { 
					VK_KHR_SURFACE_EXTENSION_NAME,
					VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
					VK_EXT_DEBUG_REPORT_EXTENSION_NAME };
static const char *DEVICE_EXTENSIONS[] = {
					VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
					VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
					VK_KHR_RAY_QUERY_EXTENSION_NAME,
					VK_KHR_SWAPCHAIN_EXTENSION_NAME };


static PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = 0;
static PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTag = 0;
static PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = 0;
static PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = 0;
static PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabel = 0;


struct Compute
{
	VkInstance instance;
	VkDebugUtilsMessengerEXT_T *debugMessenger;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkDevice device;
	VkCommandPool graphicsCommandPool;
	VkCommandPool transferCommandPool;
	VkCommandPool computeCommandPool;
	VkDescriptorPool descriptorPool;
	VkQueue graphicsQueue;
	VkQueue transferQueue;
	VkQueue computeQueue;
};

struct AIOWorkload
{
	std::vector<std::string> files;
	std::string path;
	VkDeviceSize offset[2];
	VkDeviceSize size;
	Access access;
};

struct Memory
{
	VkDeviceMemory memory;
	VkDeviceSize offset;
	VkDeviceSize alignment;
	char *mapped;
};

struct Workflow
{
	VkCommandBuffer graphicsCmdBuffers[2];
	VkCommandBuffer graphicsQFOTCmdBuffers[2];
	VkCommandBuffer transferCmdBuffers[2];
	VkCommandBuffer transferUniqueCmdBuffers[2];
	VkCommandBuffer computeCmdBuffers[2];
	std::vector<AIOWorkload> aioWorkload;
	std::vector<AIOWorkload> aioUniqueWorkload;

	Memory memory[Access_Count] = {0};
	VkDescriptorSet computeDescriptors[2];
	std::vector<VkBuffer> buffers;
	std::vector<VkPipeline> pipelines;
	std::vector<VkShaderModule> shaderModules;
	std::vector<VkDescriptorSetLayout> descriptorLayouts;
	std::vector<VkPipelineLayout> pipelineLayouts;
	std::vector<VkFence> fences;
};


static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
							VkDebugUtilsMessageTypeFlagsEXT messageType,
							const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
							void* pUserData)
{
    printf("validation layer: %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}


static int32_t findProperties(const VkPhysicalDeviceMemoryProperties* pMemoryProperties,
                       uint32_t memoryTypeBitsRequirement,
                       VkMemoryPropertyFlags requiredProperties)
{
    const uint32_t memoryCount = pMemoryProperties->memoryTypeCount;

    for (uint32_t memoryIndex = 0; memoryIndex < memoryCount; ++memoryIndex)
	{
        const uint32_t memoryTypeBits = (1 << memoryIndex);
        const bool isRequiredMemoryType = memoryTypeBitsRequirement & memoryTypeBits;

        const VkMemoryPropertyFlags properties = pMemoryProperties->memoryTypes[memoryIndex].propertyFlags;
        const bool hasRequiredProperties = (properties & requiredProperties) == requiredProperties;

        if (isRequiredMemoryType && hasRequiredProperties)
            return static_cast<int32_t>(memoryIndex);
    }

    // failed to find memory type
    return -1;
}

int computeCreate(Compute *_compute)
{
	int result = 1;

	// Enumerate extensions
	{
		uint32_t extensionCount = 64;
		VkExtensionProperties extensionProperties[64];
		vkEnumerateInstanceExtensionProperties(0, &extensionCount, extensionProperties);

		printf("Vulkan extensions:\n");
		for(uint32_t i = 0; i < extensionCount; ++i)
		{
			printf("%d. %s\n", i, extensionProperties[i].extensionName);
		}
	}

	// Create Vulkan instance
	{
		VkApplicationInfo appInfo;
		memset(&appInfo, 0, sizeof(VkApplicationInfo));
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Stream Workflow";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo;
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;

		memset(&createInfo, 0, sizeof(VkInstanceCreateInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = sizeof(INSTANCE_EXTENSIONS) / sizeof(const char *);
		createInfo.ppEnabledExtensionNames = INSTANCE_EXTENSIONS;
		
		if(VALIDATION_LAYER)
		{
			memset(&debugCreateInfo, 0, sizeof(VkDebugUtilsMessengerCreateInfoEXT));
			debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugCreateInfo.messageSeverity = //VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
							  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
							  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
							VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
							VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugCreateInfo.pfnUserCallback = &vulkanDebugCallback;

			createInfo.enabledLayerCount = sizeof(INSTANCE_LAYERS) / sizeof(const char *);
			createInfo.ppEnabledLayerNames = INSTANCE_LAYERS;
			createInfo.pNext = &debugCreateInfo;
		}

		VkResult result = vkCreateInstance(&createInfo, 0, &_compute->instance);

		if(VALIDATION_LAYER)
		{
			PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
				(PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(_compute->instance, 
				"vkCreateDebugUtilsMessengerEXT");
			vkCreateDebugUtilsMessengerEXT(_compute->instance, &debugCreateInfo, 0, &_compute->debugMessenger);
		}
		
	}

	// Enumerate devices
	{
		uint32_t deviceCount = 64;
		VkPhysicalDevice physicalDevices[64];
		vkEnumeratePhysicalDevices(_compute->instance, &deviceCount, physicalDevices);

		printf("Vulkan physical devices:\n");
		for(uint32_t i = 0; i < deviceCount; ++i)
		{
			VkPhysicalDeviceProperties deviceProperties;
			vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceProperties);

			uint32_t maj = VK_API_VERSION_MAJOR(deviceProperties.apiVersion);
			uint32_t min = VK_API_VERSION_MINOR(deviceProperties.apiVersion);
			printf("%d. %s VK_%d_%d\n", i, deviceProperties.deviceName, maj, min);
		}

		_compute->physicalDevice = physicalDevices[0];
	}

	// Enumerate physical device extensions
	{
		uint32_t extensionCount = 64;
		VkExtensionProperties extensionProperties[64];
		vkEnumerateDeviceExtensionProperties(_compute->physicalDevice, 0, &extensionCount, extensionProperties);

		printf("Physical device extensions:\n");
		for(uint32_t i = 0; i < extensionCount; ++i)
		{
			printf("%d. %s\n", i, extensionProperties[i].extensionName);
		}
	}

	// Enumerate memory properties
	{
		vkGetPhysicalDeviceMemoryProperties(_compute->physicalDevice, &_compute->memoryProperties);
		//printf("Memory property types:\n");
		//for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		//{
		//	printf("%d. 0x%x\n", i, memoryProperties.memoryTypes[i].propertyFlags);
		//}
	}

	// Enumerate queue families
	{
		uint32_t queueFamilyCount = 64;
		VkQueueFamilyProperties queueFamilies[64];
		vkGetPhysicalDeviceQueueFamilyProperties(_compute->physicalDevice, &queueFamilyCount, queueFamilies);

		printf("Vulkan queue families:\n");
		for(uint32_t i = 0; i < queueFamilyCount; ++i)
		{
			printf("%d. flg:0x%x, cnt:%d\n", i, queueFamilies[i].queueFlags, queueFamilies[i].queueCount);
		}
	}

	// Create device
	{
		float queuePriorites[] = { 1.0f };

		VkDeviceQueueCreateInfo queueCreateInfos[3];
		memset(queueCreateInfos, 0, sizeof(queueCreateInfos));
		queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfos[0].queueFamilyIndex = 0;
		queueCreateInfos[0].queueCount = sizeof(queuePriorites) / sizeof(float);
		queueCreateInfos[0].pQueuePriorities = queuePriorites;

		queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfos[1].queueFamilyIndex = 1;
		queueCreateInfos[1].queueCount = sizeof(queuePriorites) / sizeof(float);
		queueCreateInfos[1].pQueuePriorities = queuePriorites;

		queueCreateInfos[2].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfos[2].queueFamilyIndex = 2;
		queueCreateInfos[2].queueCount = sizeof(queuePriorites) / sizeof(float);
		queueCreateInfos[2].pQueuePriorities = queuePriorites;

		VkPhysicalDeviceFeatures deviceFeatures;
		memset(&deviceFeatures, 0, sizeof(VkPhysicalDeviceFeatures));

		VkDeviceCreateInfo createInfo;
		memset(&createInfo, 0, sizeof(VkDeviceCreateInfo));
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pQueueCreateInfos = queueCreateInfos;
		createInfo.queueCreateInfoCount = sizeof(queueCreateInfos) / sizeof(VkDeviceQueueCreateInfo);
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = sizeof(DEVICE_EXTENSIONS) / sizeof(const char *);
		createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS;

		vkCreateDevice(_compute->physicalDevice, &createInfo, 0, &_compute->device);
		vkGetDeviceQueue(_compute->device, 0, 0, &_compute->graphicsQueue);
		vkGetDeviceQueue(_compute->device, 1, 0, &_compute->transferQueue);
		vkGetDeviceQueue(_compute->device, 2, 0, &_compute->computeQueue);

		VkCommandPoolCreateInfo poolInfo;
		memset(&poolInfo, 0, sizeof(VkCommandPoolCreateInfo));
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = 0;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(_compute->device, &poolInfo, 0, &_compute->graphicsCommandPool);

		poolInfo.queueFamilyIndex = 1;
		vkCreateCommandPool(_compute->device, &poolInfo, 0, &_compute->transferCommandPool);

		poolInfo.queueFamilyIndex = 2;
		vkCreateCommandPool(_compute->device, &poolInfo, 0, &_compute->computeCommandPool);
	}

	// Create descriptor set pool
	{
		VkDescriptorPoolSize poolSizes[6];
		memset(&poolSizes, 0, sizeof(poolSizes));
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 32;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
		poolSizes[1].descriptorCount = 32;
		poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		poolSizes[2].descriptorCount = 32;
		poolSizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolSizes[3].descriptorCount = 32;
		poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[4].descriptorCount = 32;
		poolSizes[5].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSizes[5].descriptorCount = 32;

		VkDescriptorPoolCreateInfo poolInfo;
		memset(&poolInfo, 0, sizeof(VkDescriptorPoolCreateInfo));
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = sizeof(poolSizes) / sizeof(VkDescriptorPoolSize);
		poolInfo.pPoolSizes = poolSizes;
		poolInfo.maxSets = 32;
		vkCreateDescriptorPool(_compute->device, &poolInfo, 0, &_compute->descriptorPool);
	}

	if(DEBUG_MARKERS)
	{
		vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT) vkGetDeviceProcAddr(_compute->device, 
				"vkSetDebugUtilsObjectNameEXT");
		vkSetDebugUtilsObjectTag = (PFN_vkSetDebugUtilsObjectTagEXT) vkGetDeviceProcAddr(_compute->device, 
				"vkSetDebugUtilsObjectTagEXT");
		vkCmdBeginDebugUtilsLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT) vkGetDeviceProcAddr(_compute->device, 
				"vkCmdBeginDebugUtilsLabelEXT");
		vkCmdEndDebugUtilsLabel = (PFN_vkCmdEndDebugUtilsLabelEXT) vkGetDeviceProcAddr(_compute->device, 
				"vkCmdEndDebugUtilsLabelEXT");
		vkCmdInsertDebugUtilsLabel = (PFN_vkCmdInsertDebugUtilsLabelEXT) vkGetDeviceProcAddr(_compute->device, 
				"vkCmdInsertDebugUtilsLabelEXT");
	}

	return result;
}


static void *loadFile(const char *_name, size_t *_size)
{
	size_t size = 0;
	void *buffer = 0;	
	FILE *fp = fopen(_name, "rb");

	if(fp != 0)
	{
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		buffer = malloc(size);
		size_t read = fread(buffer, 1, size, fp);
		fclose(fp);
	}

	*_size = size;	
	return buffer;
}


void computeDestroy(Compute *_compute)
{
	vkDeviceWaitIdle(_compute->device);
	vkDestroyCommandPool(_compute->device, _compute->graphicsCommandPool, 0);
	vkDestroyCommandPool(_compute->device, _compute->transferCommandPool, 0);
	vkDestroyCommandPool(_compute->device, _compute->computeCommandPool, 0);
	vkDestroyDescriptorPool(_compute->device, _compute->descriptorPool, 0);
	vkDestroyDevice(_compute->device, 0);

	if(VALIDATION_LAYER)
	{
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT) 
			vkGetInstanceProcAddr(_compute->instance, "vkDestroyDebugUtilsMessengerEXT");
		vkDestroyDebugUtilsMessengerEXT(_compute->instance, _compute->debugMessenger, 0);
	}
	
	vkDestroyInstance(_compute->instance, 0);
}


void computeDestroyWorkflow(Compute *_compute, Workflow *_workflow)
{
	for(VkBuffer &buffer : _workflow->buffers)
		vkDestroyBuffer(_compute->device, buffer, 0);
	for(VkPipeline &pipeline : _workflow->pipelines)
		vkDestroyPipeline(_compute->device, pipeline, 0);
	for(VkShaderModule &module : _workflow->shaderModules)
		vkDestroyShaderModule(_compute->device, module, 0);
	for(VkDescriptorSetLayout &descLayout : _workflow->descriptorLayouts)
		vkDestroyDescriptorSetLayout(_compute->device, descLayout, 0);
	for(VkPipelineLayout &pipelineLayout : _workflow->pipelineLayouts)
		vkDestroyPipelineLayout(_compute->device, pipelineLayout, 0);
	for(VkFence &fence : _workflow->fences)
		vkDestroyFence(_compute->device, fence, 0);
	for(int i = 0; i < Access_Count; ++i)
	{
		if(_workflow->memory[i].mapped)
			vkUnmapMemory(_compute->device, _workflow->memory[i].memory);
		if(_workflow->memory[i].memory != 0)
			vkFreeMemory(_compute->device, _workflow->memory[i].memory, 0);
	}
}

static VkBufferUsageFlags accessToBufferUsage(Access _access)
{
	static VkBufferUsageFlags mapping[Access_Count] = {
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // Access_GPU_Read
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // Access_GPU_Write
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // Access_GPU_ReadWrite
		VK_BUFFER_USAGE_TRANSFER_DST_BIT, // Access_CPU_Read
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT // Access_CPU_Write
	};

	return mapping[_access];
}

static VkMemoryPropertyFlags accessToMemoryFlags(Access _access)
{
	static VkMemoryPropertyFlags mapping[Access_Count] = {
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //Access_GPU_Read
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //Access_GPU_Write
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //Access_GPU_ReadWrite
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // Access_CPU_Read
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT // Access_CPU_Write
	};

	return mapping[_access];
}

static void allocateMemory(Compute *_compute, Workflow *_workflow, const Description *_desc)
{
	VkBufferCreateInfo bufferInfo;
	memset(&bufferInfo, 0, sizeof(VkBufferCreateInfo));
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = 1024;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkMemoryAllocateInfo allocInfo;
	memset(&allocInfo, 0, sizeof(VkMemoryAllocateInfo));
   	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

	VkBuffer buffer;
	VkMemoryRequirements memory;

	for(int i = 0; i < Access_Count; ++i)
	{
		bufferInfo.usage = accessToBufferUsage(Access(i));
		vkCreateBuffer(_compute->device, &bufferInfo, 0, &buffer);
		vkGetBufferMemoryRequirements(_compute->device, buffer, &memory);
		vkDestroyBuffer(_compute->device, buffer, 0);

		allocInfo.memoryTypeIndex = findProperties(&_compute->memoryProperties, memory.memoryTypeBits, accessToMemoryFlags(Access(i)));
		//printf("Memory type chosen: %d\n", allocInfo.memoryTypeIndex);

		_workflow->memory[i].alignment = memory.alignment;
		allocInfo.allocationSize = _desc->parameters.poolSizes[i];
		vkAllocateMemory(_compute->device, &allocInfo, 0, &_workflow->memory[i].memory);
	}

	// Map CPU memory
	vkMapMemory(_compute->device, _workflow->memory[Access_CPU_Read].memory, 0, VK_WHOLE_SIZE, 0, 
					(void **) &_workflow->memory[Access_CPU_Read].mapped);
	vkMapMemory(_compute->device, _workflow->memory[Access_CPU_Write].memory, 0, VK_WHOLE_SIZE, 0,
					(void **) &_workflow->memory[Access_CPU_Write].mapped); 
}

static void allocateCommandBuffers(Compute *_compute, VkCommandPool _commandPool, 
	uint32_t _count, VkCommandBuffer *_commandBuffers)
{
	VkCommandBufferAllocateInfo allocInfo;
	memset(&allocInfo, 0, sizeof(VkCommandBufferAllocateInfo));
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = _commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = _count;

	vkAllocateCommandBuffers(_compute->device, &allocInfo, _commandBuffers);
}

static void createShaderModule(Compute *_compute, Workflow *_workflow, 
	const char *_filename, VkShaderModule *_module, const char *_name = "")
{
	size_t size = 0;
	void *blob = loadFile(_filename, &size);

	VkShaderModuleCreateInfo createInfo;
	memset(&createInfo, 0, sizeof(VkShaderModuleCreateInfo));
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = size;
	createInfo.pCode = (const uint32_t*) blob;

	vkCreateShaderModule(_compute->device, &createInfo, 0, _module);
	_workflow->shaderModules.push_back(*_module);

	free(blob);

	if(DEBUG_MARKERS)
	{
		VkDebugUtilsObjectNameInfoEXT nameInfo;
		memset(&nameInfo, 0, sizeof(VkDebugMarkerObjectNameInfoEXT));
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
		nameInfo.objectHandle = (uint64_t) *_module;
		nameInfo.pObjectName = _name;
		vkSetDebugUtilsObjectName(_compute->device, &nameInfo);
	}
}

static void createFence(Compute *_compute, Workflow *_workflow, VkFence *_fence)
{
	VkFenceCreateInfo createInfo;
	memset(&createInfo, 0, sizeof(VkFenceCreateInfo));
	createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	createInfo.flags = 0; //VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(_compute->device, &createInfo, 0, _fence);
	_workflow->fences.push_back(*_fence);
}

static void createDescriptorLayout(Compute *_compute, Workflow *_workflow, 
	const VkDescriptorSetLayoutBinding *_bindings, int _count, VkDescriptorSetLayout *_descriptorLayout)
{
	VkDescriptorSetLayoutCreateInfo descriptorCreateInfo;
	memset(&descriptorCreateInfo, 0, sizeof(VkDescriptorSetLayoutCreateInfo));
	descriptorCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorCreateInfo.bindingCount = _count;
	descriptorCreateInfo.pBindings = _bindings;
	vkCreateDescriptorSetLayout(_compute->device, &descriptorCreateInfo, 0, _descriptorLayout);
	_workflow->descriptorLayouts.push_back(*_descriptorLayout);
}

static void createComputePipeline(Compute *_compute, Workflow *_workflow, 
	const VkShaderModule *_module, const VkPipelineLayout *_layout, VkPipeline *_pipeline)
{
	VkPipelineShaderStageCreateInfo shaderStage;
	memset(&shaderStage, 0, sizeof(shaderStage));
	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStage.module = *_module;
	shaderStage.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo;
	memset(&pipelineInfo, 0, sizeof(VkComputePipelineCreateInfo));
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = shaderStage;
	pipelineInfo.layout = *_layout;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;
	vkCreateComputePipelines(_compute->device, VK_NULL_HANDLE, 1, &pipelineInfo, 0, _pipeline);
	_workflow->pipelines.push_back(*_pipeline);
}

static void createPipelineLayout(Compute *_compute, Workflow *_workflow, 
	const VkDescriptorSetLayout *_descriptorLayout, VkPipelineLayout *_pipelineLayout)
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo;
	memset(&pipelineLayoutInfo, 0, sizeof(VkPipelineLayoutCreateInfo));
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = _descriptorLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = 0;
	vkCreatePipelineLayout(_compute->device, &pipelineLayoutInfo, 0, _pipelineLayout);
	_workflow->pipelineLayouts.push_back(*_pipelineLayout);
}

static void createBuffer(Compute *_compute, Workflow *_workflow, VkDeviceSize _size, Access _access, 
	VkBuffer *_buffer, VkDeviceSize *_offset, const char *_name = "")
{
	VkBufferCreateInfo bufferInfo;
	memset(&bufferInfo, 0, sizeof(VkBufferCreateInfo));
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = _size;
	bufferInfo.usage = accessToBufferUsage(_access);
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vkCreateBuffer(_compute->device, &bufferInfo, 0, _buffer);
	_workflow->buffers.push_back(*_buffer);

	Memory *memory = _workflow->memory + _access;
	vkBindBufferMemory(_compute->device, *_buffer, memory->memory, memory->offset);
	*_offset = memory->offset;

	// Keep next offset aligned
	VkDeviceSize asize = _size & (~(memory->alignment - 1));
	memory->offset += (_size == asize) ? asize : asize + memory->alignment;

	if(DEBUG_MARKERS)
	{
		VkDebugUtilsObjectNameInfoEXT nameInfo;
		memset(&nameInfo, 0, sizeof(VkDebugMarkerObjectNameInfoEXT));
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
		nameInfo.objectHandle = (uint64_t) *_buffer;
		nameInfo.pObjectName = _name;
		vkSetDebugUtilsObjectName(_compute->device, &nameInfo);
	}
}

static void transferQueueOwnership(VkCommandBuffer _cmdBuffer, bool _acquire, Access _access, uint32_t _srcIndex, uint32_t _dstIndex, VkBuffer _buffer, VkDeviceSize _size)
{
	// Queue family ownership transfer
	VkBufferMemoryBarrier barrier;
	memset(&barrier, 0, sizeof(VkBufferMemoryBarrier));
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = _srcIndex;
	barrier.dstQueueFamilyIndex = _dstIndex;
	barrier.buffer = _buffer;
	barrier.offset = 0;
	barrier.size = _size;

	VkPipelineStageFlags srcStage;
	VkPipelineStageFlags dstStage;

	switch(_access)
	{
		case Access_GPU_Read:
			srcStage = _acquire ? VK_PIPELINE_STAGE_NONE : VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = _acquire ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_NONE;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		case Access_GPU_Write:
			srcStage = _acquire ? VK_PIPELINE_STAGE_NONE : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dstStage = _acquire ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_NONE;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;
	}

	if(DEBUG_MARKERS)
	{
		VkDebugUtilsLabelEXT labelInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
			0, "Buffer Memory Barrier", { 1.0f, 0.0f, 0.0f, 1.0f }};
		vkCmdBeginDebugUtilsLabel(_cmdBuffer, &labelInfo);
	}

	vkCmdPipelineBarrier(_cmdBuffer, srcStage, dstStage, 0, 0, 0, 1, &barrier, 0, 0);

	if(DEBUG_MARKERS)
	{
		vkCmdEndDebugUtilsLabel(_cmdBuffer);
	}
}

static void createTransferCommand(VkCommandBuffer _transferCmdBuffer, VkBuffer _source, VkBuffer _dest, 
							VkDeviceSize _size,	const char *_name, float _color[4])
{
	if(DEBUG_MARKERS)
	{
		VkDebugUtilsLabelEXT labelInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
			0, _name, {_color[0], _color[1], _color[2], _color[3]} };
		vkCmdBeginDebugUtilsLabel(_transferCmdBuffer, &labelInfo);
	}

	VkBufferCopy copyRegion;
	memset(&copyRegion, 0, sizeof(VkBufferCopy));
	copyRegion.srcOffset = 0; // Optional
	copyRegion.dstOffset = 0; // Optional
	copyRegion.size = _size;
	vkCmdCopyBuffer(_transferCmdBuffer, _source, _dest, 1, &copyRegion);

	if(DEBUG_MARKERS)
	{
		vkCmdEndDebugUtilsLabel(_transferCmdBuffer);
	}
}

static const char *buildName(char *_dst, const char *_name, const char *_info)
{
	strcpy(_dst, _name);
	strcat(_dst, _info);
	return _dst;
}

static void aioWorkloadCallback(const char *_name, bool _directory, size_t _size, void *_data)
{
	AIOWorkload *workload = (AIOWorkload *) _data;
	if(!_directory)
	{
		std::string file = workload->path + "/" + _name;
		workload->files.push_back(file);
	}
}

static void createAIOWorkload(std::vector<AIOWorkload> *_aioWorkload, const char *_path, bool _directory,
						Access _access, VkDeviceSize _offset[2], VkDeviceSize _size, int _iterations)
{
	AIOWorkload workload;
	workload.access = _access;
	workload.offset[0] = _offset[0];
	workload.offset[1] = _offset[1];
	workload.size = _size;
	workload.path = _path;

	if(_directory)
	{
		if(_access == Access_CPU_Write)
		{
			bool ret = aioScanDirectory(_path, aioWorkloadCallback, &workload);
		}
		else if(_access == Access_CPU_Read)
		{
			for(int i = 0; i < _iterations; ++i)
			{
				char tmp[256];
				sprintf(tmp, "/output%04d.dat", i);
				std::string file = workload.path + tmp;
				workload.files.push_back(file);
			}
		}
	}
	else
	{
		std::string file = _path;
		workload.files.push_back(file);
	}

	std::sort(workload.files.begin(), workload.files.end());
	_aioWorkload->push_back(workload);
}

static void createAIOCommands(Workflow *_workflow, AIOCmdBuffer *_aioCmdBuffer, const std::vector<AIOWorkload> *_aioWorkload, 
							Access _access, int _index)
{
	int lsb = _index & 0x1;
	for(const AIOWorkload &workload : *_aioWorkload)
	{
		if(workload.access == _access)
		{
			void *buffer = _workflow->memory[_access].mapped + workload.offset[lsb];
			if(_access == Access_CPU_Write) 
				aioCmdRead(_aioCmdBuffer, buffer, workload.files[_index].c_str(), workload.size);
			else
				aioCmdWrite(_aioCmdBuffer, buffer, workload.files[_index].c_str(), workload.size);
		}
	}
}

int computeCreateWorkflow(Compute *_compute, Workflow *_workflow, const Description *_desc)
{
	int iterations = -1;

	// Allocate memory
	allocateMemory(_compute, _workflow, _desc);

	// Allocate command buffers
	allocateCommandBuffers(_compute, _compute->graphicsCommandPool, 2, _workflow->graphicsCmdBuffers);
	allocateCommandBuffers(_compute, _compute->graphicsCommandPool, 2, _workflow->graphicsQFOTCmdBuffers);
	allocateCommandBuffers(_compute, _compute->transferCommandPool, 2, _workflow->transferCmdBuffers);
	allocateCommandBuffers(_compute, _compute->transferCommandPool, 2, _workflow->transferUniqueCmdBuffers);
	allocateCommandBuffers(_compute, _compute->computeCommandPool, 2, _workflow->computeCmdBuffers);

	VkDescriptorBufferInfo bufferInfos[2][256];
	VkWriteDescriptorSet descriptorWrites[2][256];
	memset(descriptorWrites, 0, sizeof(descriptorWrites));
	VkDescriptorSetLayoutBinding descriptorBindings[256];
	memset(descriptorBindings, 0, sizeof(descriptorBindings));
	// {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0}, //samplerPoint
	// {11, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0}, //dataIn
	// {12, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0}, //lutIn
	// {20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0}, //nodeIn
	// {30, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0}, //Constants
	// {31, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0}, //Constants2
	// {40, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0} //hdrOut


	// ------------- Iterate over JSON data ----------------------------------------------------------------

	VkCommandBufferBeginInfo beginInfo;
	memset(&beginInfo, 0, sizeof(VkCommandBufferBeginInfo));
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0; //VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = 0; // Optional
	vkBeginCommandBuffer(_workflow->transferCmdBuffers[0], &beginInfo);
	vkBeginCommandBuffer(_workflow->transferCmdBuffers[1], &beginInfo);
	vkBeginCommandBuffer(_workflow->transferUniqueCmdBuffers[0], &beginInfo);
	vkBeginCommandBuffer(_workflow->transferUniqueCmdBuffers[1], &beginInfo);

	if(DEBUG_MARKERS)
	{
		VkDebugUtilsLabelEXT labelInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
			0, "Transfer Cmds", { 0.7f, 0.7f, 0.7f, 1.0f }};
		vkCmdBeginDebugUtilsLabel(_workflow->transferCmdBuffers[0], &labelInfo);
		vkCmdBeginDebugUtilsLabel(_workflow->transferCmdBuffers[1], &labelInfo);

		VkDebugUtilsLabelEXT labelInfo2 = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
			0, "Transfer Unique Cmds", { 0.6f, 0.6f, 0.6f, 1.0f }};
		vkCmdBeginDebugUtilsLabel(_workflow->transferUniqueCmdBuffers[0], &labelInfo2);
		vkCmdBeginDebugUtilsLabel(_workflow->transferUniqueCmdBuffers[1], &labelInfo2);
	}

	iterations = _desc->parameters.iterations;

	int count = _desc->dataCount;
	for(int i = 0; i < count; ++i)
	{
		char debugName[128];
		const Data *item = _desc->dataList + i;

		if(item->source == DataSource_Memory)
		{
			VkBuffer buffer;
			VkDeviceSize offset;
			createBuffer(_compute, _workflow, item->size, Access_GPU_ReadWrite, &buffer, &offset, 
							buildName(debugName, item->name, "_GPU_ReadWrite"));

			bufferInfos[0][i].buffer = bufferInfos[1][i].buffer = buffer;
			bufferInfos[0][i].offset = bufferInfos[1][i].offset = 0;
			bufferInfos[0][i].range = bufferInfos[1][i].range = item->size;
		}
		else if(item->source == DataSource_File)
		{
			if(item->access == DataAccess_Read)
			{
				VkBuffer sbuffer;
				VkDeviceSize offsets[2];
				createBuffer(_compute, _workflow, item->size, Access_CPU_Write, &sbuffer, &offsets[0], 
								buildName(debugName, item->name, "_CPU_Write"));

				offsets[1] = offsets[0];
				createAIOWorkload(&_workflow->aioUniqueWorkload, item->path, false, Access_CPU_Write, offsets, item->size, iterations);
			
				VkBuffer buffer;
				VkDeviceSize offset;
				createBuffer(_compute, _workflow, item->size, Access_GPU_Read, &buffer, &offset, 
								buildName(debugName, item->name, "_GPU_Read"));

				float color[4] = { 1.0f, 0.4f, 0.4f, 1.0f };
				createTransferCommand(_workflow->transferUniqueCmdBuffers[0], sbuffer, buffer, item->size, item->name, color);

				bufferInfos[0][i].buffer = bufferInfos[1][i].buffer = buffer;
				bufferInfos[0][i].offset = bufferInfos[1][i].offset = 0;
				bufferInfos[0][i].range = bufferInfos[1][i].range = item->size;
			}
			else if(item->access == DataAccess_Write)
			{
				VkBuffer sbuffer;
				VkDeviceSize offsets[2];
				createBuffer(_compute, _workflow, item->size, Access_CPU_Read, &sbuffer, &offsets[0],
								buildName(debugName, item->name, "_CPU_Read"));

				offsets[1] = offsets[0];
				createAIOWorkload(&_workflow->aioUniqueWorkload, item->path, false, Access_CPU_Read, offsets, item->size, iterations);

				VkBuffer buffer;
				VkDeviceSize offset;
				createBuffer(_compute, _workflow, item->size, Access_GPU_Write, &buffer, &offset, 
								buildName(debugName, item->name, "_GPU_Write"));

				float color[4] = { 0.4f, 0.4f, 1.0f, 1.0f };
				createTransferCommand(_workflow->transferUniqueCmdBuffers[1], buffer, sbuffer, item->size, item->name, color);

				bufferInfos[0][i].buffer = bufferInfos[1][i].buffer = buffer;
				bufferInfos[0][i].offset = bufferInfos[1][i].offset = 0;
				bufferInfos[0][i].range = bufferInfos[1][i].range = item->size;
			}
		}
		else if(item->source == DataSource_Directory)
		{
			if(item->access == DataAccess_Read)
			{
				VkBuffer sbuffer[2];
				VkDeviceSize offsets[2];
				createBuffer(_compute, _workflow, item->size, Access_CPU_Write, &sbuffer[0], &offsets[0], 
								buildName(debugName, item->name, "_CPU_Write_0"));
				createBuffer(_compute, _workflow, item->size, Access_CPU_Write, &sbuffer[1], &offsets[1], 
								buildName(debugName, item->name, "_CPU_Write_1"));
				createAIOWorkload(&_workflow->aioWorkload, item->path, true, Access_CPU_Write, offsets, item->size, iterations);
			
				VkBuffer buffer[2];
				createBuffer(_compute, _workflow, item->size, Access_GPU_Read, &buffer[0], &offsets[0], 
								buildName(debugName, item->name, "_GPU_Read_0"));
				createBuffer(_compute, _workflow, item->size, Access_GPU_Read, &buffer[1], &offsets[1],
								buildName(debugName, item->name, "_GPU_Read_1"));

				float color[4] = { 1.0f, 0.4f, 0.4f, 1.0f };
				createTransferCommand(_workflow->transferCmdBuffers[0], sbuffer[1], buffer[1], item->size, item->name, color);
				//transferQueueOwnership(_workflow->transferCmdBuffers[0], false, Access_GPU_Read, _compute->transferIndex, _compute->graphicsIndex, buffer[1], item->size);
				//transferQueueOwnership(_workflow->graphicsCmdBuffers[0], true, Access_GPU_Read, _compute->transferIndex, _compute->graphicsIndex, buffer[1], item->size);

				createTransferCommand(_workflow->transferCmdBuffers[1], sbuffer[0], buffer[0], item->size, item->name, color);
				//transferQueueOwnership(_workflow->transferCmdBuffers[1], false, Access_GPU_Read, _compute->transferIndex, _compute->graphicsIndex, buffer[0], item->size);
				//transferQueueOwnership(_workflow->graphicsCmdBuffers[1], true, Access_GPU_Read, _compute->transferIndex, _compute->graphicsIndex, buffer[0], item->size);

				bufferInfos[0][i].buffer = buffer[0]; bufferInfos[1][i].buffer = buffer[1];
				bufferInfos[0][i].offset = bufferInfos[1][i].offset = 0;
				bufferInfos[0][i].range = bufferInfos[1][i].range = item->size;
			}
			else if(item->access == DataAccess_Write)
			{
				VkBuffer sbuffer[2];
				VkDeviceSize offsets[2];
				createBuffer(_compute, _workflow, item->size, Access_CPU_Read, &sbuffer[0], &offsets[0], 
								buildName(debugName, item->name, "_CPU_Read_0"));
				createBuffer(_compute, _workflow, item->size, Access_CPU_Read, &sbuffer[1], &offsets[1], 
								buildName(debugName, item->name, "_CPU_Read_1"));
				createAIOWorkload(&_workflow->aioWorkload, item->path, true, Access_CPU_Read, offsets, item->size, iterations);

				VkBuffer buffer[2];
				createBuffer(_compute, _workflow, item->size, Access_GPU_Write, &buffer[0], &offsets[0],
								buildName(debugName, item->name, "_GPU_Write_0"));
				createBuffer(_compute, _workflow, item->size, Access_GPU_Write, &buffer[1], &offsets[1], 
								buildName(debugName, item->name, "_GPU_Write_1"));
			
				float color[4] = { 0.4f, 0.4f, 1.0f, 1.0f };
				createTransferCommand(_workflow->transferCmdBuffers[0], buffer[1], sbuffer[1], item->size, item->name, color);
				createTransferCommand(_workflow->transferCmdBuffers[1], buffer[0], sbuffer[0], item->size, item->name, color);
				
				bufferInfos[0][i].buffer = buffer[0]; bufferInfos[1][i].buffer = buffer[1];
				bufferInfos[0][i].offset = bufferInfos[1][i].offset = 0;
				bufferInfos[0][i].range = bufferInfos[1][i].range = item-> size;
			}
		}

		descriptorBindings[i].binding = i;
		descriptorBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorBindings[i].descriptorCount = 1;
		descriptorBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		descriptorBindings[i].pImmutableSamplers = 0;

		descriptorWrites[0][i].sType = descriptorWrites[1][i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0][i].dstSet = descriptorWrites[1][i].dstSet = 0; // Fill later
		descriptorWrites[0][i].dstBinding = descriptorWrites[1][i].dstBinding = i;
		descriptorWrites[0][i].dstArrayElement = descriptorWrites[1][i].dstArrayElement = 0;
		descriptorWrites[0][i].descriptorType = descriptorWrites[1][i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[0][i].descriptorCount = descriptorWrites[1][i].descriptorCount = 1;
		descriptorWrites[0][i].pBufferInfo = &bufferInfos[0][i]; descriptorWrites[1][i].pBufferInfo = &bufferInfos[1][i];
	}

	if(DEBUG_MARKERS)
	{
		vkCmdEndDebugUtilsLabel(_workflow->transferCmdBuffers[0]);
		vkCmdEndDebugUtilsLabel(_workflow->transferCmdBuffers[1]);
		vkCmdEndDebugUtilsLabel(_workflow->transferUniqueCmdBuffers[0]);
		vkCmdEndDebugUtilsLabel(_workflow->transferUniqueCmdBuffers[1]);
	}

	vkEndCommandBuffer(_workflow->transferCmdBuffers[0]);
	vkEndCommandBuffer(_workflow->transferCmdBuffers[1]);
	vkEndCommandBuffer(_workflow->transferUniqueCmdBuffers[0]);
	vkEndCommandBuffer(_workflow->transferUniqueCmdBuffers[1]);

	VkDescriptorSetLayout descriptorLayout;
	createDescriptorLayout(_compute, _workflow, descriptorBindings, count, &descriptorLayout);

	VkDescriptorSetAllocateInfo allocInfo;
	memset(&allocInfo, 0, sizeof(VkDescriptorSetAllocateInfo));
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _compute->descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &descriptorLayout;
	vkAllocateDescriptorSets(_compute->device, &allocInfo, &_workflow->computeDescriptors[0]);
	vkAllocateDescriptorSets(_compute->device, &allocInfo, &_workflow->computeDescriptors[1]);

	for(int i = 0; i < count; ++i)
	{
		descriptorWrites[0][i].dstSet = _workflow->computeDescriptors[0];
		descriptorWrites[1][i].dstSet = _workflow->computeDescriptors[1];
	}
	vkUpdateDescriptorSets(_compute->device, count, descriptorWrites[0], 0, 0);
	vkUpdateDescriptorSets(_compute->device, count, descriptorWrites[1], 0, 0);

	VkPipelineLayout pipelineLayout;
	createPipelineLayout(_compute, _workflow, &descriptorLayout, &pipelineLayout);

	// ------- Iterate over JSON program -------------------------------------------------------------

	memset(&beginInfo, 0, sizeof(VkCommandBufferBeginInfo));
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	//beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = 0; // Optional
	
	vkBeginCommandBuffer(_workflow->graphicsCmdBuffers[0], &beginInfo);
	vkBeginCommandBuffer(_workflow->graphicsCmdBuffers[1], &beginInfo);

	if(DEBUG_MARKERS)
	{
		VkDebugUtilsLabelEXT labelInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
			0, "Graphics Cmds", { 0.4f, 1.0f, 0.4f, 1.0f }};
		vkCmdBeginDebugUtilsLabel(_workflow->graphicsCmdBuffers[0], &labelInfo);
		vkCmdBeginDebugUtilsLabel(_workflow->graphicsCmdBuffers[1], &labelInfo);
	}

	count = _desc->programCount;
	for(int i = 0; i < count; ++i)
	{
		const Program *item = _desc->programList + i;

		VkShaderModule module;
		createShaderModule(_compute, _workflow, item->path, &module, item->path);

		VkPipeline pipeline;
		createComputePipeline(_compute, _workflow, &module, &pipelineLayout, &pipeline);

		vkCmdBindPipeline(_workflow->graphicsCmdBuffers[0], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindPipeline(_workflow->graphicsCmdBuffers[1], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

		vkCmdBindDescriptorSets(_workflow->graphicsCmdBuffers[0], VK_PIPELINE_BIND_POINT_COMPUTE, 
			pipelineLayout, 0, 1, &_workflow->computeDescriptors[0], 0, 0);
		vkCmdBindDescriptorSets(_workflow->graphicsCmdBuffers[1], VK_PIPELINE_BIND_POINT_COMPUTE, 
			pipelineLayout, 0, 1, &_workflow->computeDescriptors[1], 0, 0);

		if(DEBUG_MARKERS)
		{
			VkDebugUtilsLabelEXT labelInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
				0, item->name, { 1.0f, 1.0f, 0.4f, 1.0f }};
			vkCmdBeginDebugUtilsLabel(_workflow->graphicsCmdBuffers[0], &labelInfo);
			vkCmdBeginDebugUtilsLabel(_workflow->graphicsCmdBuffers[1], &labelInfo);
		}

		vkCmdDispatch(_workflow->graphicsCmdBuffers[0], item->dispatch[0], item->dispatch[1], item->dispatch[2]);
		vkCmdDispatch(_workflow->graphicsCmdBuffers[1], item->dispatch[0], item->dispatch[1], item->dispatch[2]);

		if(DEBUG_MARKERS)
		{
			vkCmdEndDebugUtilsLabel(_workflow->graphicsCmdBuffers[0]);
			vkCmdEndDebugUtilsLabel(_workflow->graphicsCmdBuffers[1]);
		}

		VkMemoryBarrier barrier;
		memset(&barrier, 0, sizeof(VkMemoryBarrier));
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		if(DEBUG_MARKERS)
		{
			VkDebugUtilsLabelEXT labelInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
				0, "Memory Barrier", { 1.0f, 0.0f, 0.0f, 1.0f }};
			vkCmdBeginDebugUtilsLabel(_workflow->graphicsCmdBuffers[0], &labelInfo);
			vkCmdBeginDebugUtilsLabel(_workflow->graphicsCmdBuffers[1], &labelInfo);
		}


		vkCmdPipelineBarrier(_workflow->graphicsCmdBuffers[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, 0, 0, 0);
		vkCmdPipelineBarrier(_workflow->graphicsCmdBuffers[1], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, 0, 0, 0);

		if(DEBUG_MARKERS)
		{
			vkCmdEndDebugUtilsLabel(_workflow->graphicsCmdBuffers[0]);
			vkCmdEndDebugUtilsLabel(_workflow->graphicsCmdBuffers[1]);
		}

	}

	if(DEBUG_MARKERS)
	{
		vkCmdEndDebugUtilsLabel(_workflow->graphicsCmdBuffers[0]);
		vkCmdEndDebugUtilsLabel(_workflow->graphicsCmdBuffers[1]);
	}

	vkEndCommandBuffer(_workflow->graphicsCmdBuffers[0]);
	vkEndCommandBuffer(_workflow->graphicsCmdBuffers[1]);

	// Update iterations according to AIO workload count
	for(const AIOWorkload &workload : _workflow->aioWorkload)
	{
		int filecount = (int) workload.files.size();
		iterations = (filecount < iterations) ? filecount : iterations;
	}

	return iterations;
}

int computeExecuteWorkflow()
{
	RENDERDOC_API_1_1_2 *rdoc_api = NULL;
	if(void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
	{
		pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
		int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&rdoc_api);
		assert(ret == 1);
	}

	Compute device;
	Workflow compute;
	computeCreate(&device);
	AIO *aio = aioCreate(256);

	//const Description *desc = descCreateFromFile("data/sha256.json");
	//const Description *desc = descCreateFromFile("data/test.json");
	//const Description *desc = descCreateFromFile("data/conv1.json");
	const Description *desc = descCreateFromFile("data/conv2.json");
	//const Description *desc = descCreateFromFile("data/conv3.json");

	if(desc != 0)
	{
		int count = computeCreateWorkflow(&device, &compute, desc);

		VkSubmitInfo graphicsSubmit;
		memset(&graphicsSubmit, 0, sizeof(VkSubmitInfo));
		graphicsSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSubmitInfo transferSubmit = graphicsSubmit;

		AIOCmdBuffer *aioCmdBuffers[2] = { aioAllocCmdBuffer(aio), aioAllocCmdBuffer(aio) };
		
		VkFence graphicsFences[2], transferFences[2], computeFences[2];
		for(int i = 0; i < 2; ++i)
		{
			createFence(&device, &compute, &graphicsFences[i]);
			createFence(&device, &compute, &transferFences[i]);
			createFence(&device, &compute, &computeFences[i]);
		}

		std::vector<double> timings;
		timings.reserve(count + 4);

		for(int i = -2; i < count + 2; ++i)
		{
			Clock start;
			clockGetTime(&start);

			int index = i + 2;
			int lsb = index & 0x1;
			
			if(rdoc_api) rdoc_api->StartFrameCapture(NULL, NULL);

			VkCommandBuffer graphicsCB[4];
			graphicsSubmit.commandBufferCount = 0;
			graphicsSubmit.pCommandBuffers = graphicsCB;

			VkCommandBuffer transferCB[4];
			transferSubmit.commandBufferCount = 0;
			transferSubmit.pCommandBuffers = transferCB;

			aioBeginCmdBuffer(aioCmdBuffers[lsb]);

			if(i == -2)
			{
				createAIOCommands(&compute, aioCmdBuffers[lsb], &compute.aioUniqueWorkload, Access_CPU_Write, 0);
			}
			
			if(i >= -2 && i < count)
			{
				createAIOCommands(&compute, aioCmdBuffers[lsb], &compute.aioWorkload, Access_CPU_Write, index);
			}
			
			if(i == count + 1)
			{
				createAIOCommands(&compute, aioCmdBuffers[lsb], &compute.aioUniqueWorkload, Access_CPU_Read, 0);
			}

			if(i >= 2 && i < count + 2)
			{
				createAIOCommands(&compute, aioCmdBuffers[lsb], &compute.aioWorkload, Access_CPU_Read, index - 4);
			}
			
			aioEndCmdBuffer(aioCmdBuffers[lsb]);

			if(i == -1)
			{
				transferCB[transferSubmit.commandBufferCount] = compute.transferUniqueCmdBuffers[0];
				++transferSubmit.commandBufferCount;
			}

			if(i == count)
			{
				transferCB[transferSubmit.commandBufferCount] = compute.transferUniqueCmdBuffers[1];
				++transferSubmit.commandBufferCount;
			}

			if(i >= -1 && i < count + 1)
			{
				transferCB[transferSubmit.commandBufferCount] = compute.transferCmdBuffers[lsb];
				++transferSubmit.commandBufferCount;
			}

			if(i >= 0 && i < count)
			{
				graphicsCB[graphicsSubmit.commandBufferCount] = compute.graphicsCmdBuffers[lsb];
				++graphicsSubmit.commandBufferCount;
			}

			// TODO check if semaphore is needed between the transfer and compute queue
			if(i > -2)
			{
				vkWaitForFences(device.device, 1, &transferFences[lsb^1], VK_TRUE, UINT64_MAX);
				vkResetFences(device.device, 1, &transferFences[lsb^1]);
			}

			vkQueueSubmit(device.graphicsQueue, 1, &graphicsSubmit, graphicsFences[lsb]);

			aioWaitIdle(aio);
			aioSubmitCmdBuffer(aio, aioCmdBuffers[lsb]);

			if(i > -2)
			{
				vkWaitForFences(device.device, 1, &graphicsFences[lsb^1], VK_TRUE, UINT64_MAX);
				vkResetFences(device.device, 1, &graphicsFences[lsb^1]);
			}

			vkQueueSubmit(device.transferQueue, 1, &transferSubmit, transferFences[lsb]);
			
			if(rdoc_api) rdoc_api->EndFrameCapture(NULL, NULL);
			
			Clock stop;
			clockGetTime(&stop);
			timings.push_back(clockDeltaTime(&start, &stop));
		}

		vkQueueWaitIdle(device.graphicsQueue);
		vkQueueWaitIdle(device.transferQueue);

		aioWaitIdle(aio);
		aioFreeCmdBuffer(aioCmdBuffers[0]);
		aioFreeCmdBuffer(aioCmdBuffers[1]);

		descDestroy(desc);

		double total = 0.0;
		for(double t : timings) { total += t; }
		double mean = total / timings.size();

		double variance = 0.0;
		for(double t : timings) { variance += (t - mean)*(t - mean); }
		variance /= timings.size() - 1;

		printf("[summary] total = %f, mean = %f, sigma = %f\n", total, mean, sqrt(variance));
	}

	computeDestroyWorkflow(&device, &compute);
	computeDestroy(&device);
	aioDestroy(aio);

	return 0;
}

