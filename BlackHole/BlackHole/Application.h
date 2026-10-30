#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <volk/volk.h>
#include <string>
#include <iostream>
#include "vk_mem_alloc.h"
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <sstream>
#include "utils.h"
#include <array>
#include "Camera.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"
struct SDL_Window;
struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;
struct FrameResources
{
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
	VkImage depthImage = VK_NULL_HANDLE;
	VkImageView depthImageView = VK_NULL_HANDLE;
	VmaAllocation depthImageAllocation = nullptr;
};


//default configs
struct Application {

	float sceneScale = 1.0f;
	float diskBrightness = 2.5f;
	float hazeStrength = 1.0f;
	float diskTiltDegrees = 15.0f;

	bool showControls = true;

	Camera camera;
	bool mouseCaptured = true;

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData
		);

	VkInstance vulkanInstance{ VK_NULL_HANDLE };
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	static constexpr uint32_t MaxFramesInFlight{ 2 };
	constexpr static uint32_t VulkanVersion{ VK_API_VERSION_1_3 };
	bool running{ false };
	float width = 1280;
	float height = 720;
	const Uint64 startTime = SDL_GetPerformanceCounter();
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
	//Graphics Pipeline
	VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = nullptr;
	VkPipeline pipeline = nullptr;
	//Shaders
	VkShaderModule vertShader{};
	VkShaderModule fragShader{};
	//Command
	bool createCommandBuffers();
	//image
	VkFormat depthFormat = VK_FORMAT_UNDEFINED;
	//Render
	uint64_t frameIndex = 0;
	uint64_t nextSignalValue = MaxFramesInFlight + 1;
	//SwapChain
	std::vector<VkSemaphore> renderCompleteSemaphores;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
	uint32_t swapchainHeight{};
	uint32_t swapchainWidth{};
	bool requireSwapchainRecreate = false;
	//Synchronization
	std::array<FrameResources, MaxFramesInFlight> frameResources;
	//functions
	void buildControlUI();
	bool InitImGui();
	bool chooseDepthFormat();
	bool createSyncResources();
	VkPipeline createGraphicsPipeline();
	bool createShaders();
	VkShaderModule createShaderModule(const std::string& filename, shaderc_shader_kind kind) const;
	void destroySwapchain();
	bool createSwapchain(uint32_t width, uint32_t height);
	bool initializeVMA();
	bool createDevice(VkPhysicalDevice physicalDevice);
	VkPhysicalDevice findPhysicalDevice();
	bool findGraphicsQueue();
	bool createSurface();
	void render();
	void run();
	bool createVulkanInstance();
	bool initializeVulkan();
	bool initialize();
	void shutdown();
};





