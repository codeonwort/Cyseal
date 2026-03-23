#pragma once

#include "rhi/texture_kind.h"
#include "util/enum_util.h"
#include "core/smart_pointer.h"

class RenderCommandList;

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
	NONE         = 0, // A resource without any flags is no meaningful in any way. Some platform APIs will even report an error.
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
	TextureCreateParams& setOptimalClearDepth(float clearDepth)
	{
		optimalClearDepth = clearDepth;
		return *this;
	}
	TextureCreateParams& setOptimalClearStencil(uint8 stencil)
	{
		optimalClearStencil = stencil;
		return *this;
	}

	static TextureCreateParams texture1D(
		EPixelFormat inFormat,
		ETextureAccessFlags inAccessFlags,
		uint32 inWidth,
		uint16 inMipLevels = 1,
		uint32 inSampleCount = 1,
		uint32 inSampleQuality = 0)
	{
		return TextureCreateParams{
            ETextureDimension::TEXTURE1D,
            inFormat,
            inAccessFlags,
            inWidth,
            1,
            1,
            inMipLevels,
            inSampleCount,
            inSampleQuality
        };
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

class Texture : public TextureKind
{
public:
	struct ReadbackRegion
	{
		uint32 mipLevel       = 0;
		uint32 baseArrayLayer = 0;
		uint32 layerCount     = 1;
		uint32 offsetX        = 0;
		uint32 offsetY        = 0;
		uint32 offsetZ        = 0;
		uint32 sizeX          = 0;
		uint32 sizeY          = 0;
		uint32 sizeZ          = 0;

		static ReadbackRegion mip0(Texture* texture)
		{
			ReadbackRegion region{};
			region.sizeX = texture->getCreateParams().width;
			region.sizeY = texture->getCreateParams().height;
			region.sizeZ = texture->getCreateParams().depth;
			return region;
		}
	};
	struct ReadbackHandle
	{
		// Do not modify members but only read them.
		bool     bAvailable   = false;
		void*    readbackData = nullptr; // CAUTION: Data might not be tightly packed and you must consider rowPitch when moving to the next row.
		uint64   rowPitch     = 0;
		uint64   slicePitch   = 0;
		uint64   totalBytes   = 0;
		Texture* owner        = nullptr;
	};

	virtual TextureKindShapeDesc internal_getShapeDesc() override
	{
		const TextureCreateParams& params = getCreateParams();
		TextureKindShapeDesc::Dimension dim;
		switch (params.dimension)
		{
			case ETextureDimension::UNKNOWN   : dim = TextureKindShapeDesc::Dimension::Unknown; break;
			case ETextureDimension::TEXTURE1D : dim = TextureKindShapeDesc::Dimension::Tex1D; break;
			case ETextureDimension::TEXTURE2D : dim = TextureKindShapeDesc::Dimension::Tex2D; break;
			case ETextureDimension::TEXTURE3D : dim = TextureKindShapeDesc::Dimension::Tex3D; break;
			default: dim = TextureKindShapeDesc::Dimension::Unknown; CHECK_NO_ENTRY();
		}
		return TextureKindShapeDesc{
			dim, params.format, params.width, params.height, params.depth, params.mipLevels, params.numLayers,
		};
	}

	virtual const TextureCreateParams& getCreateParams() const = 0;

	/// <summary>
	/// Upload data to the internal GPU texture resource.
	/// This is allowed only if the texture was initialized with ETextureAccessFlags::CPU_WRITE flag.
	/// </summary>
	/// <param name="commandList"></param>
	/// <param name="buffer"></param>
	/// <param name="rowPitch"></param>
	/// <param name="slicePitch"></param>
	/// <param name="subresourceIndex"></param>
	virtual void uploadData(
		RenderCommandList* commandList,
		const void* buffer,
		uint64 rowPitch,
		uint64 slicePitch,
		uint32 subresourceIndex = 0) = 0;

	virtual uint64 getRowPitch() const = 0;

	/// <summary>
	/// Create a request to readback data from GPU.
	/// This is allowed only if the texture was initialized with ETextureAccessFlags::CPU_READBACK flag.
	/// The data is available when a render device executes the command list and the command queue in the device is flushed.
	/// The returned request could be null if the request failed for somehow.
	/// </summary>
	/// <param name="commandList">Command list in which the request will be processed.</param>
	/// <param name="region">Subregion of this texture to readback.</param>
	/// <returns>Handle to the readback request.</returns>
	virtual SharedPtr<ReadbackHandle> requestReadback(RenderCommandList* commandList, const ReadbackRegion& region)
	{
		return nullptr;
	}
};
