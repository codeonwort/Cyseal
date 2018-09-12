#pragma once

#include <Windows.h>
#include <stdint.h>

class SwapChain;
class RenderCommandAllocator;

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
	virtual void draw() = 0;

protected:
	SwapChain* swapChain;
	RenderCommandAllocator* commandAllocator;

};
