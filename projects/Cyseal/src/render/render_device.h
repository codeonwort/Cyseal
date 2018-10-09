#pragma once

#include <Windows.h>
#include <stdint.h>

class SwapChain;
class RenderCommandAllocator;
class RenderCommandList;
class RenderCommandQueue;
class GPUResource;
class DepthStencilView;

enum class ERenderDeviceRawAPI
{
	DirectX12,
	Vulkan
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

	EWindowType windowType;
	uint32_t windowWidth;
	uint32_t windowHeight;
};

// ID3D12Device
// VkDevice
class RenderDevice
{
	
public:
	RenderDevice();
	virtual ~RenderDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) = 0;
	virtual void recreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) = 0;
	virtual void flushCommandQueue() = 0;

	inline SwapChain* getSwapChain() const { return swapChain; }
	inline GPUResource* getDefaultDepthStencilBuffer() const { return defaultDepthStencilBuffer; }
	inline DepthStencilView* getDefaultDSV() const { return defaultDSV; }

	inline RenderCommandAllocator* getCommandAllocator() const { return commandAllocator; }
	inline RenderCommandList* getCommandList() const { return commandList; }
	inline RenderCommandQueue* getCommandQueue() const { return commandQueue; }

protected:
	SwapChain* swapChain;
	GPUResource* defaultDepthStencilBuffer;
	DepthStencilView* defaultDSV;

	RenderCommandAllocator* commandAllocator;
	RenderCommandQueue* commandQueue;
	RenderCommandList* commandList;

};
