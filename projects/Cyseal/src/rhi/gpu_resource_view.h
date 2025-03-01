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

struct Texture3DSRVDesc
{
	uint32 mostDetailedMip = 0;
	uint32 mipLevels       = (uint32)(-1);
	float minLODClamp      = 0.0f;
};

// D3D12_TEXCUBE_SRV
struct TextureCubeSRVDesc
{
	uint32 mostDetailedMip = 0;
	uint32 mipLevels       = (uint32)(-1);
	float minLODClamp      = 0.0f;
};

// D3D12_SHADER_RESOURCE_VIEW_DESC
struct ShaderResourceViewDesc
{
	EPixelFormat format;
	ESRVDimension viewDimension;
	// #todo-rhi: UINT Shader4ComponentMapping
	union
	{
		// #todo-rhi: Support all SRV descs. (See D3D12_SHADER_RESOURCE_VIEW_DESC)
		BufferSRVDesc                    buffer;
		//Texture1DSRVDesc                 texture1D;
		//Texture1DArraySRVDesc            texture1DArray;
		Texture2DSRVDesc                 texture2D;
		//Texture2DArraySRVDesc            texture2DArray;
		//Texture2DMultisampleSRVDesc      texture2DMS;
		//Texture2DMultisampleArraySRVDesc texture2DMSArray;
		Texture3DSRVDesc                 texture3D;
		TextureCubeSRVDesc               textureCube;
		//TextureCubeArraySRVDesc          textureCubeArray;
		//RaytracingAccelStructSRVDesc     raytracingAccelStruct;
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
		// #todo-rhi: Texture1DUAVDesc, Texture1DArrayUAVDesc, Texture2DArrayUAVDesc, Texture3DUAVDesc
	};
};

//////////////////////////////////////////////////////////////////////////
// RenderTargetView create info

enum class ERTVDimension
{
	Unknown          = 0,
	Buffer           = 1,
	Texture1D        = 2,
	Texture1DArray   = 3,
	Texture2D        = 4,
	Texture2DArray   = 5,
	Texture2DMS      = 6,
	Texture2DMSArray = 7,
	Texture3D        = 8,
};

struct Texture2DRTVDesc
{
	uint32 mipSlice   = 0;
	uint32 planeSlice = 0;
};

struct RenderTargetViewDesc
{
	EPixelFormat format;
	ERTVDimension viewDimension;
	union
	{
		// #todo-rhi: Other RTV descs
		Texture2DRTVDesc texture2D;
	};
};

//////////////////////////////////////////////////////////////////////////
// DepthStencilView create info

// D3D12_DSV_DIMENSION
enum class EDSVDimension
{
	Unknown          = 0,
	Texture1D        = 1,
	Texture1DArray   = 2,
	Texture2D        = 3,
	Texture2DArray   = 4,
	Texture2DMS      = 5,
	Texture2DMSArray = 6,
};

enum class EDSVFlags
{
	None        = 0,
	OnlyDepth   = 0x1,
	OnlyStencil = 0x2,
};
ENUM_CLASS_FLAGS(EDSVFlags);

struct Texture2DDSVDesc
{
	uint32 mipSlice = 0;
};

struct DepthStencilViewDesc
{
	EPixelFormat  format;
	EDSVDimension viewDimension;
	EDSVFlags     flags;
	union
	{
		Texture2DDSVDesc texture2D;
	};
};

//////////////////////////////////////////////////////////////////////////
// View wrapper classes

class RenderTargetView
{
public:
	RenderTargetView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex)
		: ownerResource(inOwner)
		, sourceHeap(inSourceHeap)
		, descriptorIndex(inDescriptorIndex)
	{
	}

	virtual ~RenderTargetView();

	DescriptorHeap* getSourceHeap() const { return sourceHeap; }
	uint32 getDescriptorIndexInHeap() const { return descriptorIndex; }

protected:
	GPUResource* ownerResource;
	DescriptorHeap* sourceHeap;
	uint32 descriptorIndex;
};

class DepthStencilView
{
public:
	DepthStencilView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex)
		: ownerResource(inOwner)
		, sourceHeap(inSourceHeap)
		, descriptorIndex(inDescriptorIndex)
	{}

	virtual ~DepthStencilView();

	DescriptorHeap* getSourceHeap() const { return sourceHeap; }
	uint32 getDescriptorIndexInHeap() const { return descriptorIndex; }

protected:
	GPUResource* ownerResource;
	DescriptorHeap* sourceHeap;
	uint32 descriptorIndex;
};

class ShaderResourceView
{
public:
	ShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex)
		: ownerResource(inOwner)
		, sourceHeap(inSourceHeap)
		, descriptorIndex(inDescriptorIndex)
	{}
	
	virtual ~ShaderResourceView();

	DescriptorHeap* getSourceHeap() const { return sourceHeap; }
	uint32 getDescriptorIndexInHeap() const { return descriptorIndex; }

	inline void temp_markNoSourceHeap() { bNoSourceHeap = true; }

protected:
	GPUResource* ownerResource;
	DescriptorHeap* sourceHeap;
	uint32 descriptorIndex;

	bool bNoSourceHeap = false; // #todo-rhi: Temp hack for AccelerationStructure
};

class UnorderedAccessView
{
public:
	UnorderedAccessView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex)
		: ownerResource(inOwner)
		, sourceHeap(inSourceHeap)
		, descriptorIndex(inDescriptorIndex)
	{}

	virtual ~UnorderedAccessView();

	DescriptorHeap* getSourceHeap() const { return sourceHeap; }
	uint32 getDescriptorIndexInHeap() const { return descriptorIndex; }

protected:
	GPUResource* ownerResource;
	DescriptorHeap* sourceHeap;
	uint32 descriptorIndex;
};

// #todo-rhi: Why only CBV has no default implementation?
class ConstantBufferView
{
public:
	virtual ~ConstantBufferView() = default;

	virtual void writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes) = 0;

	virtual DescriptorHeap* getSourceHeap() const = 0;
	virtual uint32 getDescriptorIndexInHeap() const = 0;
};
