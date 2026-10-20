#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <volk/volk.h>
#include <string>
#include <iostream>
#include "vk_mem_alloc.h"
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
	//queue
	VkQueue gfxQueue = nullptr;
	uint32_t gfxQueueFamIdx = UINT32_MAX;
	//Device
	VkPhysicalDevice physicalDevice = nullptr;
	VkDevice device = nullptr;
	//Vulkan core
	SDL_Window* window = nullptr;
	VkSurfaceKHR surface = nullptr;
	VmaAllocator vmaAllocator = nullptr;

	//SwapChain
	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	uint32_t swapchainHeight{};
	uint32_t swapchainWidth{};
	//functions
	bool createSwapchain(uint32_t width, uint32_t height);
	bool initializeVMA();
	bool createDevice(VkPhysicalDevice physicalDevice);
	VkPhysicalDevice findPhysicalDevice();
	bool findGraphicsQueue();
	bool createSurface();
	void run();
	bool createVulkanInstance();
	bool initializeVulkan();
	bool initialize();
	void shutdown();
};



