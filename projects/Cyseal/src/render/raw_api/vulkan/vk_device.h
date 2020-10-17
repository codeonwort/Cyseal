#pragma once

#include "render/render_device.h"
#include <vector>

#include <vulkan/vulkan_core.h>

class VulkanDevice : public RenderDevice
{
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

private:
	VkInstance instance;
	VkDebugReportCallbackEXT callback;
	VkSurfaceKHR surface;

	bool enableDebugLayer = false;
};
