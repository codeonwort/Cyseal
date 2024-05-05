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

// D3D12_RESOURCE_FLAGS
// VkBufferUsageFlags
// #wip-buffer: more flags and redundant with EGPUResourceState right above
enum class EBufferAccessFlags : uint32
{
	NONE          = 0,
	COPY_SRC      = 1 << 0, // Can be a source of copy operation (CPU can write data to the buffer)
	COPY_DST      = 1 << 1, // Can be a destination of copy operation
	VERTEX_BUFFER = 1 << 2, // Can be bound as vertex buffer
	INDEX_BUFFER  = 1 << 3, // Can be bound as index buffer
	CBV           = 1 << 4, // Can be bound as SRV
	SRV           = 1 << 5, // Can be bound as SRV
	UAV           = 1 << 6, // Can be bound as UAV
};
ENUM_CLASS_FLAGS(EBufferAccessFlags);

struct BufferCreateParams
{
	uint64             sizeInBytes;
	uint32             alignment   = 0;
	EBufferAccessFlags accessFlags = EBufferAccessFlags::NONE;
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
// Vertex Buffer

struct VertexBufferCreateParams
{
	// Buffer size, must be non-zero.
	uint32 sizeInBytes;

	// If null, the initial data is undefined.
	void* initialData = nullptr;
	// Only meaningful if initialData is there.
	uint32 strideInBytes = 0;

	// If false, this buffer will be suballocated from a global pool.
	// Otherwise, this buffer uses separate allocation.
	// CAUTION: Separate allocation may consume large portion of VRAM
	//          than the buffer actually requires, and there is upper limit
	//          of total allocation count.
	bool bCommittedResource = false;
};

// Can be a committed resource or suballocation of a vertex buffer pool.
// #todo-rhi: Remove VertexBuffer or make it a child class of Buffer.
class VertexBuffer : public GPUResource
{
	friend class VertexBufferPool;
public:
	virtual void initialize(uint32 sizeInBytes, EBufferAccessFlags usageFlags) = 0;
	
	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) = 0;

	virtual uint32 getVertexCount() const = 0;

	// offsetInPool
	virtual uint64 getBufferOffsetInBytes() const = 0;

	VertexBufferPool* internal_getParentPool() const { return parentPool; }

protected:
	// Null if a committed resource.
	VertexBufferPool* parentPool = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Index Buffer

struct IndexBufferCreateParams
{
	uint32 numIndices;
	uint32 elementSize;
};

// Can be a committed resource or suballocation of an index buffer pool.
// #todo-rhi: Remove IndexBuffer or make it a child class of Buffer.
class IndexBuffer : public GPUResource
{
public:
	virtual void initialize(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags) = 0;

	virtual void initializeWithinPool(
		IndexBufferPool* pool,
		uint64 offsetInPool,
		uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) = 0;

	virtual uint32 getIndexCount() const = 0;
	virtual EPixelFormat getIndexFormat() const = 0;

	// offsetInPool
	virtual uint64 getBufferOffsetInBytes() const = 0;

protected:
	// Null if a committed resource.
	IndexBufferPool* parentPool = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Buffer

// A generic buffer that maintains its own committed resource.
// It's main purpose is to serve GPU memory for various buffer views.
// CBV, SRV, and UAVs can be created from a buffer.
class Buffer : public GPUResource
{
public:
	struct UploadDesc
	{
		void*  srcData;
		uint32 sizeInBytes;
		uint64 destOffsetInBytes;
	};

	virtual void initialize(const BufferCreateParams& inCreateParams)
	{
		createParams = inCreateParams;
		CHECK(createParams.sizeInBytes > 0);
		// ... subclasses do remaining work
	}

	virtual void writeToGPU(RenderCommandList* commandList, uint32 numUploads, Buffer::UploadDesc* uploadDescs) = 0;
	
	void singleWriteToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes, uint64 destOffsetInBytes)
	{
		UploadDesc desc{ srcData, sizeInBytes, destOffsetInBytes };
		writeToGPU(commandList, 1, &desc);
	}

	inline const BufferCreateParams& getCreateParams() const { return createParams; }

protected:
	BufferCreateParams createParams;
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

	// #wip-texture: A Texture should not internally hold its views.
	virtual RenderTargetView*    getRTV() const = 0;
	virtual ShaderResourceView*  getSRV() const = 0;
	virtual DepthStencilView*    getDSV() const = 0;

	// Element index in the descriptor heap from which the descriptor was created.
	virtual uint32 getSRVDescriptorIndex() const = 0;
	virtual uint32 getRTVDescriptorIndex() const = 0;
	virtual uint32 getDSVDescriptorIndex() const = 0;

	virtual DescriptorHeap* getSourceSRVHeap() const = 0;
	virtual DescriptorHeap* getSourceRTVHeap() const = 0;
	virtual DescriptorHeap* getSourceDSVHeap() const = 0;
};

//////////////////////////////////////////////////////////////////////////
// Raytracing resource

// D3D12_RAYTRACING_GEOMETRY_TYPE
enum class ERaytracingGeometryType
{
	Triangles,
	ProceduralPrimitiveAABB
};

// D3D12_RAYTRACING_GEOMETRY_FLAGS
enum class ERaytracingGeometryFlags : uint32
{
	None                        = 0,
	Opaque                      = 1 << 0,
	NoDuplicateAnyhitInvocation = 1 << 1
};
ENUM_CLASS_FLAGS(ERaytracingGeometryFlags);

// D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC
struct RaytracingGeometryTrianglesDesc
{
	// Assumes this buffer contains a series of 3x4 matrices in compact,
	// So that k-th matrix starts from (48 * k * transformIndex) of this buffer.
	Buffer* transform3x4Buffer = nullptr;
	uint32 transformIndex = 0;

	EPixelFormat indexFormat;
	EPixelFormat vertexFormat;
	uint32 indexCount;
	uint32 vertexCount;
	IndexBuffer* indexBuffer;
	VertexBuffer* vertexBuffer;
};

// D3D12_RAYTRACING_GEOMETRY_DESC
struct RaytracingGeometryDesc
{
	ERaytracingGeometryType type;
	ERaytracingGeometryFlags flags;
	union
	{
		RaytracingGeometryTrianglesDesc triangles;
		// #todo-dxr: RaytracingGeometryAABBsDesc
	};
};

// D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC
struct BLASInstanceInitDesc
{
	BLASInstanceInitDesc()
	{
		::memset(instanceTransform, 0, sizeof(instanceTransform));
		instanceTransform[0][0] = 1.0f;
		instanceTransform[1][1] = 1.0f;
		instanceTransform[2][2] = 1.0f;
	}

	std::vector<RaytracingGeometryDesc> geomDescs;
	float instanceTransform[3][4];
};

struct BLASInstanceUpdateDesc
{
	uint32 blasIndex;
	float instanceTransform[3][4];
};

class AccelerationStructure : public GPUResource
{
public:
	virtual ~AccelerationStructure() = default;

	virtual void rebuildTLAS(
		RenderCommandList* commandList,
		uint32 numInstanceUpdates,
		const BLASInstanceUpdateDesc* updateDescs) = 0;

	virtual ShaderResourceView* getSRV() const = 0;
};
