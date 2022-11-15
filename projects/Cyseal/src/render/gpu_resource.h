#pragma once

#include "core/types.h"
#include "util/enum_util.h"
#include "pixel_format.h"

class VertexBufferPool;
class IndexBufferPool;
class ConstantBufferView;
class ShaderResourceView;
class UnorderedAccessView;
class DescriptorHeap;
class RenderCommandList;

// GPU Resources = Buffers + Textures
// #todo: merge with texture.h?

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

// For StructuredBuffer
enum class EBufferAccessFlags : uint32
{
	UAV       = 1 << 0,
	CPU_WRITE = 1 << 1,
};
ENUM_CLASS_FLAGS(EBufferAccessFlags);

enum class ETextureDimension : uint8
{
    UNKNOWN = 0,
    TEXTURE1D = 1,
    TEXTURE2D = 2,
    TEXTURE3D = 3
};

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
};

enum class EDepthClearFlags : uint8
{
	DEPTH   = 0x1,
	STENCIL = 0x2,
	DEPTH_STENCIL = DEPTH | STENCIL
};
ENUM_CLASS_FLAGS(EDepthClearFlags);

// #todo: Maybe not needed
// Base class for buffers and textures
// ID3D12Resource
class GPUResource
{
public:
	virtual ~GPUResource() {}
};

// #todo-barrier: There are 3 types of barriers (transition, aliasing, and UAV)
// Only deal with transition barrier for now.
// D3D12_RESOURCE_BARRIER_TYPE
enum class EResourceBarrierType
{
	Transition = 0,
	Aliasing = (Transition + 1),
	UAV = (Aliasing + 1)
};
// D3D12_RESOURCE_BARRIER
struct ResourceBarrier
{
	const EResourceBarrierType type = EResourceBarrierType::Transition;
	// #todo-barrier: Split barrier
	// ...
	GPUResource* resource;
	EGPUResourceState stateBefore;
	EGPUResourceState stateAfter;
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
class VertexBuffer : public GPUResource
{
	friend class VertexBufferPool;
public:
	virtual void initialize(uint32 sizeInBytes) = 0;
	
	virtual void initializeWithinPool(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, uint32 strideInBytes) = 0;

	// #todo-wip-rt: Microsoft DXR sample uses StructuredBuffer for vertex buffer
	// but my vbuf layout is nontrivial.
	// To bind as a ByteAddressBuffer in HLSL.
	virtual ShaderResourceView* getByteAddressView() const = 0;

	virtual uint32 getVertexCount() const = 0;

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

class IndexBuffer : public GPUResource
{
public:
	virtual void initialize(uint32 sizeInBytes, EPixelFormat format) = 0;

	virtual void initializeWithinPool(
		IndexBufferPool* pool,
		uint64 offsetInPool,
		uint32 sizeInBytes) = 0;

	virtual void updateData(RenderCommandList* commandList, void* data, EPixelFormat format) = 0;

	// To bind as a ByteAddressBuffer in HLSL.
	virtual ShaderResourceView* getByteAddressView() const = 0;

	virtual uint32 getIndexCount() const = 0;
	virtual EPixelFormat getIndexFormat() const = 0;

protected:
	// Null if a committed resource.
	IndexBufferPool* parentPool = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Constant buffer memory
// D3D12 committed resource (resource + implicit heap)
class ConstantBuffer : public GPUResource
{
public:
	virtual ~ConstantBuffer() = default;

	virtual void initialize(uint32 sizeInBytes) = 0;

	// #todo-wip: Is it alright to make buffering a built-in feature of CBV?
	//            -> Not good. See base_pass.cpp. Maybe instancing parameter will be better.
	// 'bufferingCount' : Same as the swapchain image count if this CBV will be dynamic per frame.
	// Returns null if out of memory.
	virtual ConstantBufferView* allocateCBV(
		DescriptorHeap* descHeap,
		uint32 sizeInBytes,
		uint32 bufferingCount) = 0;
};

//////////////////////////////////////////////////////////////////////////
// StructuredBuffer

class StructuredBuffer : public GPUResource
{
public:
	virtual void uploadData(
		RenderCommandList* commandList,
		void* data,
		uint32 sizeInBytes,
		uint32 destOffsetInBytes) = 0;

	virtual ShaderResourceView* getSRV() const = 0;
	virtual UnorderedAccessView* getUAV() const = 0;

	// Element index in the descriptor heap from which the descriptor was created.
	virtual uint32 getSRVDescriptorIndex() const = 0;
	virtual uint32 getUAVDescriptorIndex() const = 0;

	virtual DescriptorHeap* getSourceSRVHeap() const = 0;
	virtual DescriptorHeap* getSourceUAVHeap() const = 0;
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
	// #todo-wip-rt: No need to be a *structured* buffer,
	// just currently easiest solution in my clunky abstraction.
	// I have to refactor these buffer classes...
	StructuredBuffer* transform3x4Buffer = nullptr;
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
		// #todo-wip-rt: AABBs
	};
};

class AccelerationStructure
{
public:
	virtual ~AccelerationStructure() = default;

	virtual ShaderResourceView* getSRV() const = 0;
};
