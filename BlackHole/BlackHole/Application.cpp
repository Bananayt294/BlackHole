#include "Application.h"

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
				width = event.window.data1;
				height = event.window.data2;
			}
		}
	}
}

bool Application::initialize(){
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) == true) {
		window = SDL_CreateWindow("Vulkan Learning", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
		if (!window) {
			std::cerr << "Error Creating Window" << std::endl;
			return false;
		}
		if (!initializeVulkan()) {
			return false;
		}
	}
}

bool Application::initializeVulkan() {
	if (!createVulkanInstance()) {
		std::cerr << "Could not create a vulkan Instance";
		return 1;
	};

	if (!createSurface()) {
		std::cerr << "Could not Create window Surface" << std::endl;
		return false;
	};
	if (physicalDevice = findPhysicalDevice(); !physicalDevice) {
		std::cerr << "Unable To Find An Appropriate Physical Device" << std::endl;
		return false;
	};
	if (!findGraphicsQueue()) {
		std::cerr << "Unable To Find a compatible Graphics Queue" << std::endl;
		return false;
	};
	if (!createDevice(physicalDevice)) {
		std::cerr << "Couldn't Create the logical GPU device";
		return false;
	}
	if (!initializeVMA()){
		std::cerr << "unable to create a memory allocator" << std::endl;
		return false;
	}
	if (!createSwapchain(width, height)) {
		std::cerr << "Unable to create swapchain" << std::endl;
		return false;
	}
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
	VkApplicationInfo appInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "My First Triangle",
		.apiVersion = VulkanVersion,
	};
	uint32_t instExtCount = 0;
	const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&instExtCount);
	std::vector<const char*> requestedExtensions{
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
	};
	for (int i = 0; i < instExtCount; ++i) {
		requestedExtensions.push_back(extensions[i]);
	};

	std::vector<const char*> requestedLayers{ "VK_LAYER_KHRONOS_validation" };
	VkDebugUtilsMessengerCreateInfoEXT debugInfo{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
							| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback
	};


	VkInstanceCreateInfo instCreateInfo{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &debugInfo,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames = requestedLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data()
	};
	if (vkCreateInstance(&instCreateInfo, nullptr, &vulkanInstance) != VK_SUCCESS) {
		return false;
	}
	volkLoadInstance(vulkanInstance);
	return true;
}

void Application::shutdown() {
	if (vmaAllocator) {
		vmaDestroyAllocator(vmaAllocator);
	}
	if (surface) {
		vkDestroySurfaceKHR(vulkanInstance, surface, nullptr);
	};
	if (device) {
		vkDestroyDevice(device, nullptr);
	};
	if (vulkanInstance) {
		vkDestroyInstance(vulkanInstance, nullptr);
	}
	volkFinalize();
	if (window) {
		SDL_DestroyWindow(window);
	}
	SDL_Quit();
}

VkPhysicalDevice Application::findPhysicalDevice() {
	uint32_t physDeviceCount = 0;
	vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, nullptr);
	std::vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
	vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, physicalDevices.data());
	VkPhysicalDevice physicalDevice = nullptr;
	if (physDeviceCount) {
		physicalDevice = physicalDevices[0];
		for (auto& pDev : physicalDevices) {
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(pDev, &props);
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				physicalDevice = pDev;
				break;
			}
		}
	};
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());
	bool formatSupported = false;
	for (const VkSurfaceFormatKHR& surfFormat : surfaceFormats) {
		if (surfFormat.format == swapchainFormat) {
			formatSupported = true;
			break;
		};
	};
	if (!formatSupported) {
		std::cerr << "Requested swapchain format is not supported by the surface" << std::endl;
		return nullptr;
	}
	return physicalDevice;
}

bool Application::findGraphicsQueue() {
	uint32_t queueFamCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, nullptr);
	std::vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, queueFamProps.data());

	for (int currentFamIdx = 0; currentFamIdx < queueFamProps.size(); currentFamIdx++) {
		VkBool32 hasPresentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, currentFamIdx, surface, &hasPresentSupport);
		const auto& props = queueFamProps[currentFamIdx];
		if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && hasPresentSupport) {
			gfxQueueFamIdx = currentFamIdx;
			return true;
		}
	}
	return false;
}
bool Application::createDevice(VkPhysicalDevice physicalDevice) {
	VkPhysicalDeviceVulkan14Features supportedFeatures14{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr };
	VkPhysicalDeviceVulkan13Features supportedFeatures13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supportedFeatures14 };
	VkPhysicalDeviceVulkan12Features supportedFeatures12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13 };
	VkPhysicalDeviceFeatures2 supportedFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12 };
	vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

	if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 || !supportedFeatures12.timelineSemaphore) {
		std::cerr << "Physical Device dosent Meet the feature requirements" << std::endl;
		return false;
};
	VkPhysicalDeviceVulkan14Features features14{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr,
	};
	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &features14,
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
		.pEnabledFeatures = nullptr,
	};
	if (vkCreateDevice(physicalDevice, &devCreateInfo, nullptr, &device) != VK_SUCCESS) {
		return false;
	}
	vkGetDeviceQueue(device, gfxQueueFamIdx, 0, &gfxQueue);
	if (!gfxQueue) {
		std::cerr << "could'nt Get the Graphics Queue" << std::endl;
		return false;
	}
	return true;
	}

bool Application::initializeVMA() {
	VmaVulkanFunctions vmaFuncInfo{};
	VmaAllocatorCreateInfo vmaAllocInfo{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = physicalDevice,
		.device = device,
		.pVulkanFunctions = &vmaFuncInfo,
		.instance = vulkanInstance,
		.vulkanApiVersion = VulkanVersion
	};
	vmaImportVulkanFunctionsFromVolk(&vmaAllocInfo, &vmaFuncInfo);
	if (vmaCreateAllocator(&vmaAllocInfo, &vmaAllocator) != VK_SUCCESS) {
		return false;
	}
	return true;
}

bool Application::createSwapchain(uint32_t width, uint32_t height) {
	swapchainWidth = width;
	swapchainHeight = height;
	VkSurfaceCapabilitiesKHR surfaceCaps{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps) != VK_SUCCESS) {
		std::cerr << "could not get the surface capabilities" << std::endl;
		return false;
	}
	uint32_t requestedImageCount = std::max(2u, surfaceCaps.minImageCount);
	if (surfaceCaps.maxImageCount > 0) {
		requestedImageCount = std::min(requestedImageCount, surfaceCaps.maxImageCount);
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
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
	if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS) {
		std::cerr << "Error Creating SwapChain" << std::endl;
		return false;
	}
	uint32_t imageCount = 0;
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
	swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
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
	VkImageCreateInfo depthCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat,
		.extent{ .width = swapchainWidth, .height = swapchainHeight, .depth = 1 };
	}
}