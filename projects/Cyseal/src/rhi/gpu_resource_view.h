#pragma once

#include "core/int_types.h"
#include "util/enum_util.h"
#include "pixel_format.h"

class RenderDevice;
class DescriptorHeap;
class GPUResource;
class RenderCommandList;

//////////////////////////////////////////////////////////////////////////
// ShaderResourceView create info

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
		// #todo-renderdevice: Other fields (tex1d, tex2darray, tex3d, texcube, ..)
		// See D3D12_SHADER_RESOURCE_VIEW_DESC for full list.
		Texture2DSRVDesc texture2D;
	};
};

//////////////////////////////////////////////////////////////////////////
// UnorderedAccessView create info

// D3D12_UAV_DIMENSION
enum class EUAVDimension
{
	Unknown,
	Buffer,
	Texture1D,
	Texture1DArray,
	Texture2D,
	Texture2DArray,
	Texture3D
};

// D3D12_BUFFER_UAV_FLAGS
enum class EBufferUAVFlags : uint8
{
	None = 0,
	Raw  = 1 << 0,
};
ENUM_CLASS_FLAGS(EBufferUAVFlags);

// D3D12_BUFFER_UAV
struct BufferUAVDesc
{
	uint64 firstElement;
	uint32 numElements;
	uint32 structureByteStride;
	uint64 counterOffsetInBytes;
	EBufferUAVFlags flags;
};

// D3D12_TEX2D_UAV
struct Texture2DUAVDesc
{
	uint32 mipSlice   = 0;
	uint32 planeSlice = 0;
};

// D3D12_UNORDERED_ACCESS_VIEW_DESC
struct UnorderedAccessViewDesc
{
	EPixelFormat format;
	EUAVDimension viewDimension;
	union
	{
		BufferUAVDesc buffer;
		Texture2DUAVDesc texture2D;
		// #todo-dx12: Texture1DUAVDesc, Texture1DArrayUAVDesc, Texture2DArrayUAVDesc, Texture3DUAVDesc
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
	ShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex)
		: ownerResource(inOwner)
		, sourceHeap(inSourceHeap)
		, descriptorIndex(inDescriptorIndex)
	{}
	
	virtual ~ShaderResourceView() = default;

	DescriptorHeap* getSourceHeap() const { return sourceHeap; }
	uint32 getDescriptorIndexInHeap() const { return descriptorIndex; }

protected:
	GPUResource* ownerResource;
	DescriptorHeap* sourceHeap;
	uint32 descriptorIndex;
};

class UnorderedAccessView
{
public:
	UnorderedAccessView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex)
		: ownerResource(inOwner)
		, sourceHeap(inSourceHeap)
		, descriptorIndex(inDescriptorIndex)
	{}

	virtual ~UnorderedAccessView() = default;

	DescriptorHeap* getSourceHeap() const { return sourceHeap; }
	uint32 getDescriptorIndexInHeap() const { return descriptorIndex; }

protected:
	GPUResource* ownerResource;
	DescriptorHeap* sourceHeap;
	uint32 descriptorIndex;
};

class ConstantBufferView
{
public:
	virtual ~ConstantBufferView() = default;

	virtual void writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes) = 0;

	virtual DescriptorHeap* getSourceHeap() = 0;
	virtual uint32 getDescriptorIndexInHeap() const = 0;
};
