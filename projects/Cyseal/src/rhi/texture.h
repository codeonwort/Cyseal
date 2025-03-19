#pragma once

#include "rhi/gpu_resource.h"
#include "rhi/render_command.h"
#include "util/enum_util.h"

enum class ETextureDimension : uint8
{
    UNKNOWN   = 0,
    TEXTURE1D = 1,
    TEXTURE2D = 2,
    TEXTURE3D = 3
};

// D3D12_RESOURCE_FLAGS (my texture variant)
enum class ETextureAccessFlags : uint32
{
    SRV          = 1 << 0,
    RTV          = 1 << 1,
    UAV          = 1 << 2,
    DSV          = 1 << 3,
    CPU_WRITE    = 1 << 4,
	CPU_READBACK = 1 << 5,

    COLOR_ALL = SRV | RTV | UAV
};
ENUM_CLASS_FLAGS(ETextureAccessFlags);

// D3D12_RESOURCE_DESC (CD3DX12_RESOURCE_DESC)
// VkImageCreateInfo
struct TextureCreateParams
{
    ETextureDimension dimension;
    EPixelFormat format;
    ETextureAccessFlags accessFlags;
    uint32 width;
    uint32 height;
    uint16 depth; // or array size
    uint16 mipLevels; // 0 means full mips
    uint32 sampleCount;
    uint32 sampleQuality;
    uint32 numLayers = 1; // #todo-texture: For tex2Darray or texCube

	float optimalClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float optimalClearDepth = 0.0f;
	uint8 optimalClearStencil = 0;
	TextureCreateParams& setOptimalClearColor(float r, float g, float b, float a)
	{
		optimalClearColor[0] = r;
		optimalClearColor[1] = g;
		optimalClearColor[2] = b;
		optimalClearColor[3] = a;
		return *this;
	}
	TextureCreateParams& setOptimalClearDepth(float depth)
	{
		optimalClearDepth = depth;
		return *this;
	}
	TextureCreateParams& setOptimalClearStencil(uint8 stencil)
	{
		optimalClearStencil = stencil;
		return *this;
	}

    static TextureCreateParams texture2D(
        EPixelFormat inFormat,
        ETextureAccessFlags inAccessFlags,
        uint32 inWidth,
        uint32 inHeight,
        uint16 inMipLevels = 1,
        uint32 inSampleCount = 1,
        uint32 inSampleQuality = 0)
    {
        return TextureCreateParams{
            ETextureDimension::TEXTURE2D,
            inFormat,
            inAccessFlags,
            inWidth,
            inHeight,
            1,
            inMipLevels,
            inSampleCount,
            inSampleQuality
        };
    }
	
	static TextureCreateParams textureCube(
		EPixelFormat inFormat,
		ETextureAccessFlags inAccessFlags,
		uint32 inWidth,
		uint32 inHeight,
		uint16 inMipLevels = 1,
		uint32 inSampleCount = 1,
		uint32 inSampleQuality = 0)
	{
		return TextureCreateParams{
			ETextureDimension::TEXTURE2D,
			inFormat,
			inAccessFlags,
			inWidth,
			inHeight,
			1,
			inMipLevels,
			inSampleCount,
			inSampleQuality,
			6
		};
	}

    static TextureCreateParams texture3D(
		EPixelFormat inFormat,
		ETextureAccessFlags inAccessFlags,
		uint32 inWidth,
		uint32 inHeight,
		uint16 inDepth,
		uint16 inMipLevels = 1,
		uint32 inSampleCount = 1,
		uint32 inSampleQuality = 0)
	{
		return TextureCreateParams{
			ETextureDimension::TEXTURE3D,
			inFormat,
			inAccessFlags,
			inWidth,
			inHeight,
			inDepth,
			inMipLevels,
			inSampleCount,
			inSampleQuality,
			1
		};
	}
};

class Texture : public GPUResource
{
public:
	virtual const TextureCreateParams& getCreateParams() const = 0;

	virtual void uploadData(
		RenderCommandList& commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch,
		uint32 subresourceIndex = 0) = 0;

	virtual uint64 getRowPitch() const { return 0; }

	// createParams should have CPU_READBACK flag.
	virtual uint64 getReadbackBufferSize() const { return 0; }

	// Invoke while constructing command list.
	// @return false if failed.
	virtual bool prepareReadback(RenderCommandList* commandList) { return false; }

	// Invoke after flushing command queue.
	// dst should be a preallocated memory as large as the size returned by getReadbackBufferSize().
	// @return false if failed.
	virtual bool readbackData(void* dst) { return false; }
};
