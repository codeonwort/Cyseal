#pragma once

#include "render/render_device.h"

#if !COMPILE_BACKEND_VULKAN

class VulkanDevice : public RenderDevice {};

#else // COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "util/logging.h"

#include <vector>
#include <vulkan/vulkan_core.h>

DECLARE_LOG_CATEGORY(LogVulkan);

class VulkanSwapchain;

class VulkanDevice : public RenderDevice
{
	friend class VulkanSwapchain;

public:
	VulkanDevice() = default;
	~VulkanDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

	virtual bool supportsRayTracing() override;

	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName) override;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual IndexBuffer* createIndexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName) override;
	virtual IndexBuffer* createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual Texture* createTexture(const TextureCreateParams& createParams) override;

	virtual ShaderStage* createShader(EShaderStage shaderStage, const char* debugName) override;

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

	// Internal use only
	inline VkDevice getRaw() const { return vkDevice; }
	inline VkPhysicalDevice getVkPhysicalDevice() const { return vkPhysicalDevice; }
	inline VkSurfaceKHR getVkSurface() const { return vkSurface; }
	inline VkQueue getVkGraphicsQueue() const { return vkGraphicsQueue; }
	inline VkQueue getVkPresentQueue() const { return vkPresentQueue; }
	inline VkSemaphore getVkImageAvailableSemoaphre() const { return vkImageAvailableSemaphore; }
	inline VkSemaphore getVkRenderFinishedSemoaphre() const { return vkRenderFinishedSemaphore; }

	VkCommandPool getTempCommandPool() const;

private:
	bool checkValidationLayerSupport();
	void getRequiredExtensions(std::vector<const char*>& extensions);
	bool isDeviceSuitable(VkPhysicalDevice physDevice);
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

	VkSemaphore vkImageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore vkRenderFinishedSemaphore = VK_NULL_HANDLE;

	VkDebugReportCallbackEXT vkDebugCallback = VK_NULL_HANDLE;
	bool enableDebugLayer = false;
};

#endif // COMPILE_BACKEND_VULKAN
