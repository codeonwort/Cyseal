#pragma once

#include <Windows.h>
#include "core/int_types.h"
#include "pixel_format.h"

class RenderDevice;
class SwapChain;
class GPUResource;
class RenderTargetView;

// ID3D12SwapChain
// VkSwapChainKHR
class SwapChain
{
	
public:
	SwapChain();
	virtual ~SwapChain();

	virtual void initialize(
		RenderDevice* renderDevice,
		HWND          hwnd,
		uint32        width,
		uint32        height) = 0;
	virtual void present() = 0;
	virtual void swapBackBuffer() = 0;

	virtual GPUResource* getCurrentBackBuffer() const = 0;
	virtual RenderTargetView* getCurrentBackBufferRTV() const = 0;

	inline uint32 getBackbufferWidth() const { return backBufferWidth; }
	inline uint32 getBackbufferHeight() const { return backBufferHeight; }
	inline EPixelFormat getBackbufferFormat() const { return backbufferFormat; }
	inline EPixelFormat getBackbufferDepthFormat() const { return backbufferDepthFormat; }

	// #todo-swapchain: Support 4xMSAA
	virtual bool supports4xMSAA() const { return false; }
	virtual uint32 get4xMSAAQuality() const { return 1; }

protected:
	uint32 backBufferWidth;
	uint32 backBufferHeight;
	EPixelFormat backbufferFormat;
	EPixelFormat backbufferDepthFormat;

};
