#include "Application.h"
#include <algorithm>
#include <cstring>
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <sstream>
#include "utils.h"

bool Application::createSurface() {
	if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface)) {
		return false;
	}
	return true;
}


void Application::run() {
	running = true;
	while (running) {
		SDL_Event event{ 0 };
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				int pixelWidth = 0;
				int pixelHeight = 0;
				if (SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)) {
					width = static_cast<float>(pixelWidth);
					height = static_cast<float>(pixelHeight);
					requireSwapchainRecreate = true;
				}
			}
		}
		if (running) {
			render();
		}
	}
}

bool Application::initialize() {
	timelineSemaphore = VK_NULL_HANDLE;
	swapchain = VK_NULL_HANDLE;
	requireSwapchainRecreate = false;
	depthFormat = VK_FORMAT_UNDEFINED;

	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		std::cerr << "Error initializing SDL video: " << SDL_GetError() << std::endl;
		return false;
	}

	window = SDL_CreateWindow("Vulkan Learning", static_cast<int>(width),
		static_cast<int>(height), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!window) {
		std::cerr << "Error creating window: " << SDL_GetError() << std::endl;
		return false;
	}

	return initializeVulkan();
}

bool Application::initializeVulkan() {
	if (!createVulkanInstance()) {
		std::cerr << "Could not create a Vulkan instance" << std::endl;
		return false;
	}

	if (!createSurface()) {
		std::cerr << "Could not create window surface: " << SDL_GetError() << std::endl;
		return false;
	};
	physicalDevice = findPhysicalDevice();
	if (physicalDevice == VK_NULL_HANDLE) {
		std::cerr << "Unable to find a compatible physical device" << std::endl;
		return false;
	}

	if (!chooseDepthFormat()) {
		return false;
	}

	if (!findGraphicsQueue()) {
		std::cerr << "Unable To Find a compatible Graphics Queue" << std::endl;
		return false;
	};
	if (!createDevice(physicalDevice)) {
		std::cerr << "Could not create the logical GPU device" << std::endl;
		return false;
	}
	if (!initializeVMA()){
		std::cerr << "unable to create a memory allocator" << std::endl;
		return false;
	}
	if (!createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
		std::cerr << "Unable to create swapchain" << std::endl;
		return false;
	}
	if (!createShaders()) {
		std::cerr << "Error Creating Shader Modules" << std::endl;
		return false;
	}
	if (pipeline = createGraphicsPipeline(); !pipeline) {
		std::cerr << "Unable To Initialize Graphics Queue" << std::endl;
		return false;
	}
	if (!createSyncResources()) {
		std::cerr << "Could not Create the sync related Resources" << std::endl;
		return false;
	}
	if (!createCommandBuffers()) {
		std::cerr << "Could not create a command Buffer" << std::endl;
		return false;
	}

	return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Application::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserdata) {

	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::cerr << "Validation Layer" << pCallbackData->pMessage << std::endl;
	}
	return VK_FALSE;
}

bool Application::createVulkanInstance() {
	if (volkInitialize() != VK_SUCCESS) {
		std::cerr << "Could not initialize vulkan" << std::endl;
		return false;
	};
	uint32_t loaderVersion = VK_API_VERSION_1_0;
	if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS || loaderVersion < VulkanVersion) {
		std::cerr << "Vulkan 1.3 or newer is required" << std::endl;
		return false;
	}
	VkApplicationInfo appInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "BlackHole",
		.apiVersion = VulkanVersion,
	};
	uint32_t instExtCount = 0;
	const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&instExtCount);
	if (!sdlExtensions) {
		std::cerr << "Could not get SDL Vulkan instance extensions: " << SDL_GetError() << std::endl;
		return false;
	}

	uint32_t availableExtensionCount = 0;
	if (vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr) != VK_SUCCESS) {
		std::cerr << "Could not enumerate Vulkan instance extensions" << std::endl;
		return false;
	}
	std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);
	if (vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, availableExtensions.data()) != VK_SUCCESS) {
		std::cerr << "Could not enumerate Vulkan instance extensions" << std::endl;
		return false;
	}

	std::vector<const char*> requestedExtensions(sdlExtensions, sdlExtensions + instExtCount);
	const bool debugUtilsAvailable = std::any_of(availableExtensions.begin(), availableExtensions.end(),
		[](const VkExtensionProperties& extension) {
			return std::strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
		});
	if (debugUtilsAvailable) {
		requestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	uint32_t availableLayerCount = 0;
	if (vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr) != VK_SUCCESS) {
		std::cerr << "Could not enumerate Vulkan instance layers" << std::endl;
		return false;
	}
	std::vector<VkLayerProperties> availableLayers(availableLayerCount);
	if (vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data()) != VK_SUCCESS) {
		std::cerr << "Could not enumerate Vulkan instance layers" << std::endl;
		return false;
	}
	std::vector<const char*> requestedLayers;
	if (std::any_of(availableLayers.begin(), availableLayers.end(), [](const VkLayerProperties& layer) {
		return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
	})) {
		requestedLayers.push_back("VK_LAYER_KHRONOS_validation");
	}

	VkDebugUtilsMessengerCreateInfoEXT debugInfo{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
							| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback
	};


	VkInstanceCreateInfo instCreateInfo{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = debugUtilsAvailable ? &debugInfo : nullptr,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames = requestedLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data()
	};
	if (vkCreateInstance(&instCreateInfo, nullptr, &vulkanInstance) != VK_SUCCESS) {
		std::cerr << "Could not create the Vulkan instance" << std::endl;
		return false;
	}
	volkLoadInstance(vulkanInstance);
	if (debugUtilsAvailable && vkCreateDebugUtilsMessengerEXT(vulkanInstance, &debugInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
		std::cerr << "Could not create the Vulkan debug messenger" << std::endl;
		return false;
	}
	return true;
}

void Application::shutdown()
{
	// Only wait if the logical device was created.
	if (device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(device);

		if (timelineSemaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(device, timelineSemaphore, nullptr);
			timelineSemaphore = VK_NULL_HANDLE;
		}

		for (auto& res : frameResources) {
			if (res.imageAcquiredSemaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(device, res.imageAcquiredSemaphore, nullptr);
				res.imageAcquiredSemaphore = VK_NULL_HANDLE;
			}

			if (res.commandPool != VK_NULL_HANDLE) {
				vkDestroyCommandPool(device, res.commandPool, nullptr);
				res.commandPool = VK_NULL_HANDLE;
				res.commandBuffer = VK_NULL_HANDLE;
			}
		}

		if (pipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			pipelineLayout = VK_NULL_HANDLE;
		}

		if (pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(device, pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}

		if (vertShader != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device, vertShader, nullptr);
			vertShader = VK_NULL_HANDLE;
		}

		if (fragShader != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device, fragShader, nullptr);
			fragShader = VK_NULL_HANDLE;
		}

		// Destroy swapchain and frame depth resources before the allocator and device.
		destroySwapchain();

		if (vmaAllocator != VK_NULL_HANDLE) {
			vmaDestroyAllocator(vmaAllocator);
			vmaAllocator = VK_NULL_HANDLE;
		}

		vkDestroyDevice(device, nullptr);
		device = VK_NULL_HANDLE;
	}

	// Surface must be destroyed before its instance.
	if (surface != VK_NULL_HANDLE &&
		vulkanInstance != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(vulkanInstance, surface, nullptr);
		surface = VK_NULL_HANDLE;
	}

	if (debugMessenger != VK_NULL_HANDLE && vulkanInstance != VK_NULL_HANDLE) {
		vkDestroyDebugUtilsMessengerEXT(vulkanInstance, debugMessenger, nullptr);
		debugMessenger = VK_NULL_HANDLE;
	}
	if (vulkanInstance != VK_NULL_HANDLE) {
		vkDestroyInstance(vulkanInstance, nullptr);
		vulkanInstance = VK_NULL_HANDLE;
	}

	volkFinalize();

	if (window != nullptr) {
		SDL_DestroyWindow(window);
		window = nullptr;
	}

	SDL_Quit();
}
VkPhysicalDevice Application::findPhysicalDevice()
{
	uint32_t physDeviceCount = 0;

	if (vkEnumeratePhysicalDevices(
		vulkanInstance, &physDeviceCount, nullptr) != VK_SUCCESS ||
		physDeviceCount == 0) {
		std::cerr << "No Vulkan physical devices found\n";
		return VK_NULL_HANDLE;
	}

	std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);

	if (vkEnumeratePhysicalDevices(
		vulkanInstance, &physDeviceCount,
		physicalDevices.data()) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}

	for (VkPhysicalDevice candidate : physicalDevices) {
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(candidate, &properties);
		if (properties.apiVersion < VulkanVersion) {
			continue;
		}

		VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		VkPhysicalDeviceVulkan12Features features12{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &features13
		};
		VkPhysicalDeviceFeatures2 features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features12 };
		vkGetPhysicalDeviceFeatures2(candidate, &features);
		if (!features13.dynamicRendering || !features13.synchronization2 || !features12.timelineSemaphore) {
			continue;
		}

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());
		bool hasPresentGraphicsQueue = false;
		for (uint32_t familyIndex = 0; familyIndex < queueFamilyCount; ++familyIndex) {
			VkBool32 presentSupported = VK_FALSE;
			if ((queueFamilies[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
				vkGetPhysicalDeviceSurfaceSupportKHR(candidate, familyIndex, surface, &presentSupported) == VK_SUCCESS &&
				presentSupported) {
				hasPresentGraphicsQueue = true;
				break;
			}
		}
		if (!hasPresentGraphicsQueue) {
			continue;
		}

		uint32_t deviceExtensionCount = 0;
		if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &deviceExtensionCount, nullptr) != VK_SUCCESS) {
			continue;
		}
		std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
		if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &deviceExtensionCount, deviceExtensions.data()) != VK_SUCCESS ||
			std::none_of(deviceExtensions.begin(), deviceExtensions.end(), [](const VkExtensionProperties& extension) {
				return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
			})) {
			continue;
		}

		uint32_t formatCount = 0;

		VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
			candidate, surface, &formatCount, nullptr);

		if (result != VK_SUCCESS || formatCount == 0)
			continue;

		std::vector<VkSurfaceFormatKHR> formats(formatCount);

		result = vkGetPhysicalDeviceSurfaceFormatsKHR(
			candidate, surface, &formatCount, formats.data());

		if (result != VK_SUCCESS)
			continue;

		bool formatSupported = false;

		for (const VkSurfaceFormatKHR& format : formats) {
			if (format.format == VK_FORMAT_UNDEFINED ||
				(format.format == swapchainFormat &&
					format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
				formatSupported = true;
				break;
			}
		}

		if (formatSupported) {
			std::cout << "Selected GPU: " << properties.deviceName << '\n';
			return candidate;
		}
	}

	std::cerr << "No physical device supports the requested swapchain format\n";
	return VK_NULL_HANDLE;
}

bool Application::findGraphicsQueue() {
	uint32_t queueFamCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, nullptr);
	std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, queueFamProps.data());

	for (uint32_t currentFamIdx = 0; currentFamIdx < queueFamProps.size(); ++currentFamIdx) {
		VkBool32 hasPresentSupport = VK_FALSE;
		if (vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, currentFamIdx, surface, &hasPresentSupport) != VK_SUCCESS) {
			continue;
		}
		const auto& props = queueFamProps[currentFamIdx];
		if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && hasPresentSupport) {
			gfxQueueFamIdx = currentFamIdx;
			return true;
		}
	}
	return false;
}
bool Application::createDevice(VkPhysicalDevice physicalDevice) {
	VkPhysicalDeviceVulkan13Features supportedFeatures13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	VkPhysicalDeviceVulkan12Features supportedFeatures12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13 };
	VkPhysicalDeviceFeatures2 supportedFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12 };
	vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

	if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 || !supportedFeatures12.timelineSemaphore) {
		std::cerr << "Physical device does not meet the required feature requirements" << std::endl;
		return false;
};
	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
	};
	VkPhysicalDeviceVulkan12Features features12{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &features13,
		.timelineSemaphore = VK_TRUE,
	};
	VkPhysicalDeviceFeatures2 features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12,
	};
	std::vector<float> queuePriorities{ 1.0f };
	VkDeviceQueueCreateInfo gfxQueueInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = gfxQueueFamIdx,
			.queueCount = 1,
			.pQueuePriorities = queuePriorities.data(),
	};

	const std::vector<const char* > deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkDeviceCreateInfo devCreateInfo{
	.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	.pNext = &features,
	.queueCreateInfoCount = 1,
	.pQueueCreateInfos = &gfxQueueInfo,
	.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
	.ppEnabledExtensionNames = deviceExtensions.data(),
	.pEnabledFeatures = nullptr,
	};
	if (vkCreateDevice(physicalDevice, &devCreateInfo, nullptr, &device) != VK_SUCCESS) {
		std::cerr << "Could not create the logical Vulkan device" << std::endl;
		return false;
	}
	volkLoadDevice(device);
	vkGetDeviceQueue(device, gfxQueueFamIdx, 0, &gfxQueue);
	if (!gfxQueue) {
		std::cerr << "could'nt Get the Graphics Queue" << std::endl;
		return false;
	}
	return true;
	}

bool Application::initializeVMA()
{
	VmaVulkanFunctions vmaFuncInfo{};

	vmaFuncInfo.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vmaFuncInfo.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

	vmaFuncInfo.vkGetPhysicalDeviceProperties =
		vkGetPhysicalDeviceProperties;
	vmaFuncInfo.vkGetPhysicalDeviceMemoryProperties =
		vkGetPhysicalDeviceMemoryProperties;

	vmaFuncInfo.vkAllocateMemory = vkAllocateMemory;
	vmaFuncInfo.vkFreeMemory = vkFreeMemory;
	vmaFuncInfo.vkMapMemory = vkMapMemory;
	vmaFuncInfo.vkUnmapMemory = vkUnmapMemory;
	vmaFuncInfo.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
	vmaFuncInfo.vkInvalidateMappedMemoryRanges =
		vkInvalidateMappedMemoryRanges;

	vmaFuncInfo.vkBindBufferMemory = vkBindBufferMemory;
	vmaFuncInfo.vkBindImageMemory = vkBindImageMemory;
	vmaFuncInfo.vkGetBufferMemoryRequirements =
		vkGetBufferMemoryRequirements;
	vmaFuncInfo.vkGetImageMemoryRequirements =
		vkGetImageMemoryRequirements;

	vmaFuncInfo.vkCreateBuffer = vkCreateBuffer;
	vmaFuncInfo.vkDestroyBuffer = vkDestroyBuffer;
	vmaFuncInfo.vkCreateImage = vkCreateImage;
	vmaFuncInfo.vkDestroyImage = vkDestroyImage;

	vmaFuncInfo.vkCmdCopyBuffer = vkCmdCopyBuffer;

	VmaAllocatorCreateInfo vmaAllocInfo{};
	vmaAllocInfo.physicalDevice = physicalDevice;
	vmaAllocInfo.device = device;
	vmaAllocInfo.pVulkanFunctions = &vmaFuncInfo;
	vmaAllocInfo.instance = vulkanInstance;
	vmaAllocInfo.vulkanApiVersion = VulkanVersion;

	return vmaCreateAllocator(&vmaAllocInfo, &vmaAllocator) == VK_SUCCESS;
}

bool Application::createSwapchain(uint32_t width, uint32_t height) {
	VkSurfaceCapabilitiesKHR surfaceCaps{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps) != VK_SUCCESS) {
		std::cerr << "Could not get the surface capabilities" << std::endl;
		return false;
	}
	if ((surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
		std::cerr << "The surface does not support color-attachment swapchain images" << std::endl;
		return false;
	}
	if (surfaceCaps.currentExtent.width != UINT32_MAX) {
		swapchainWidth = surfaceCaps.currentExtent.width;
		swapchainHeight = surfaceCaps.currentExtent.height;
	}
	else {
		swapchainWidth = std::clamp(width, surfaceCaps.minImageExtent.width, surfaceCaps.maxImageExtent.width);
		swapchainHeight = std::clamp(height, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height);
	}
	uint32_t requestedImageCount = std::max(2u, surfaceCaps.minImageCount);
	if (surfaceCaps.maxImageCount > 0) {
		requestedImageCount = std::min(requestedImageCount, surfaceCaps.maxImageCount);
	}
	const VkCompositeAlphaFlagBitsKHR alphaModes[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};
	VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	for (VkCompositeAlphaFlagBitsKHR mode : alphaModes) {
		if ((surfaceCaps.supportedCompositeAlpha & mode) != 0) {
			compositeAlpha = mode;
			break;
		}
	}
	if ((surfaceCaps.supportedCompositeAlpha & compositeAlpha) == 0) {
		std::cerr << "The surface has no supported composite-alpha mode" << std::endl;
		return false;
	}
	VkSwapchainCreateInfoKHR swapchainCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = requestedImageCount,
		.imageFormat = swapchainFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{ .width = swapchainWidth, .height = swapchainHeight },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = surfaceCaps.currentTransform,
		.compositeAlpha = compositeAlpha,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
	if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS) {
		std::cerr << "Error Creating SwapChain" << std::endl;
		return false;
	}
	uint32_t imageCount = 0;
	if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) != VK_SUCCESS || imageCount == 0) {
		std::cerr << "Could not retrieve swapchain images" << std::endl;
		return false;
	}
	swapchainImages.resize(imageCount);
	if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()) != VK_SUCCESS) {
		std::cerr << "Could not retrieve swapchain images" << std::endl;
		return false;
	}
	swapchainImageViews.resize(imageCount);
	for (size_t i = 0; i < swapchainImages.size(); ++i) {
		VkImageViewCreateInfo imgViewInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchainFormat,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};
		if (vkCreateImageView(device, &imgViewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
			std::cerr << "Error Creating Swapchain Image view" << std::endl;
			return false;
		}
	}

	renderCompleteSemaphores.resize(swapchainImages.size());
	for (VkSemaphore& semaphore : renderCompleteSemaphores) {
		VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
			std::cerr << "Error Creating the render-complete semaphore" << std::endl;
			return false;
		}
	}
	for (FrameResources& frame : frameResources) {
	VkImageCreateInfo depthCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat,
		.extent{ .width = swapchainWidth, .height = swapchainHeight, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocInfo{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	if (vmaCreateImage(vmaAllocator, &depthCreateInfo, &allocInfo, &frame.depthImage, &frame.depthImageAllocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Error allocating depth image" << std::endl;
		return false;
	}
	VkImageViewCreateInfo depthImgViewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = frame.depthImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = depthFormat,
		.subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
	};
	if (vkCreateImageView(device, &depthImgViewInfo, nullptr, &frame.depthImageView) != VK_SUCCESS) {
		std::cerr << "Error creating depth image view" << std::endl;
		return false;
	}
	}
	return true;
}

void Application::destroySwapchain() {
	for (VkImageView swapchainImgView : swapchainImageViews) {
		if (swapchainImgView != VK_NULL_HANDLE) {
			vkDestroyImageView(device, swapchainImgView, nullptr);
		}
	}
	swapchainImageViews.clear();

	for (VkSemaphore& semaphore : renderCompleteSemaphores) {
		if (semaphore != VK_NULL_HANDLE) {
			vkDestroySemaphore(device, semaphore, nullptr);
		}
	}
	renderCompleteSemaphores.clear();
	if (swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		swapchain = VK_NULL_HANDLE;
	}
	for (FrameResources& frame : frameResources) {
		if (frame.depthImageView != VK_NULL_HANDLE) {
			vkDestroyImageView(device, frame.depthImageView, nullptr);
			frame.depthImageView = VK_NULL_HANDLE;
		}
		if (frame.depthImage != VK_NULL_HANDLE && vmaAllocator != VK_NULL_HANDLE) {
			vmaDestroyImage(vmaAllocator, frame.depthImage, frame.depthImageAllocation);
			frame.depthImage = VK_NULL_HANDLE;
			frame.depthImageAllocation = nullptr;
		}
	}
}

bool Application::chooseDepthFormat()
{
	const std::vector<VkFormat> candidates{
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_D24_UNORM_S8_UINT,
		VK_FORMAT_D16_UNORM
	};

	for (VkFormat format : candidates) {
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(
			physicalDevice, format, &properties);

		if (properties.optimalTilingFeatures &
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			depthFormat = format;
			std::cout << "Selected depth format: "
				<< static_cast<int>(depthFormat) << '\n';
			return true;
		}
	}

	std::cerr << "No supported depth attachment format\n";
	return false;
}

VkShaderModule Application::createShaderModule(const std::string& filename, shaderc_shader_kind kind) const {
	const std::string shaderPath = "src/" + filename;
	const char* basePath = SDL_GetBasePath();
	const std::string resolvedShaderPath = basePath ? std::string(basePath) + shaderPath : shaderPath;
	std::string src = readTextFile(resolvedShaderPath);
	if (src.empty() && resolvedShaderPath != shaderPath) {
		src = readTextFile(shaderPath);
	}
	if (src.empty()) {
		std::cerr << "Could not read shader file from " << resolvedShaderPath << " or " << shaderPath << std::endl;
		return nullptr;
	}
	std::cout << "Compiling shader " << resolvedShaderPath << std::endl;
	shaderc::Compiler compiler;
	shaderc::CompileOptions opts;
	opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
	opts.SetTargetSpirv(shaderc_spirv_version_1_6);
	opts.SetOptimizationLevel(shaderc_optimization_level_performance);
	shaderc::CompilationResult result = compiler.CompileGlslToSpv(src, kind, filename.c_str(), opts);
	if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
		std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
		return nullptr;
	}
	const size_t shaderSize = ((result.cend() - result.cbegin()) * sizeof(uint32_t));
	VkShaderModuleCreateInfo moduleCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shaderSize,
		.pCode = result.cbegin()
	};
	VkShaderModule shaderModule = nullptr;
	if (vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		std::cerr << "Error Creating shader Module" << std::endl;
		return nullptr;
	}
	return shaderModule;
}

bool Application::createShaders() {
	if (vertShader = createShaderModule("vert.glsl", shaderc_vertex_shader); !vertShader) {
		return false;
	}
	if (fragShader = createShaderModule("frag.glsl", shaderc_fragment_shader); !fragShader) {
		return false;
	}
	return true;
}

VkPipeline Application::createGraphicsPipeline() {
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = 12;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
	};
	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		std::cerr << "Unable to create the pipeline layout" << std::endl;
		return nullptr;
	}

	const char* entryPoint = "main";
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertShader,
			.pName = entryPoint
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragShader,
			.pName = entryPoint
		}
	};
	VkPipelineVertexInputStateCreateInfo vertInputInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
	};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkPipelineDepthStencilStateCreateInfo depthStencilInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};
	VkPipelineViewportStateCreateInfo viewPortInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = nullptr,
		.scissorCount = 1,
		.pScissors = nullptr
	};
	VkPipelineRasterizationStateCreateInfo rasterInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo multiSampleInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	VkPipelineColorBlendAttachmentState attachState{
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};
	VkPipelineColorBlendStateCreateInfo blendInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachState,
	};
	std::vector<VkDynamicState> dynamicState{
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t> (dynamicState.size()),
		.pDynamicStates = dynamicState.data()
	};
	VkPipelineRenderingCreateInfo renderInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &swapchainFormat,
		.depthAttachmentFormat = depthFormat
	};

	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderInfo,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewPortInfo,
		.pRasterizationState = &rasterInfo,
		.pMultisampleState = &multiSampleInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &blendInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = pipelineLayout,
		.renderPass = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};
	VkPipeline newPipeline = VK_NULL_HANDLE;
	const VkResult pipelineResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline);
	if (pipelineResult != VK_SUCCESS) {
		std::cerr << "Error creating the graphics pipeline: " << pipelineResult << std::endl;
		return nullptr;
	};
	return newPipeline;
}

bool Application::createSyncResources() {
	VkSemaphoreTypeCreateInfo semaphoreTypeInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = MaxFramesInFlight
	};
	VkSemaphoreCreateInfo semaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &semaphoreTypeInfo,
	};
	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timelineSemaphore) != VK_SUCCESS) {
		std::cerr << "Unable to create timeline Semaphore" << std::endl;
		return false;
	};
	for (FrameResources& res : frameResources) {
		VkSemaphoreCreateInfo semaphoreInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
		};
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &res.imageAcquiredSemaphore) != VK_SUCCESS) {
			std::cerr << "Error creating the per-frame image-acquire Semaphore" << std::endl;
			return false;
		}
	}
	return true;
}
bool Application::createCommandBuffers() {
	for (FrameResources& res : frameResources) {
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = gfxQueueFamIdx
		};
		if (vkCreateCommandPool(device, &poolInfo, nullptr, &res.commandPool) != VK_SUCCESS) {
			std::cerr << "Unable To Create Command Pool" << std::endl;
			return false;
		}
		VkCommandBufferAllocateInfo cmdAllocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = res.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &res.commandBuffer) != VK_SUCCESS) {
			std::cerr << "Unable to allocate command buffer" << std::endl;
			return false;
		}
	}
	return true;
}

void Application::render() {
	if (requireSwapchainRecreate) {
		if (width <= 0 || height <= 0) {
			return;
		}
		if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
			std::cerr << "Could not wait for the Vulkan device before recreating the swapchain" << std::endl;
			running = false;
			return;
		}
		destroySwapchain();
		if (!createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
			running = false;
			return;
		}
		requireSwapchainRecreate = false;
	}

	const uint32_t frameResIndex = static_cast<uint32_t>(frameIndex % MaxFramesInFlight);
	const uint64_t signalValue = nextSignalValue;
	const uint64_t waitValue = signalValue - MaxFramesInFlight;

	VkSemaphoreWaitInfo waitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &timelineSemaphore,
		.pValues = &waitValue
	};
	if (vkWaitSemaphores(device, &waitInfo, UINT64_MAX) != VK_SUCCESS) {
		std::cerr << "Could not wait for the frame timeline semaphore" << std::endl;
		running = false;
		return;
	}
	FrameResources& res = frameResources[frameResIndex];
	if (vkResetCommandPool(device, res.commandPool, 0) != VK_SUCCESS) {
		std::cerr << "Could not reset the frame command pool" << std::endl;
		running = false;
		return;
	}
	VkSemaphore imageAcquireSemaphore = res.imageAcquiredSemaphore;
	uint32_t imageIndex = 0;
	VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquireSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
		requireSwapchainRecreate = true;
		return;
	}
	if (acquireResult == VK_SUBOPTIMAL_KHR) {
		requireSwapchainRecreate = true;
	}
	else if (acquireResult != VK_SUCCESS) {
		std::cerr << "Could not acquire a swapchain image: " << acquireResult << std::endl;
		running = false;
		return;
	}
	VkCommandBufferBeginInfo cmdBeginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	if (vkBeginCommandBuffer(res.commandBuffer, &cmdBeginInfo) != VK_SUCCESS) {
		std::cerr << "Could not begin recording the frame command buffer" << std::endl;
		running = false;
		return;
	}
	std::vector<VkImageMemoryBarrier2> layoutBarriers{
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = 0, //Once this activity finishes ONLY then allow the image to move on and go through the barrier
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,//The destination Stage of the where the barrier ends(The layer the image is sent to after the work is done)
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = swapchainImages[imageIndex],
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = res.depthImage,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		}
	};
	VkDependencyInfo depInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = static_cast<uint32_t>(layoutBarriers.size()),
		.pImageMemoryBarriers = layoutBarriers.data()
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &depInfo);
	VkRenderingAttachmentInfo colorAttachInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = swapchainImageViews[imageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue{ .color{ 0.01f, 0.01f, 0.01f, 1 } }
	};
	VkRenderingAttachmentInfo depthAttachInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = res.depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue{ .depthStencil{ 1.0f, 0 } }
	};
	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea{
			.offset{ .x = 0, .y = 0 },
			.extent{ .width = swapchainWidth, .height = swapchainHeight }
			},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachInfo,
			.pDepthAttachment = &depthAttachInfo,
	};
	vkCmdBeginRendering(res.commandBuffer, &renderingInfo);
	{
		VkViewport viewport{
			.x = 0, .y = 0,
			.width = static_cast<float>(swapchainWidth),
			.height = static_cast<float>(swapchainHeight)
		};
		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);
		VkRect2D scissor{
			.offset{ .x = 0, .y = 0 },
			.extent{ .width = swapchainWidth, .height = swapchainHeight }
		};
		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(res.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		struct PushConstants {
			float resolution[2];
			float time;
		};

		const Uint64 currentTime = SDL_GetPerformanceCounter();

		const double elapsedTime =static_cast<double>(currentTime - startTime) /static_cast<double>(SDL_GetPerformanceFrequency());

		PushConstants pc{};
		pc.resolution[0] = static_cast<float>(swapchainWidth);
		pc.resolution[1] = static_cast<float>(swapchainHeight);
		pc.time = elapsedTime;

		vkCmdPushConstants(
			res.commandBuffer,
			pipelineLayout, // Your VkPipelineLayout
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(PushConstants),
			&pc
		);
		vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);
	}
	vkCmdEndRendering(res.commandBuffer);

	VkImageMemoryBarrier2 presentLayoutBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapchainImages[imageIndex],
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};
	VkDependencyInfo presentDepInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &presentLayoutBarrier
	};
	vkCmdPipelineBarrier2(res.commandBuffer, &presentDepInfo);

	VkSemaphoreSubmitInfo imageAcquireWaitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = imageAcquireSemaphore,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	std::vector<VkSemaphoreSubmitInfo> semaphoreSignals{
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = renderCompleteSemaphores[imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
		},
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = timelineSemaphore,
			.value = signalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
		}
	};
	VkCommandBufferSubmitInfo cmdSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = res.commandBuffer,
	};
	VkSubmitInfo2 submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &imageAcquireWaitInfo,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignals.size()),
		.pSignalSemaphoreInfos = semaphoreSignals.data()
	};
	if (vkEndCommandBuffer(res.commandBuffer) != VK_SUCCESS) {
		std::cerr << "Could not finish recording the frame command buffer" << std::endl;
		running = false;
		return;
	}
	if (vkQueueSubmit2(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		std::cerr << "Could not submit the frame to the graphics queue" << std::endl;
		running = false;
		return;
	}
	++frameIndex;
	++nextSignalValue;

	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
		.swapchainCount = 1,
		.pSwapchains = &swapchain,
		.pImageIndices = &imageIndex,
		.pResults = nullptr
	};
	const VkResult presentResult = vkQueuePresentKHR(gfxQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
		requireSwapchainRecreate = true;
	}
	else if (presentResult != VK_SUCCESS) {
		std::cerr << "Could not present the swapchain image: " << presentResult << std::endl;
		running = false;
	}
}
