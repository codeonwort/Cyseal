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
	virtual void swapBackbuffer() = 0;

	virtual GPUResource* getCurrentBackbuffer() const = 0;
	virtual RenderTargetView* getCurrentBackbufferRTV() const = 0;

	inline uint32 getBackbufferWidth() const { return backbufferWidth; }
	inline uint32 getBackbufferHeight() const { return backbufferHeight; }
	inline EPixelFormat getBackbufferFormat() const { return backbufferFormat; }
	inline EPixelFormat getBackbufferDepthFormat() const { return backbufferDepthFormat; }

	// #todo-swapchain: Support 4xMSAA
	virtual bool supports4xMSAA() const { return false; }
	virtual uint32 get4xMSAAQuality() const { return 1; }

protected:
	// Should match with those from RenderDevice
	uint32 backbufferWidth;
	uint32 backbufferHeight;
	EPixelFormat backbufferFormat;
	EPixelFormat backbufferDepthFormat;
};
