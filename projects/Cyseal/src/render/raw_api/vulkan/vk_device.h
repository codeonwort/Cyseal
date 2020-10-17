#pragma once

#include "render/render_device.h"
#include <vector>

#include <vulkan/vulkan_core.h>

class VulkanDevice : public RenderDevice
{
	struct QueueFamilyIndices
	{
		int graphicsFamily = -1;
		int presentFamily = -1;
		bool isComplete()
		{
			return graphicsFamily >= 0 && presentFamily >= 0;
		}
	};

	struct SwapChainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

public:
	VulkanDevice();
	~VulkanDevice();

	void initialize(const RenderDeviceCreateParams& createParams) override;

	void recreateSwapChain(HWND hwnd, uint32 width, uint32 height) override;

	void flushCommandQueue() override;

	bool supportsRayTracing() override;

	virtual VertexBuffer* createVertexBuffer(void* data, uint32 sizeInBytes, uint32 strideInBytes);
	virtual IndexBuffer* createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format);

private:
	bool checkValidationLayerSupport();
	void getRequiredExtensions(std::vector<const char*>& extensions);
	bool isDeviceSuitable(VkPhysicalDevice physDevice);
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physDevice);
	bool checkDeviceExtensionSupport(VkPhysicalDevice physDevice);
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice physDevice);
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32 windowWidth, uint32 windowHeight);

private:
	VkInstance instance;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;

	VkSurfaceKHR surface;

	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSwapchainKHR swapchain;
	std::vector<VkImage> swapchainImages;
	VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;

	VkDebugReportCallbackEXT callback;
	bool enableDebugLayer = false;
};
