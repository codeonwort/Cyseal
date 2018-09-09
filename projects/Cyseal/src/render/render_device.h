#pragma once

#include <Windows.h>
#include <stdint.h>

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

// Encapsulates D3D device or Vulkan device.
class RenderDevice
{
	
public:
	virtual void initialize(const RenderDeviceCreateParams& createParams) = 0;

protected:
	//

};
