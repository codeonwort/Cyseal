#pragma once

#include "core/types.h"
#include "core/assertion.h"
#include "util/enum_util.h"
#include "pixel_format.h"

#include <vector>

class RenderCommandList;
class DescriptorHeap;

class VertexBufferPool;
class IndexBufferPool;

class ConstantBufferView;
class RenderTargetView;
class ShaderResourceView;
class DepthStencilView;
class UnorderedAccessView;

// GPU Resources = Anything resides in GPU-visible memory
// (buffers, textures, acceleration structures, ...)

// #todo-rhi: Not good abstraction for Vulkan
// D3D12_RESOURCE_STATES
enum class EGPUResourceState : uint32
{
	COMMON                     = 0,
	VERTEX_AND_CONSTANT_BUFFER = 0x1,
	INDEX_BUFFER               = 0x2,
	RENDER_TARGET              = 0x4,
	UNORDERED_ACCESS           = 0x8,
	DEPTH_WRITE                = 0x10,
	DEPTH_READ                 = 0x20,
	NON_PIXEL_SHADER_RESOURCE  = 0x40,
	PIXEL_SHADER_RESOURCE      = 0x80,
	STREAM_OUT                 = 0x100,
	INDIRECT_ARGUMENT          = 0x200,
	COPY_DEST                  = 0x400,
	COPY_SOURCE                = 0x800,
	RESOLVE_DEST               = 0x1000,
	RESOLVE_SOURCE             = 0x2000,
	//GENERIC_READ             = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
	PRESENT                    = 0,
	PREDICATION                = 0x200,
	VIDEO_DECODE_READ          = 0x10000,
	VIDEO_DECODE_WRITE         = 0x20000,
	VIDEO_PROCESS_READ         = 0x40000,
	VIDEO_PROCESS_WRITE        = 0x80000
};

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

enum class EDepthClearFlags : uint8
{
	DEPTH   = 0x1,
	STENCIL = 0x2,
	DEPTH_STENCIL = DEPTH | STENCIL
};
ENUM_CLASS_FLAGS(EDepthClearFlags);

// Base class for GPU resources (buffers, textures, accel structs, ...)
// ID3D12Resource
class GPUResource
{
public:
	virtual ~GPUResource() {}

	// D3D12: ID3D12Resource
	// Vulkan: VkBuffer or VkImage
	virtual void* getRawResource() const { CHECK_NO_ENTRY(); return nullptr; }
	virtual void setRawResource(void* inRawResource) { CHECK_NO_ENTRY(); }

	virtual void setDebugName(const wchar_t* inDebugName) {}
};

//////////////////////////////////////////////////////////////////////////
// Texture

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
