#pragma once

#include "render/render_device.h"

#if !COMPILE_BACKEND_VULKAN

class VulkanDevice : public RenderDevice {};

#else // COMPILE_BACKEND_VULKAN

#include "util/logging.h"
#include <vector>
#include <vulkan/vulkan_core.h>

DECLARE_LOG_CATEGORY(LogVulkan);

class VulkanDevice : public RenderDevice
{
	friend class VulkanSwapchain;

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

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

	virtual bool supportsRayTracing() override;

	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName) override;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual IndexBuffer* createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format) override;
	virtual Texture* createTexture(const TextureCreateParams& createParams) override;
	virtual Shader* createShader() override;
	virtual RootSignature* createRootSignature(const RootSignatureDesc& desc) override;
	virtual PipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) override;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) override;
	virtual ConstantBuffer* createConstantBuffer(DescriptorHeap* descriptorHeap, uint32 heapSize, uint32 payloadSize) override;

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) override;

	inline VkDevice getRaw() const { return vkDevice; }

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
	VkInstance vkInstance = VK_NULL_HANDLE;
	VkPhysicalDevice vkPhysicalDevice = VK_NULL_HANDLE;
	VkDevice vkDevice = VK_NULL_HANDLE;

	VkSurfaceKHR vkSurface = VK_NULL_HANDLE;

	VkQueue vkGraphicsQueue = VK_NULL_HANDLE;
	VkQueue vkPresentQueue = VK_NULL_HANDLE;

	// #todo-vulkan: VulkanSwapchain
	VkSwapchainKHR swapchain;
	std::vector<VkImage> swapchainImages;
	VkFormat swapchainImageFormat;
	VkExtent2D swapchainExtent;
	std::vector<VkImageView> swapchainImageViews;
	VkRenderPass backbufferRenderPass;
	std::vector<VkFramebuffer> swapchainFramebuffers;
	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;

	VkCommandPool vkCommandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> vkCommandBuffers;
	VkSemaphore vkImageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore vkRenderFinishedSemaphore = VK_NULL_HANDLE;

	VkDebugReportCallbackEXT vkDebugCallback = VK_NULL_HANDLE;
	bool enableDebugLayer = false;
};

#endif // COMPILE_BACKEND_VULKAN
