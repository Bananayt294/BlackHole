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
		for (auto &pDev : physicalDevices) {
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(pDev, &props);
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				physicalDevice = pDev;
				break;
			}
		}
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
	}
}