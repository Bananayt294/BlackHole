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


void Application::run()
{
	running = true;

	Uint64 previousCounter = SDL_GetPerformanceCounter();

	while (running)
	{
		Uint64 currentCounter = SDL_GetPerformanceCounter();

		float deltaTime = static_cast<float>(
			static_cast<double>(currentCounter - previousCounter) /
			static_cast<double>(SDL_GetPerformanceFrequency())
			);

		previousCounter = currentCounter;

		SDL_Event event{};

		while (SDL_PollEvent(&event))
		{
			// Forward SDL events to Dear ImGui
			ImGui_ImplSDL3_ProcessEvent(&event);

			if (event.type == SDL_EVENT_QUIT)
			{
				running = false;
				break;
			}

			// Toggle between UI mode and camera mode
			else if (event.type == SDL_EVENT_KEY_DOWN &&
				event.key.key == SDLK_TAB &&
				!event.key.repeat)
			{
				bool captureMouse = showControls;

				if (SDL_SetWindowRelativeMouseMode(
					window,
					captureMouse))
				{
					showControls = !showControls;
					mouseCaptured = captureMouse;
				}
				else
				{
					SDL_Log(
						"Mouse mode error: %s",
						SDL_GetError()
					);
				}
			}

			// Camera mouse look
			else if (event.type == SDL_EVENT_MOUSE_MOTION &&
				mouseCaptured)
			{
				camera.processMouse(
					event.motion.xrel,
					event.motion.yrel
				);
			}

			// Window resizing
			else if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{
				int pixelWidth = 0;
				int pixelHeight = 0;

				if (SDL_GetWindowSizeInPixels(
					window,
					&pixelWidth,
					&pixelHeight))
				{
					width = static_cast<float>(pixelWidth);
					height = static_cast<float>(pixelHeight);

					requireSwapchainRecreate = true;
				}
			}
		}

		if (running)
		{
			// Update camera only in camera mode
			if (mouseCaptured)
			{
				camera.update(
					SDL_GetKeyboardState(nullptr),
					deltaTime
				);
			}

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

	if (!SDL_SetWindowRelativeMouseMode(window, true))
	{
		SDL_Log("Mouse capture failed: %s", SDL_GetError());
	}

	return initializeVulkan();
}

bool Application::initializeVulkan()
{
	if (!createVulkanInstance())
	{
		std::cerr << "Could not create a Vulkan instance\n";
		return false;
	}

	if (!createSurface())
	{
		std::cerr << "Could not create window surface: "
			<< SDL_GetError() << '\n';
		return false;
	}

	physicalDevice = findPhysicalDevice();

	if (physicalDevice == VK_NULL_HANDLE)
	{
		std::cerr << "Unable to find a compatible physical device\n";
		return false;
	}

	if (!SelectHDR())
	{
		std::cerr << "HDR format is not fully supported!\n";
		return false;
	}

	if (!chooseDepthFormat())
		return false;

	if (!findGraphicsQueue())
	{
		std::cerr << "Unable to find a compatible graphics queue\n";
		return false;
	}

	if (!createDevice(physicalDevice))
	{
		std::cerr << "Could not create the logical GPU device\n";
		return false;
	}

	if (!initializeVMA())
	{
		std::cerr << "Unable to create a memory allocator\n";
		return false;
	}

	if (!createSwapchain(
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height)))
	{
		std::cerr << "Unable to create swapchain\n";
		return false;
	}

	if (!createShaders())
	{
		std::cerr << "Error creating shader modules\n";
		return false;
	}

	// Original black-hole scene pipeline.
	// It now renders into the HDR scene image.
	if (pipeline = createGraphicsPipeline(HDR_FORMAT);pipeline == VK_NULL_HANDLE){
		std::cerr << "Failed to create HDR scene pipeline\n";
		return false;
	}

	// Bright-pass extraction.
	if (!createBloomExtractPipeline(vertShader,bloomExtractFrag,bloomFormat)) {
		std::cerr << "Failed to create bloom extraction pipeline\n";
		return false;
	}

	// Gaussian blur.
	if (!createBlurPipeline(vertShader,blurFrag,bloomFormat)) {
		std::cerr << "Failed to create blur pipeline\n";
		return false;
	}

	// Final composite.
	if (!createCompositePipeline(vertShader,compositeFrag,swapchainFormat)) {
		std::cerr << "Failed to create composite pipeline\n";
		return false;
	}

	if (!createSyncResources()) {
		std::cerr << "Could not create synchronization resources\n";
		return false;
	}

	if (!createCommandBuffers()) {
		std::cerr << "Could not create command buffers\n";
		return false;
	}

	if (!InitImGui()) {
		std::cerr << "Could not initialize Dear ImGui\n";
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

		if(pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(device, pipeline, nullptr);
			std::cout << "[DESTROY] Scene pipeline: " << pipeline << '\n';
			pipeline = VK_NULL_HANDLE;
		}

		if (pipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			pipelineLayout = VK_NULL_HANDLE;
		}

		if (bloomExtractFrag != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(device, bloomExtractFrag, nullptr);
			bloomExtractFrag = VK_NULL_HANDLE;
		}

		if (blurFrag != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(device, blurFrag, nullptr);
			blurFrag = VK_NULL_HANDLE;
		}

		if (compositeFrag != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(device, compositeFrag, nullptr);
			compositeFrag = VK_NULL_HANDLE;
		}

		if (vertShader != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device, vertShader, nullptr);
			vertShader = VK_NULL_HANDLE;
		}

		if (fragShader != VK_NULL_HANDLE) {
			vkDestroyShaderModule(device, fragShader, nullptr);
			fragShader = VK_NULL_HANDLE;
		}
		destroyBloomResources();
		destroyBloomDescriptorResources();
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
	std::cout << "Frame resources count: " << frameResources.size() << std::endl;

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

	if (!createColorTarget(swapchainWidth, swapchainHeight, frame.sceneHDRImage,frame.sceneHDRAllocation,frame.sceneHDRImageView)) {
		std::cerr << "Could not create scene HDR image" << std::endl;
		return false;
	}

	if (!createColorTarget(bloomWidth, bloomHeight, frame.bloomAImage, frame.bloomAAllocation, frame.bloomAImageView)) {
		std::cerr << "Error creating Color Target" << std::endl;
		return false;
	}
	if (!createColorTarget(bloomWidth,bloomHeight,frame.bloomBImage,frame.bloomBAllocation,frame.bloomBImageView)){
		std::cerr << "Error creating Color Target" << std::endl;
		return false;
	}
	}
	if (!createBloomDescriptorResources())
	{
		std::cerr << "Failed to initialize bloom descriptors" << std::endl;
		return false;
	}
	if (!createCompositeDescriptorResources()) {
		return false;
		std::cerr << "Failed to initialize composite descriptors" << std::endl;
	}
	return true;
}

void Application::destroyBloomDescriptorResources()
{
	// Descriptor pools also free their allocated descriptor sets.
	if (bloomDescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(
			device, bloomDescriptorPool, nullptr);
		bloomDescriptorPool = VK_NULL_HANDLE;
	}

	if (compositeDescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(
			device, compositeDescriptorPool, nullptr);
		compositeDescriptorPool = VK_NULL_HANDLE;
	}

	// Pipeline layouts
	if (bloomExtractPipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(
			device, bloomExtractPipelineLayout, nullptr);
		bloomExtractPipelineLayout = VK_NULL_HANDLE;
	}

	if (blurPipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(
			device, blurPipelineLayout, nullptr);
		blurPipelineLayout = VK_NULL_HANDLE;
	}

	if (compositePipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(
			device, compositePipelineLayout, nullptr);
		compositePipelineLayout = VK_NULL_HANDLE;
	}

	// Descriptor set layouts
	if (bloomExtractSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(
			device, bloomExtractSetLayout, nullptr);
		bloomExtractSetLayout = VK_NULL_HANDLE;
	}

	if (compositeDescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(
			device, compositeDescriptorSetLayout, nullptr);
		compositeDescriptorSetLayout = VK_NULL_HANDLE;
	}

	// Sampler
	if (postProcessSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(
			device, postProcessSampler, nullptr);
		postProcessSampler = VK_NULL_HANDLE;
	}

	// Clear CPU-side descriptor handles
	for (FrameResources& frame : frameResources)
	{
		frame.bloomExtractDescriptorSet = VK_NULL_HANDLE;
		frame.compositeDescriptorSet = VK_NULL_HANDLE;
	}
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
		destroyColorTarget(frame.sceneHDRImage,frame.sceneHDRAllocation,frame.sceneHDRImageView);
		destroyColorTarget(frame.bloomAImage,frame.bloomAAllocation,frame.bloomAImageView);
		destroyColorTarget(frame.bloomBImage,frame.bloomBAllocation,frame.bloomBImageView);
	}
}

bool Application::chooseDepthFormat(){
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

bool Application::createShaders()
{
	if (vertShader = createShaderModule("vert.glsl", shaderc_vertex_shader); !vertShader) {
		return false;
	}

	if (fragShader = createShaderModule("frag.glsl", shaderc_fragment_shader); !fragShader) {
		return false;
	}

	if (bloomExtractFrag = createShaderModule("bloom_extract.frag", shaderc_fragment_shader); !bloomExtractFrag) {
		return false;
	}

	if (blurFrag = createShaderModule("blur.frag", shaderc_fragment_shader); !blurFrag){
		return false;
	}

	if (compositeFrag = createShaderModule("composite.frag", shaderc_fragment_shader); !compositeFrag){
		return false;
	}

	return true;
}

VkPipeline Application::createGraphicsPipeline(VkFormat colorFormat) {
	if (pipelineLayout == VK_NULL_HANDLE){
		VkPushConstantRange pushConstantRange{};

		pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		pushConstantRange.offset = 0;
		pushConstantRange.size = 64;

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 0,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &pushConstantRange
		};

		if (vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&pipelineLayout) != VK_SUCCESS){
			std::cerr << "Unable to create pipeline layout" << std::endl;
			return VK_NULL_HANDLE;
		}
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
		.pColorAttachmentFormats = &colorFormat,
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

void Application::render()
{
	
	// 1. Recreate swapchain if necessary
	

	if (requireSwapchainRecreate)
	{
		if (width <= 0 || height <= 0)
			return;

		if (vkDeviceWaitIdle(device) != VK_SUCCESS)
		{
			std::cerr << "Could not wait for the Vulkan device before recreating the swapchain\n";
			running = false;
			return;
		}

		destroySwapchain();

		if (!createSwapchain(
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height)))
		{
			running = false;
			return;
		}

		requireSwapchainRecreate = false;
	}

	
	// 2. Frame synchronization
	

	const uint32_t frameResIndex = static_cast<uint32_t>(frameIndex % MaxFramesInFlight);

	const uint64_t signalValue = nextSignalValue;
	const uint64_t waitValue = signalValue - MaxFramesInFlight;

	VkSemaphoreWaitInfo waitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &timelineSemaphore,
		.pValues = &waitValue
	};

	if (vkWaitSemaphores(device, &waitInfo, UINT64_MAX) != VK_SUCCESS)
	{
		std::cerr << "Could not wait for the frame timeline semaphore\n";
		running = false;
		return;
	}

	FrameResources& res = frameResources[frameResIndex];

	if (vkResetCommandPool(device, res.commandPool, 0) != VK_SUCCESS)
	{
		std::cerr << "Could not reset the frame command pool\n";
		running = false;
		return;
	}

	
	// 3. Acquire swapchain image
	

	VkSemaphore imageAcquireSemaphore = res.imageAcquiredSemaphore;
	uint32_t imageIndex = 0;

	VkResult acquireResult = vkAcquireNextImageKHR(
		device,
		swapchain,
		UINT64_MAX,
		imageAcquireSemaphore,
		VK_NULL_HANDLE,
		&imageIndex
	);

	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		requireSwapchainRecreate = true;
		return;
	}

	if (acquireResult == VK_SUBOPTIMAL_KHR)
	{
		requireSwapchainRecreate = true;
	}
	else if (acquireResult != VK_SUCCESS)
	{
		std::cerr << "Could not acquire a swapchain image: "
			<< acquireResult << '\n';

		running = false;
		return;
	}

	
	// 4. ImGui frame
	

	if (ImGui::GetCurrentContext() == nullptr)
		throw std::runtime_error("ImGui context has not been created");

	if (ImGui::GetIO().BackendRendererUserData == nullptr)
		throw std::runtime_error("ImGui Vulkan backend has not been initialized");

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();

	ImGui::NewFrame();

	buildControlUI();

	ImGui::Render();

	
	// 5. Begin command buffer
	

	VkCommandBufferBeginInfo cmdBeginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	if (vkBeginCommandBuffer(res.commandBuffer, &cmdBeginInfo) != VK_SUCCESS)
	{
		std::cerr << "Could not begin recording the frame command buffer\n";
		running = false;
		return;
	}

	
	// 6. Transition depth image and HDR scene image
	

	std::vector<VkImageMemoryBarrier2> initialBarriers{
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
				.layerCount = 1
			}
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

			.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
			.srcAccessMask = 0,

			.dstStageMask =
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,

			.dstAccessMask =
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,

			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

			.image = res.sceneHDRImage,

			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		}
	};

	VkDependencyInfo initialDependency{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount =
			static_cast<uint32_t>(initialBarriers.size()),
		.pImageMemoryBarriers = initialBarriers.data()
	};

	vkCmdPipelineBarrier2(
		res.commandBuffer,
		&initialDependency
	);

	
	// 7. Render scene into HDR image
	

	VkRenderingAttachmentInfo colorAttachInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = res.sceneHDRImageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue{
			.color{ 0.01f, 0.01f, 0.01f, 1.0f }
		}
	};

	VkRenderingAttachmentInfo depthAttachInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = res.depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue{
			.depthStencil{ 1.0f, 0 }
		}
	};

	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,

		.renderArea{
			.offset{ .x = 0, .y = 0 },
			.extent{
				.width = swapchainWidth,
				.height = swapchainHeight
			}
		},

		.layerCount = 1,

		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachInfo,

		.pDepthAttachment = &depthAttachInfo
	};

	vkCmdBeginRendering(res.commandBuffer, &renderingInfo);

	{
		VkViewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(swapchainWidth),
			.height = static_cast<float>(swapchainHeight),
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		vkCmdSetViewport(res.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor{
			.offset{ .x = 0, .y = 0 },
			.extent{
				.width = swapchainWidth,
				.height = swapchainHeight
			}
		};

		vkCmdSetScissor(res.commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(
			res.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline
		);

		struct PushConstants
		{
			float resolution[2]{};
			float time{};
			float padding{};
			float cameraPositionYaw[4]{};
			float cameraPitchFov[4]{};
			float sceneControls[4]{};
		};

		static_assert(sizeof(PushConstants) == 64);

		const Uint64 currentTime = SDL_GetPerformanceCounter();

		const double elapsedTime =
			static_cast<double>(currentTime - startTime) /
			static_cast<double>(SDL_GetPerformanceFrequency());

		PushConstants pc{};

		pc.resolution[0] = static_cast<float>(swapchainWidth);
		pc.resolution[1] = static_cast<float>(swapchainHeight);
		pc.time = static_cast<float>(elapsedTime);

		pc.cameraPositionYaw[0] = camera.x;
		pc.cameraPositionYaw[1] = camera.y;
		pc.cameraPositionYaw[2] = camera.z;
		pc.cameraPositionYaw[3] = camera.yaw;

		pc.cameraPitchFov[0] = camera.pitch;
		pc.cameraPitchFov[1] = camera.fovDegrees;

		pc.sceneControls[0] = sceneScale;
		pc.sceneControls[1] = diskBrightness;
		pc.sceneControls[2] = hazeStrength;
		pc.sceneControls[3] = diskTiltDegrees;

		vkCmdPushConstants(
			res.commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(PushConstants),
			&pc
		);

		vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);
	}

	vkCmdEndRendering(res.commandBuffer);

	
	// 8. Transition HDR scene image to shader-readable
	

	VkImageMemoryBarrier2 sceneToSampleBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,

		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,

		.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,

		.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,

		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,

		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

		.image = res.sceneHDRImage,

		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkDependencyInfo sceneDependency{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &sceneToSampleBarrier
	};

	vkCmdPipelineBarrier2(
		res.commandBuffer,
		&sceneDependency
	);

	
	// 9. Transition bloomAImage to color attachment
	

	VkImageMemoryBarrier2 bloomAToAttachment{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

		.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
		.srcAccessMask = 0,

		.dstStageMask =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,

		.dstAccessMask =
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,

		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

		.image = res.bloomAImage,

		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkDependencyInfo bloomABeginDependency{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &bloomAToAttachment
	};

	vkCmdPipelineBarrier2(
		res.commandBuffer,
		&bloomABeginDependency
	);

	
	// 10. Bloom extraction rendering attachment
	

	VkRenderingAttachmentInfo bloomColorAttachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,

		.imageView = res.bloomAImageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,

		.clearValue{
			.color{ 0.0f, 0.0f, 0.0f, 1.0f }
		}
	};

	VkRenderingInfo bloomRenderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,

		.renderArea{
			.offset{ .x = 0, .y = 0 },
			.extent{
				.width = bloomWidth,
				.height = bloomHeight
			}
		},

		.layerCount = 1,

		.colorAttachmentCount = 1,
		.pColorAttachments = &bloomColorAttachment
	};

	
	// 11. Execute bloom extraction shader
	

	vkCmdBeginRendering(
		res.commandBuffer,
		&bloomRenderingInfo
	);

	{
		VkViewport bloomViewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(bloomWidth),
			.height = static_cast<float>(bloomHeight),
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		};

		VkRect2D bloomScissor{
			.offset{ .x = 0, .y = 0 },
			.extent{
				.width = bloomWidth,
				.height = bloomHeight
			}
		};

		vkCmdSetViewport(
			res.commandBuffer,
			0,
			1,
			&bloomViewport
		);

		vkCmdSetScissor(
			res.commandBuffer,
			0,
			1,
			&bloomScissor
		);

		vkCmdBindPipeline(
			res.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			bloomExtractPipeline
		);

		vkCmdBindDescriptorSets(
			res.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			bloomExtractPipelineLayout,
			0,
			1,
			&res.bloomExtractDescriptorSet,
			0,
			nullptr
		);

		vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);
	}

	vkCmdEndRendering(res.commandBuffer);

	
	// 12. Transition bloomAImage to shader-readable
	

	VkImageMemoryBarrier2 bloomAToSample{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,

		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,

		.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,

		.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,

		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,

		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

		.image = res.bloomAImage,

		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkDependencyInfo bloomAEndDependency{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &bloomAToSample
	};

	vkCmdPipelineBarrier2(
		res.commandBuffer,
		&bloomAEndDependency
	);

	
	// 13. Transition acquired swapchain image to color attachment
	

	VkImageMemoryBarrier2 swapchainToAttachment{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

		.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
		.srcAccessMask = 0,

		.dstStageMask =
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,

		.dstAccessMask =
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,

		// Discard the previous presentation contents.
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
	};

	VkDependencyInfo swapchainBeginDependency{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &swapchainToAttachment
	};

	vkCmdPipelineBarrier2(
		res.commandBuffer,
		&swapchainBeginDependency
	);


	// 14. Temporary swapchain clear + ImGui
	

	VkRenderingAttachmentInfo swapchainAttachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,

		.imageView = swapchainImageViews[imageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,

		.clearValue{
			.color{ 0.0f, 0.0f, 0.0f, 1.0f }
		}
	};

	VkRenderingInfo swapchainRenderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,

		.renderArea{
			.offset{ .x = 0, .y = 0 },
			.extent{
				.width = swapchainWidth,
				.height = swapchainHeight
			}
		},

		.layerCount = 1,

		.colorAttachmentCount = 1,
		.pColorAttachments = &swapchainAttachment
	};

	// Begin final swapchain rendering pass
	vkCmdBeginRendering(
		res.commandBuffer,
		&swapchainRenderingInfo
	);

	// Set viewport
	VkViewport compositeViewport{
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(swapchainWidth),
		.height = static_cast<float>(swapchainHeight),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	vkCmdSetViewport(
		res.commandBuffer,
		0,
		1,
		&compositeViewport
	);

	// Set scissor
	VkRect2D compositeScissor{
		.offset = { 0, 0 },
		.extent = { swapchainWidth, swapchainHeight }
	};

	vkCmdSetScissor(
		res.commandBuffer,
		0,
		1,
		&compositeScissor
	);

	// Bind composite pipeline
	vkCmdBindPipeline(
		res.commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		compositePipeline
	);

	// Bind scene + bloom descriptor set
	vkCmdBindDescriptorSets(
		res.commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		compositePipelineLayout,
		0,
		1,
		&res.compositeDescriptorSet,
		0,
		nullptr
	);

	// Draw fullscreen triangle
	vkCmdDraw(res.commandBuffer, 3, 1, 0, 0);

	// Render ImGui on top of the composite
	ImGui_ImplVulkan_RenderDrawData(
		ImGui::GetDrawData(),
		res.commandBuffer
	);

	// End final rendering pass
	vkCmdEndRendering(res.commandBuffer);

	// 15. Transition swapchain image to presentation 

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
			.layerCount = 1
		}
	};

	VkDependencyInfo presentDepInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &presentLayoutBarrier
	};

	vkCmdPipelineBarrier2(
		res.commandBuffer,
		&presentDepInfo
	);

	
	// 16. Submit command buffer
	

	VkSemaphoreSubmitInfo imageAcquireWaitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = imageAcquireSemaphore,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
	};

	std::vector<VkSemaphoreSubmitInfo> semaphoreSignals{
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = renderCompleteSemaphores[imageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
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
		.commandBuffer = res.commandBuffer
	};

	VkSubmitInfo2 submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,

		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &imageAcquireWaitInfo,

		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,

		.signalSemaphoreInfoCount =
			static_cast<uint32_t>(semaphoreSignals.size()),

		.pSignalSemaphoreInfos = semaphoreSignals.data()
	};

	if (vkEndCommandBuffer(res.commandBuffer) != VK_SUCCESS)
	{
		std::cerr << "Could not finish recording the frame command buffer\n";
		running = false;
		return;
	}

	if (vkQueueSubmit2(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
	{
		std::cerr << "Could not submit the frame to the graphics queue\n";
		running = false;
		return;
	}

	++frameIndex;
	++nextSignalValue;

	
	// 17. Present
	

	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderCompleteSemaphores[imageIndex],

		.swapchainCount = 1,
		.pSwapchains = &swapchain,

		.pImageIndices = &imageIndex,
		.pResults = nullptr
	};

	const VkResult presentResult =
		vkQueuePresentKHR(gfxQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
		presentResult == VK_SUBOPTIMAL_KHR)
	{
		requireSwapchainRecreate = true;
	}
	else if (presentResult != VK_SUCCESS)
	{
		std::cerr << "Could not present the swapchain image: "
			<< presentResult << '\n';

		running = false;
	}
}


bool Application::InitImGui(){
		IMGUI_CHECKVERSION();

		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 6.0f;
		style.FrameRounding = 4.0f;
		style.GrabRounding = 4.0f;

		if (!ImGui_ImplSDL3_InitForVulkan(window))
		{
			throw std::runtime_error(
				"Failed to in*itialize ImGui SDL3 backend"
			);
			return false;
		}

		ImGui_ImplVulkan_InitInfo init{};

		init.ApiVersion = VK_API_VERSION_1_4;
		init.Instance = vulkanInstance;
		init.PhysicalDevice = physicalDevice;
		init.Device = device;
		init.QueueFamily = gfxQueueFamIdx;
		init.Queue = gfxQueue;

		init.DescriptorPoolSize = 64;

		init.MinImageCount = 2;
		init.ImageCount =
			static_cast<uint32_t>(swapchainImages.size());

		init.UseDynamicRendering = true;

		init.PipelineInfoMain.MSAASamples =
			VK_SAMPLE_COUNT_1_BIT;

		auto& renderingInfo =
			init.PipelineInfoMain.PipelineRenderingCreateInfo;

		renderingInfo.sType =
			VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &swapchainFormat;

		renderingInfo.depthAttachmentFormat =
			VK_FORMAT_UNDEFINED;

		renderingInfo.stencilAttachmentFormat =
			VK_FORMAT_UNDEFINED;

		if (!ImGui_ImplVulkan_Init(&init))
		{
			ImGui_ImplSDL3_Shutdown();

			throw std::runtime_error(
				"Failed to initialize ImGui Vulkan backend"
			);
			return false;
		}

		std::cout << "ImGui initialized successfully!" << std::endl;
		return true;
	}



void Application::buildControlUI()
{
	if (!showControls)
		return;

	ImGui::SetNextWindowSize(
		ImVec2(320.0f, 0.0f),
		ImGuiCond_FirstUseEver
	);

	if (ImGui::Begin("Gargantua Controls"))
	{
		ImGuiIO& io = ImGui::GetIO();

		ImGui::Text(
			"Mouse Position: %.1f, %.1f",
			io.MousePos.x,
			io.MousePos.y
		);

		ImGui::Text(
			"Left Click: %s",
			ImGui::IsMouseDown(ImGuiMouseButton_Left)
			? "PRESSED"
			: "RELEASED"
		);

		ImGui::Separator();

		ImGui::Text("BLACK HOLE SIMULATION");
		ImGui::Separator();

		ImGui::Text("Scene");

		ImGui::SliderFloat(
			"Scene Scale",
			&sceneScale,
			0.5f,
			2.5f,
			"%.2fx"
		);

		ImGui::Spacing();
		ImGui::Text("Accretion Disk");

		ImGui::SliderFloat(
			"Brightness",
			&diskBrightness,
			0.0f,
			6.0f,
			"%.2f"
		);

		ImGui::SliderFloat(
			"Disk Tilt",
			&diskTiltDegrees,
			0.0f,
			35.0f,
			"%.1f deg"
		);

		ImGui::Spacing();
		ImGui::Text("Atmosphere");

		ImGui::SliderFloat(
			"Haze Intensity",
			&hazeStrength,
			0.0f,
			3.0f,
			"%.2fx"
		);

		ImGui::Spacing();
		ImGui::Text("Camera");

		ImGui::SliderFloat(
			"Field Of View",
			&camera.fovDegrees,
			30.0f,
			90.0f,
			"%.1f deg"
		);

		if (ImGui::Button("Reset Settings"))
		{
			sceneScale = 1.0f;
			diskBrightness = 2.5f;
			hazeStrength = 1.0f;
			diskTiltDegrees = 15.0f;
			camera.fovDegrees = 55.0f;
		}

		ImGui::Separator();

		ImGui::Text("Press TAB to toggle this panel.");
	}

	ImGui::End();
}

bool Application::SelectHDR() {
	VkFormatProperties formatProperties{};

	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &formatProperties);
	VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures) {
		return false;
	}
	return true;
}

bool Application::createColorTarget(
	uint32_t targetWidth,
	uint32_t targetHeight,
	VkImage& image,
	VmaAllocation& allocation,
	VkImageView& imageView)
{
	constexpr VkFormat HDR_FORMAT =
		VK_FORMAT_R16G16B16A16_SFLOAT;

	if (targetWidth == 0 || targetHeight == 0)
		return false;

	image = VK_NULL_HANDLE;
	allocation = nullptr;
	imageView = VK_NULL_HANDLE;

	VkImageCreateInfo imageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = HDR_FORMAT,
		.extent{
			.width = targetWidth,
			.height = targetHeight,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocInfo{};

	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.requiredFlags =
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkResult result = vmaCreateImage(
		vmaAllocator,
		&imageCreateInfo,
		&allocInfo,
		&image,
		&allocation,
		nullptr
	);

	if (result != VK_SUCCESS)
	{
		std::cerr << "Could not create color target image: "
			<< result << std::endl;
		return false;
	}

	VkImageViewCreateInfo viewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = HDR_FORMAT,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	result = vkCreateImageView(device,&viewInfo,nullptr,&imageView);

	if (result != VK_SUCCESS){
		std::cerr << "Could not create color target image view: " << result << std::endl;

		vmaDestroyImage(vmaAllocator,image,allocation);
		image = VK_NULL_HANDLE;
		allocation = nullptr;

		return false;
	}
return true;
}

void Application::destroyColorTarget(VkImage& image,VmaAllocation& allocation,VkImageView& imageView) {
	if (imageView != VK_NULL_HANDLE) {
		vkDestroyImageView(device, imageView, nullptr);
		imageView = VK_NULL_HANDLE;
	}

	if (image != VK_NULL_HANDLE && allocation != nullptr &&vmaAllocator != VK_NULL_HANDLE) {
		vmaDestroyImage(vmaAllocator,image,allocation);

		image = VK_NULL_HANDLE;
		allocation = nullptr;
	}
}


bool Application::createBloomDescriptorResources()
{
	if (frameResources.empty())
	{
		std::cerr << "Frame resources are not initialized\n";
		return false;
	}

	const uint32_t frameCount = static_cast<uint32_t>(frameResources.size());

	if (bloomExtractSetLayout == VK_NULL_HANDLE)
	{
		VkDescriptorSetLayoutBinding imageBinding{};

		imageBinding.binding = 0;
		imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		imageBinding.descriptorCount = 1;
		imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};

		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &imageBinding;

		VkResult result = vkCreateDescriptorSetLayout(
			device,
			&layoutInfo,
			nullptr,
			&bloomExtractSetLayout
		);

		if (result != VK_SUCCESS)
		{
			std::cerr << "Descriptor set layout failed: " << result << '\n';
			return false;
		}
	}

	if (postProcessSampler == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo samplerInfo{};

		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		VkResult result = vkCreateSampler(
			device,
			&samplerInfo,
			nullptr,
			&postProcessSampler
		);

		if (result != VK_SUCCESS) {
			std::cerr << "Sampler creation failed: " << result << std::endl;
			return false;
		}
	}

	if (bloomDescriptorPool == VK_NULL_HANDLE)
	{
		VkDescriptorPoolSize poolSize{};

		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = frameCount;

		VkDescriptorPoolCreateInfo poolInfo{};

		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = frameCount;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;

		VkResult result = vkCreateDescriptorPool(device,&poolInfo,nullptr,&bloomDescriptorPool);

		if (result != VK_SUCCESS)
		{
			std::cerr << "Descriptor pool creation failed: "<< result << std::endl;
			return false;
		}
	}

	std::vector<uint32_t> missingFrames;

	for (uint32_t i = 0; i < frameCount; ++i){
		if (frameResources[i].bloomExtractDescriptorSet == VK_NULL_HANDLE){
			missingFrames.push_back(i);
		}
	}

	if (!missingFrames.empty()) {
		std::vector<VkDescriptorSetLayout> layouts(missingFrames.size(),bloomExtractSetLayout);

		std::vector<VkDescriptorSet> sets(missingFrames.size(),VK_NULL_HANDLE);

		VkDescriptorSetAllocateInfo allocInfo{};

		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = bloomDescriptorPool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		allocInfo.pSetLayouts = layouts.data();

		VkResult result = vkAllocateDescriptorSets(device,&allocInfo,sets.data());

		if (result != VK_SUCCESS) {
			std::cerr << "Descriptor set allocation failed: " << result << std::endl;
			return false;
		}

		for (size_t i = 0; i < missingFrames.size(); ++i) {
			frameResources[missingFrames[i]].bloomExtractDescriptorSet = sets[i];
		}
	}

	for (FrameResources& frame : frameResources){
		if (frame.sceneHDRImageView == VK_NULL_HANDLE) {
			std::cerr << "Scene HDR image view is null\n";
			return false;
		}

		VkDescriptorImageInfo imageInfo{};

		imageInfo.sampler = postProcessSampler;
		imageInfo.imageView = frame.sceneHDRImageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};

		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = frame.bloomExtractDescriptorSet;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device,1,&write,0,nullptr);
	}

	if (bloomExtractPipelineLayout == VK_NULL_HANDLE) {
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};

		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &bloomExtractSetLayout;

		VkResult result = vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&bloomExtractPipelineLayout);

		if (result != VK_SUCCESS) {
			std::cerr << "Bloom pipeline layout creation failed: " << result << '\n';
			return false;
		}
	}

	std::cout << "Bloom descriptor resources initialized successfully\n";

	return true;
}

bool Application::createCompositeDescriptorResources() {
	const uint32_t frameCount = static_cast<uint32_t>(frameResources.size());

	if (frameCount == 0)
		return false;

	if (postProcessSampler == VK_NULL_HANDLE)
	{
		std::cerr << "Post-process sampler has not been created.\n";
		return false;
	}

	if (compositeDescriptorSetLayout == VK_NULL_HANDLE)
	{
		std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

		for (uint32_t i = 0; i < bindings.size(); ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(
			device,
			&layoutInfo,
			nullptr,
			&compositeDescriptorSetLayout
		) != VK_SUCCESS)
		{
			std::cerr << "Failed to create composite descriptor set layout.\n";
			return false;
		}
	}

	
	if (compositeDescriptorPool == VK_NULL_HANDLE)
	{
		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = frameCount * 2;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = frameCount;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;

		if (vkCreateDescriptorPool(
			device,
			&poolInfo,
			nullptr,
			&compositeDescriptorPool
		) != VK_SUCCESS)
		{
			std::cerr << "Failed to create composite descriptor pool.\n";
			return false;
		}
	}


	bool setsAllocated = true;

	for (const FrameResources& frame : frameResources)
	{
		if (frame.compositeDescriptorSet == VK_NULL_HANDLE)
		{
			setsAllocated = false;
			break;
		}
	}

	if (!setsAllocated)
	{
		std::vector<VkDescriptorSetLayout> layouts(
			frameCount,
			compositeDescriptorSetLayout
		);

		std::vector<VkDescriptorSet> descriptorSets(frameCount);

		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = compositeDescriptorPool;
		allocateInfo.descriptorSetCount = frameCount;
		allocateInfo.pSetLayouts = layouts.data();

		if (vkAllocateDescriptorSets(
			device,
			&allocateInfo,
			descriptorSets.data()
		) != VK_SUCCESS)
		{
			std::cerr << "Failed to allocate composite descriptor sets.\n";
			return false;
		}

		for (uint32_t i = 0; i < frameCount; ++i)
		{
			frameResources[i].compositeDescriptorSet =
				descriptorSets[i];
		}
	}


	for (FrameResources& frame : frameResources)
	{
		VkDescriptorImageInfo sceneInfo{};
		sceneInfo.sampler = postProcessSampler;
		sceneInfo.imageView = frame.sceneHDRImageView;
		sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo bloomInfo{};
		bloomInfo.sampler = postProcessSampler;

		bloomInfo.imageView = frame.bloomAImageView;

		bloomInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::array<VkWriteDescriptorSet, 2> writes{};

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = frame.compositeDescriptorSet;
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &sceneInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = frame.compositeDescriptorSet;
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &bloomInfo;

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(writes.size()),
			writes.data(),
			0,
			nullptr
		);
	}
	
	// 5. Create composite pipeline layout
	if (compositePipelineLayout == VK_NULL_HANDLE)
	{
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts =
			&compositeDescriptorSetLayout;

		if (vkCreatePipelineLayout(
			device,
			&pipelineLayoutInfo,
			nullptr,
			&compositePipelineLayout
		) != VK_SUCCESS)
		{
			std::cerr << "Failed to create composite pipeline layout.\n";
			return false;
		}
	}

	return true;
}

bool Application::createCompositePipeline(VkShaderModule vertexShader,VkShaderModule fragmentShader,VkFormat colorFormat) {
	if (compositePipelineLayout == VK_NULL_HANDLE ||
		vertexShader == VK_NULL_HANDLE ||
		fragmentShader == VK_NULL_HANDLE)
	{
		std::cerr << "Composite pipeline dependencies are missing.\n";
		return false;
	}

	VkPipelineShaderStageCreateInfo shaderStages[2]{};

	shaderStages[0].sType =
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].module = vertexShader;
	shaderStages[0].pName = "main";

	shaderStages[1].sType =
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].module = fragmentShader;
	shaderStages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples =
		VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorAttachment{};
	colorAttachment.blendEnable = VK_FALSE;
	colorAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorAttachment;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &colorFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType =
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = compositePipelineLayout;
	pipelineInfo.renderPass = VK_NULL_HANDLE;

	VkResult result = vkCreateGraphicsPipelines(
		device,
		VK_NULL_HANDLE,
		1,
		&pipelineInfo,
		nullptr,
		&compositePipeline
	);

	if (result != VK_SUCCESS)
	{
		std::cerr << "Failed to create composite pipeline: "
			<< result << '\n';
		return false;
	}
	std::cout << "[PIPELINE CREATED] " << "compositePipeline" << '\n';
	return true;
}

static VkResult createFullscreenPipeline(
	VkDevice device,
	VkPipelineLayout pipelineLayout,
	VkShaderModule vertexShader,
	VkShaderModule fragmentShader,
	VkFormat colorFormat,
	VkPipeline* outputPipeline)
{
	VkPipelineShaderStageCreateInfo shaderStages[2]{};

	shaderStages[0].sType =
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].module = vertexShader;
	shaderStages[0].pName = "main";

	shaderStages[1].sType =
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].module = fragmentShader;
	shaderStages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorAttachment{};
	colorAttachment.blendEnable = VK_FALSE;
	colorAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorAttachment;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &colorFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType =
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = VK_NULL_HANDLE;

	VkResult result = vkCreateGraphicsPipelines(
		device,
		VK_NULL_HANDLE,
		1,
		&pipelineInfo,
		nullptr,
		outputPipeline
	);
	if(result == VK_SUCCESS) {
		std::cout << "[CREATE] Fullscreen pipeline: " << *outputPipeline << '\n';
	}
	return result;
}

bool Application::createBloomExtractPipeline(
	VkShaderModule vertexShader,
	VkShaderModule fragmentShader,
	VkFormat bloomFormat)
{
	if (bloomExtractPipelineLayout == VK_NULL_HANDLE)
	{
		std::cerr << "Bloom extraction pipeline layout is missing.\n";
		return false;
	}

	VkResult result = createFullscreenPipeline(
		device,
		bloomExtractPipelineLayout,
		vertexShader,
		fragmentShader,
		bloomFormat,
		&bloomExtractPipeline
	);

	if (result != VK_SUCCESS)
	{
		std::cerr << "Failed to create bloom extraction pipeline: " << result << std::endl;
		return false;
	}

	return true;
}

bool Application::createBlurPipeline(
	VkShaderModule vertexShader,
	VkShaderModule fragmentShader,
	VkFormat bloomFormat)
{
	if (bloomExtractPipelineLayout == VK_NULL_HANDLE)
	{
		std::cerr << "Bloom extraction pipeline layout is missing.\n";
		return false;
	}
	if (blurPipelineLayout == VK_NULL_HANDLE)
	{
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(BlurPushConstants);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &bloomExtractSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstantRange;
		VkResult result = vkCreatePipelineLayout(
			device,
			&layoutInfo,
			nullptr,
			&blurPipelineLayout
		);
		if (result != VK_SUCCESS)
		{
			std::cerr << "Failed to create blur pipeline layout: " << result << std::endl;
			return false;
		}
	}
	VkResult result = createFullscreenPipeline(
		device,
		blurPipelineLayout,
		vertexShader,
		fragmentShader,
		bloomFormat,
		&blurPipeline
	);
	return result == VK_SUCCESS;
}

void Application::destroyBloomResources()
{
	if (bloomExtractPipeline != VK_NULL_HANDLE)
	{
		std::cout << "[DESTROY] Bloom extraction: "
			<< bloomExtractPipeline << '\n';

		vkDestroyPipeline(device, bloomExtractPipeline, nullptr);
		bloomExtractPipeline = VK_NULL_HANDLE;
	}

	if (blurPipeline != VK_NULL_HANDLE)
	{
		std::cout << "[DESTROY] Blur: "
			<< blurPipeline << '\n';

		vkDestroyPipeline(device, blurPipeline, nullptr);
		blurPipeline = VK_NULL_HANDLE;
	}

	if (compositePipeline != VK_NULL_HANDLE)
	{
		std::cout << "[DESTROY] Composite: "
			<< compositePipeline << '\n';

		vkDestroyPipeline(device, compositePipeline, nullptr);
		compositePipeline = VK_NULL_HANDLE;
	}
}