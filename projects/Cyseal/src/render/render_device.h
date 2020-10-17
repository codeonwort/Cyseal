#pragma once

#include "core/types.h"
#include "pixel_format.h"
#include <Windows.h>

class SwapChain;
class RenderCommandAllocator;
class RenderCommandList;
class RenderCommandQueue;
class GPUResource;
class DepthStencilView;
class VertexBuffer;
class IndexBuffer;

enum class ERenderDeviceRawAPI
{
	DirectX12,
	Vulkan
};

enum class ERayTracingTier
{
	NotSupported,
	Tier_1_0
};

enum class EWindowType
{
	FULLSCREEN,
	BORDERLESS,
	WINDOWED
};

struct RenderDeviceCreateParams
{
	HWND hwnd;
	ERenderDeviceRawAPI rawAPI;
	ERayTracingTier rayTracingTier;
	bool enableDebugLayer = true; // Enable debug layer (dx) or validation layer (vk)

	// #todo-renderdevice: These are not renderdevice params. Move to somewhere.
	// or leave here as initial values.
	EWindowType windowType = EWindowType::WINDOWED;
	uint32 windowWidth = 1920;
	uint32 windowHeight = 1080;
};

// ID3D12Device and VkDevice
class RenderDevice
{
	
public:
	RenderDevice();
	virtual ~RenderDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) = 0;

	virtual void recreateSwapChain(HWND hwnd, uint32 width, uint32 height) = 0;

	virtual void flushCommandQueue() = 0;

	virtual bool supportsRayTracing() = 0;

	virtual VertexBuffer* createVertexBuffer(void* data, uint32 sizeInBytes, uint32 strideInBytes) = 0;
	virtual IndexBuffer* createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format) = 0;

	inline SwapChain* getSwapChain() const { return swapChain; }
	inline GPUResource* getDefaultDepthStencilBuffer() const { return defaultDepthStencilBuffer; }
	inline DepthStencilView* getDefaultDSV() const { return defaultDSV; }

	inline RenderCommandAllocator* getCommandAllocator() const { return commandAllocator; }
	inline RenderCommandList* getCommandList() const { return commandList; }
	inline RenderCommandQueue* getCommandQueue() const { return commandQueue; }

protected:
	SwapChain*              swapChain;
	GPUResource*            defaultDepthStencilBuffer;
	DepthStencilView*       defaultDSV;

	RenderCommandAllocator* commandAllocator;
	RenderCommandQueue*     commandQueue;
	RenderCommandList*      commandList;

};

extern RenderDevice* gRenderDevice;
