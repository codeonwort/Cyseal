#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "util/logging.h"
#include "rhi/render_device.h"

#include <vector>
#include <Volk/volk.h>

DECLARE_LOG_CATEGORY(LogVulkan);

class VulkanSwapchain;

VkDevice getVkDevice();

class VulkanDevice : public RenderDevice
{
	friend class VulkanSwapchain;

public:
	VulkanDevice() = default;
	~VulkanDevice();

	virtual void onInitialize(const RenderDeviceCreateParams& createParams) override;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) override;

	virtual void flushCommandQueue() override;

	// ------------------------------------------------------------------------
	// Plugin: DearImgui

	virtual void initializeDearImgui() override;
	virtual void beginDearImguiNewFrame() override;
	virtual void renderDearImgui(RenderCommandList* commandList, SwapChainImage* swapChainImage) override;
	virtual void shutdownDearImgui() override;

	// ------------------------------------------------------------------------
	// Create

	virtual RenderCommandList* createRenderCommandList() override;
	virtual RenderCommandAllocator* createRenderCommandAllocator() override;

	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, EBufferAccessFlags usageFlags, const wchar_t* inDebugName) override;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) override;

	virtual IndexBuffer* createIndexBuffer(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags, const wchar_t* inDebugName) override;
	virtual IndexBuffer* createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes, EPixelFormat format) override;
	
	virtual Buffer* createBuffer(const BufferCreateParams& createParams) override;
	virtual Texture* createTexture(const TextureCreateParams& createParams) override;

	virtual ShaderStage* createShader(EShaderStage shaderStage, const char* debugName) override;

	virtual GraphicsPipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) override;

	virtual ComputePipelineState* createComputePipelineState(const ComputePipelineDesc& desc) override;

	virtual RaytracingPipelineStateObject* createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc& desc) override;

	virtual RaytracingShaderTable* createRaytracingShaderTable(
		RaytracingPipelineStateObject* RTPSO,
		uint32 numShaderRecords,
		uint32 rootArgumentSize,
		const wchar_t* debugName) override;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) override;

	virtual ConstantBufferView* createCBV(Buffer* buffer, DescriptorHeap* descriptorHeap, uint32 sizeInBytes, uint32 offsetInBytes) override;
	virtual ShaderResourceView* createSRV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const ShaderResourceViewDesc& createParams) override;
	virtual UnorderedAccessView* createUAV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const UnorderedAccessViewDesc& createParams) override;
	virtual RenderTargetView* createRTV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const RenderTargetViewDesc& createParams) override;
	virtual DepthStencilView* createDSV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const DepthStencilViewDesc& createParams) override;

	virtual ShaderResourceView* createSRV(GPUResource* gpuResource, const ShaderResourceViewDesc& createParams) override;
	virtual UnorderedAccessView* createUAV(GPUResource* gpuResource, const UnorderedAccessViewDesc& createParams) override;
	virtual RenderTargetView* createRTV(GPUResource* gpuResource, const RenderTargetViewDesc& createParams) override;
	virtual DepthStencilView* createDSV(GPUResource* gpuResource, const DepthStencilViewDesc& createParams) override;

	virtual ShaderResourceView* cloneSRVWithDifferentHeap(ShaderResourceView* src, DescriptorHeap* anotherHeap) override;

	virtual CommandSignature* createCommandSignature(const CommandSignatureDesc& inDesc, GraphicsPipelineState* inPipelineState) override;
	virtual CommandSignature* createCommandSignature(const CommandSignatureDesc& inDesc, ComputePipelineState* inPipelineState) override;
	virtual CommandSignature* createCommandSignature(const CommandSignatureDesc& inDesc, RaytracingPipelineStateObject* inPipelineState) override;
	virtual IndirectCommandGenerator* createIndirectCommandGenerator(const CommandSignatureDesc& inDesc, uint32 maxCommandCount) override;

	// ------------------------------------------------------------------------
	// Copy

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) override;

	// ------------------------------------------------------------------------
	// Getters

	virtual RenderCommandList* getCommandListForCustomCommand() const override;

	// #todo-vulkan: Correct constant buffer data alignment
	virtual uint32 getConstantBufferDataAlignment() const { return 256; }

// ------------------------------------------------------------------------
// Internal use only
public:
	inline VkDevice getRaw() const { return vkDevice; }
	inline VkPhysicalDevice getVkPhysicalDevice() const { return vkPhysicalDevice; }
	inline VkSurfaceKHR getVkSurface() const { return vkSurface; }
	inline VkQueue getVkGraphicsQueue() const { return vkGraphicsQueue; }
	inline VkQueue getVkPresentQueue() const { return vkPresentQueue; }
	inline VkSemaphore getVkSwapchainImageAvailableSemaphore(uint32 swapchainIx) const { return vkSwapchainImageAvailableSemaphores[swapchainIx]; }
	inline VkSemaphore getVkRenderFinishedSemaphore() const { return vkRenderFinishedSemaphore; }

	inline const VkPhysicalDeviceProperties2& getVkPhysicalDeviceProperties2() const { return vkPhysicalDeviceProperties2; }

	void beginVkDebugMarker(VkCommandBuffer& cmdBuffer, const char* debugName, uint32 color = 0x000000);
	void endVkDebugMarker(VkCommandBuffer& cmdBuffer);
	void setObjectDebugName(
		VkDebugReportObjectTypeEXT objectType,
		uint64 objectHandle,
		const char* debugName);

	VkCommandPool getTempCommandPool() const;

private:
	bool checkValidationLayerSupport();
	void getRequiredExtensions(std::vector<const char*>& extensions);
	bool isDeviceSuitable(VkPhysicalDevice physDevice, bool bSkipSwapchainSupport);
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

	VkPhysicalDeviceProperties2 vkPhysicalDeviceProperties2;

	VkQueue vkGraphicsQueue = VK_NULL_HANDLE;
	VkQueue vkPresentQueue = VK_NULL_HANDLE;

	std::vector<VkSemaphore> vkSwapchainImageAvailableSemaphores; // Swapchain image is available.
	VkSemaphore vkRenderFinishedSemaphore = VK_NULL_HANDLE; // Graphics queue has finished.

	VkDebugReportCallbackEXT vkDebugCallback = VK_NULL_HANDLE;
	bool enableDebugLayer = false;

	// #todo-vulkan: EXT - Debug marker
	bool canEnableDebugMarker = false;
	PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBegin = VK_NULL_HANDLE;
	PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEnd = VK_NULL_HANDLE;
	PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
