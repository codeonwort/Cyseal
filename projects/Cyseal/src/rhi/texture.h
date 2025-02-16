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
    TextureCreateParams& setOptimalClearColor(float r, float g, float b, float a)
    {
        optimalClearColor[0] = r;
        optimalClearColor[1] = g;
        optimalClearColor[2] = b;
        optimalClearColor[3] = a;
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
};
