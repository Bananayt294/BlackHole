#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <volk/volk.h>
#include <string>
#include <iostream>
struct SDL_Window;

//default configs
struct Application {

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData
		);

	VkInstance vulkanInstance{ nullptr };
	constexpr static uint32_t VulkanVersion{ VK_API_VERSION_1_4 };
	bool running{ false };
	float width = 1280;
	float height = 720;
	VkPhysicalDevice physicalDevice = nullptr;
	SDL_Window* window = nullptr;
	VkSurfaceKHR surface = nullptr;
	VkPhysicalDevice findPhysicalDevice();
	bool findGraphicsQueue();
	bool createSurface();
	void run();
	bool createVulkanInstance();
	bool initializeVulkan();
	bool initialize();
	void shutdown();
};



