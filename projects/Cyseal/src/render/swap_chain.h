#pragma once

#include <Windows.h>
#include <stdint.h>

class RenderDevice;
class SwapChain;

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

};
