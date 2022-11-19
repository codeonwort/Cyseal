#pragma once

#include "core/int_types.h"
#include "util/enum_util.h"
#include "pixel_format.h"

class RenderDevice;
class DescriptorHeap;
class GPUResource;

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
public:
	ShaderResourceView(GPUResource* inOwner) : ownerResource(inOwner) {}
	virtual ~ShaderResourceView() = default;

	// #todo-wip-rt
	//virtual DescriptorHeap* getSourceHeap() = 0;
	//virtual uint32 getDescriptorIndexInHeap() const = 0;

protected:
	GPUResource* ownerResource = nullptr;
};

class UnorderedAccessView
{
public:
	UnorderedAccessView(GPUResource* inOwner) : ownerResource(inOwner) {}
	virtual ~UnorderedAccessView() = default;

protected:
	GPUResource* ownerResource = nullptr;
};

class ConstantBufferView
{
public:
	virtual ~ConstantBufferView() = default;

	virtual void upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex) = 0;

	virtual DescriptorHeap* getSourceHeap() = 0;
	virtual uint32 getDescriptorIndexInHeap(uint32 bufferingIndex) const = 0;
};
