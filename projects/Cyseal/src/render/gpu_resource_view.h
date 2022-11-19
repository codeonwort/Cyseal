#pragma once

#include "core/int_types.h"
#include "util/enum_util.h"
#include "pixel_format.h"

class RenderDevice;
class DescriptorHeap;
class VertexBuffer;
class Texture;
class IndexBuffer;
class StructuredBuffer;
class AccelerationStructure;

//////////////////////////////////////////////////////////////////////////
// View create infos

// #todo-wip-rt: Create SRVs with these structs.
// A buffer or texture object should not hold their views internally.

// D3D12_SRV_DIMENSION
enum class ESRVDimension
{
	Unknown,
	Buffer,
	Texture1D,
	Texture1DArray,
	Texture2D,
	Texture2DArray,
	Texture2DMultiSampled,
	Texture2DMultiSampledArray,
	Texture3D,
	TextureCube,
	TextureCubeArray,
	RaytracingAccelerationStructure
};

// D3D12_BUFFER_SRV_FLAGS
enum class EBufferSRVFlags : uint8
{
	None = 0,
	Raw  = 1 << 0,
};
ENUM_CLASS_FLAGS(EBufferSRVFlags);

// D3D12_BUFFER_SRV
struct BufferSRVDesc
{
	uint64 firstElement;
	uint32 numElements;
	uint32 structureByteStride;
	EBufferSRVFlags flags;
};

// D3D12_TEX2D_SRV
struct Texture2DSRVDesc
{
	uint32 mostDetailedMip = 0;
	uint32 mipLevels       = (uint32)(-1);
	uint32 planeSlice      = 0;
	float minLODClamp      = 0.0f;
};

// D3D12_SHADER_RESOURCE_VIEW_DESC
struct ShaderResourceViewDesc
{
	EPixelFormat format;
	ESRVDimension viewDimension;
	// #todo-dx12: UINT Shader4ComponentMapping
	union
	{
		BufferSRVDesc buffer;
		// #todo-wip: Other fields
		Texture2DSRVDesc texture2D;
	};
};

//////////////////////////////////////////////////////////////////////////
// View wrapper classes

class RenderTargetView
{
};

class DepthStencilView
{
};

class ShaderResourceView
{
protected:
	// #todo-wip-rt: At least merge structured/index/vertex buffers...
	enum class ESource { Texture, StructuredBuffer, AccelerationStructure, IndexBuffer, VertexBuffer };
public:
	ShaderResourceView(Texture* inOwner)
		: ownerTexture(inOwner)
		, source(ESource::Texture)
	{}
	ShaderResourceView(StructuredBuffer* inOwner)
		: ownerStructuredBuffer(inOwner)
		, source(ESource::StructuredBuffer)
	{}
	ShaderResourceView(AccelerationStructure* inOwner)
		: ownerAccelStruct(inOwner)
		, source(ESource::AccelerationStructure)
	{}
	ShaderResourceView(IndexBuffer* inOwner)
		: ownerIndexBuffer(inOwner)
		, source(ESource::IndexBuffer)
	{}
	ShaderResourceView(VertexBuffer* inOwner)
		: ownerVertexBuffer(inOwner)
		, source(ESource::VertexBuffer)
	{}
	virtual ~ShaderResourceView() = default;

	// #todo-wip-rt
	//virtual DescriptorHeap* getSourceHeap() = 0;
	//virtual uint32 getDescriptorIndexInHeap() const = 0;

protected:
	ESource source;
	Texture* ownerTexture = nullptr;
	StructuredBuffer* ownerStructuredBuffer = nullptr;
	AccelerationStructure* ownerAccelStruct = nullptr;
	IndexBuffer* ownerIndexBuffer = nullptr;
	VertexBuffer* ownerVertexBuffer = nullptr;
};

class UnorderedAccessView
{
protected:
	// #todo-wip-rt: Same problem with ShaderResourceView::ESource
	enum class ESource { Texture, StructuredBuffer };
public:
	UnorderedAccessView(Texture* inOwner)
		: ownerTexture(inOwner)
		, source(ESource::Texture)
	{}
	UnorderedAccessView(StructuredBuffer* inOwner)
		: ownerStructuredBuffer(inOwner)
		, source(ESource::StructuredBuffer)
	{}
protected:
	ESource source;
	Texture* ownerTexture = nullptr;
	StructuredBuffer* ownerStructuredBuffer = nullptr;
};

class ConstantBufferView
{
public:
	virtual ~ConstantBufferView() = default;

	virtual void upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex) = 0;

	virtual DescriptorHeap* getSourceHeap() = 0;
	virtual uint32 getDescriptorIndexInHeap(uint32 bufferingIndex) const = 0;
};
