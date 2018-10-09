#pragma once

#include <Windows.h>
#include <stdint.h>

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
		uint32_t      width,
		uint32_t      height) = 0;
	virtual void present() = 0;
	virtual void swapBackBuffer() = 0;

	inline uint32_t getBackBufferWidth() const { return backBufferWidth; }
	inline uint32_t getBackBufferHeight() const { return backBufferHeight; }

	virtual GPUResource* getCurrentBackBuffer() const = 0;
	virtual RenderTargetView* getCurrentBackBufferRTV() const = 0;

protected:
	uint32_t backBufferWidth;
	uint32_t backBufferHeight;

};
