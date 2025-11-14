#pragma once

#include "core/int_types.h"
#include "pixel_format.h"
#include "texture_kind.h"

class RenderDevice;
class RenderCommandList;
class SwapChain;
class GPUResource;
class RenderTargetView;

// Marker class.
class SwapChainImage : public TextureKind
{
public:
	virtual TextureKindShapeDesc internal_getShapeDesc() override
	{
		return shapeDesc;
	}

	void internal_setShapeDesc(uint32 inWidth, uint32 inHeight, EPixelFormat inFormat)
	{
		shapeDesc = TextureKindShapeDesc{
			TextureKindShapeDesc::Dimension::Tex2D,
			inFormat,
			inWidth, inHeight, 1, 1, 1,
		};
	}

private:
	TextureKindShapeDesc shapeDesc;
};

// ID3D12SwapChain
// VkSwapchainKHR
class SwapChain
{
public:
	SwapChain();
	virtual ~SwapChain();

	virtual void initialize(
		RenderDevice* renderDevice,
		void*         nativeWindowHandle,
		uint32        width,
		uint32        height) = 0;

	virtual void resize(uint32 newWidth, uint32 newHeight) = 0;

	virtual void present() = 0;
	virtual void swapBackbuffer() = 0;
	virtual uint32 getBufferCount() const = 0;

	uint32 getNextBackbufferIndex() const
	{
		return (getCurrentBackbufferIndex() + 1) % getBufferCount();
	}

	virtual uint32 getCurrentBackbufferIndex() const = 0;
	virtual SwapChainImage* getSwapchainBuffer(uint32 ix) const = 0;
	virtual RenderTargetView* getSwapchainBufferRTV(uint32 ix) const = 0;

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
