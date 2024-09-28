#pragma once

#include "pipeline_state.h"
#include "gpu_resource_view.h"
#include "descriptor_heap.h"
#include "core/int_types.h"
#include "util/enum_util.h"

#include <string>

// Common interface for DX12 root signature and Vulkan descriptor set.
// NOTE 1: This file might be merged into another file.
// NOTE 2: Just direct wrapping of d3d12 structs. Needs complete rewrite for Vulkan.

// D3D12_SHADER_VISIBILITY
enum class EShaderVisibility : uint8
{
	All      = 0, // Compute always use this; so does RT.
	Vertex   = 1,
	Hull     = 2,
	Domain   = 3,
	Geometry = 4,
	Pixel    = 5
	// #todo-rhi: EShaderVisibility - Amplication, Mesh
};

// D3D12_FILTER
enum class ETextureFilter : uint16
{
	MIN_MAG_MIP_POINT                          = 0,
	MIN_MAG_POINT_MIP_LINEAR                   = 0x1,
	MIN_POINT_MAG_LINEAR_MIP_POINT             = 0x4,
	MIN_POINT_MAG_MIP_LINEAR                   = 0x5,
	MIN_LINEAR_MAG_MIP_POINT                   = 0x10,
	MIN_LINEAR_MAG_POINT_MIP_LINEAR            = 0x11,
	MIN_MAG_LINEAR_MIP_POINT                   = 0x14,
	MIN_MAG_MIP_LINEAR                         = 0x15,
	ANISOTROPIC                                = 0x55,
	COMPARISON_MIN_MAG_MIP_POINT               = 0x80,
	COMPARISON_MIN_MAG_POINT_MIP_LINEAR        = 0x81,
	COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT  = 0x84,
	COMPARISON_MIN_POINT_MAG_MIP_LINEAR        = 0x85,
	COMPARISON_MIN_LINEAR_MAG_MIP_POINT        = 0x90,
	COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x91,
	COMPARISON_MIN_MAG_LINEAR_MIP_POINT        = 0x94,
	COMPARISON_MIN_MAG_MIP_LINEAR              = 0x95,
	COMPARISON_ANISOTROPIC                     = 0xd5,
	MINIMUM_MIN_MAG_MIP_POINT                  = 0x100,
	MINIMUM_MIN_MAG_POINT_MIP_LINEAR           = 0x101,
	MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT     = 0x104,
	MINIMUM_MIN_POINT_MAG_MIP_LINEAR           = 0x105,
	MINIMUM_MIN_LINEAR_MAG_MIP_POINT           = 0x110,
	MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR    = 0x111,
	MINIMUM_MIN_MAG_LINEAR_MIP_POINT           = 0x114,
	MINIMUM_MIN_MAG_MIP_LINEAR                 = 0x115,
	MINIMUM_ANISOTROPIC                        = 0x155,
	MAXIMUM_MIN_MAG_MIP_POINT                  = 0x180,
	MAXIMUM_MIN_MAG_POINT_MIP_LINEAR           = 0x181,
	MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT     = 0x184,
	MAXIMUM_MIN_POINT_MAG_MIP_LINEAR           = 0x185,
	MAXIMUM_MIN_LINEAR_MAG_MIP_POINT           = 0x190,
	MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR    = 0x191,
	MAXIMUM_MIN_MAG_LINEAR_MIP_POINT           = 0x194,
	MAXIMUM_MIN_MAG_MIP_LINEAR                 = 0x195,
	MAXIMUM_ANISOTROPIC                        = 0x1d5
};

// D3D12_TEXTURE_ADDRESS_MODE
enum class ETextureAddressMode : uint8
{
	Wrap       = 1,
	Mirror     = 2,
	Clamp      = 3,
	Border     = 4,
	MirrorOnce = 5
};

// D3D12_STATIC_BORDER_COLOR
enum EStaticBorderColor : uint8
{
	TransparentBlack = 0,
	OpaqueBlack      = 1,
	OpaqueWhite      = 2
};

// D3D12_STATIC_SAMPLER_DESC
struct StaticSamplerDesc
{
	ETextureFilter filter              = ETextureFilter::MIN_MAG_MIP_POINT;
	ETextureAddressMode addressU       = ETextureAddressMode::Wrap;
	ETextureAddressMode addressV       = ETextureAddressMode::Wrap;
	ETextureAddressMode addressW       = ETextureAddressMode::Wrap;
	float mipLODBias                   = 0.0f;
	uint32 maxAnisotropy               = 1;
	EComparisonFunc comparisonFunc     = EComparisonFunc::Always;
	EStaticBorderColor borderColor     = EStaticBorderColor::TransparentBlack;
	float minLOD                       = 0.0f;
	float maxLOD                       = 0.0f;
	uint32 shaderRegister              = 0;
	uint32 registerSpace               = 0;
	EShaderVisibility shaderVisibility = EShaderVisibility::All;
};

// -----------------------------------------------------------------------

struct ShaderParameterTable
{
	using ParameterName = const char*;
	struct PushConstant       { std::string name; uint32 value; uint32 destOffsetIn32BitValues; };
	struct ConstantBuffer     { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct StructuredBuffer   { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct RWBuffer           { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct RWStructuredBuffer { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct ByteAddressBuffer  { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct Texture            { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct RWTexture          { std::string name; DescriptorHeap* sourceHeap; uint32 startIndex; uint32 count; };
	struct AccelerationStruct { std::string name; ShaderResourceView* srv; };

public:
	// These API take a single parameter.
	void pushConstant(ParameterName name, uint32 value, uint32 destOffsetIn32BitValues = 0) { pushConstants.emplace_back(PushConstant{ name, value, destOffsetIn32BitValues }); }
	void constantBuffer(ParameterName name, ConstantBufferView* buffer) { constantBuffers.emplace_back(ConstantBuffer{ name, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap(), 1 }); }
	void structuredBuffer(ParameterName name, ShaderResourceView* buffer) { structuredBuffers.emplace_back(StructuredBuffer{ name, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap(), 1 }); }
	void rwBuffer(ParameterName name, UnorderedAccessView* buffer) { rwBuffers.emplace_back(RWBuffer{ name, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap(), 1 }); }
	void rwStructuredBuffer(ParameterName name, UnorderedAccessView* buffer) { rwStructuredBuffers.emplace_back(RWStructuredBuffer{ name, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap(), 1 }); }
	void byteAddressBuffer(ParameterName name, ShaderResourceView* buffer) { byteAddressBuffers.emplace_back(ByteAddressBuffer{ name, buffer->getSourceHeap(), buffer->getDescriptorIndexInHeap(), 1 }); }
	void texture(ParameterName name, ShaderResourceView* texture) { textures.emplace_back(Texture{ name, texture->getSourceHeap(), texture->getDescriptorIndexInHeap(), 1 }); }
	void rwTexture(ParameterName name, UnorderedAccessView* texture) { rwTextures.emplace_back(RWTexture{ name, texture->getSourceHeap(), texture->getDescriptorIndexInHeap(), 1 }); }
	void accelerationStructure(ParameterName name, ShaderResourceView* accelStruct) { accelerationStructures.emplace_back(AccelerationStruct{ name, accelStruct }); }

	// CAUTION: Use at your own risk. These API directly copy contiguous descriptors in a descriptor heap. No check for resource type.
	void constantBuffer(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { constantBuffers.emplace_back(ConstantBuffer{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }
	void structuredBuffer(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { structuredBuffers.emplace_back(StructuredBuffer{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }
	void rwBuffer(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { rwBuffers.emplace_back(RWBuffer{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }
	void rwStructuredBuffer(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { rwStructuredBuffers.emplace_back(RWStructuredBuffer{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }
	void byteAddressBuffer(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { byteAddressBuffers.emplace_back(ByteAddressBuffer{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }
	void texture(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { textures.emplace_back(Texture{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }
	void rwTexture(ParameterName name, DescriptorHeap* sourceHeap, uint32 firstDescriptorIndex, uint32 descriptorCount) { rwTextures.emplace_back(RWTexture{ name, sourceHeap, firstDescriptorIndex, descriptorCount }); }

	size_t totalParameters() const
	{
		return pushConstants.size() + constantBuffers.size()
			+ structuredBuffers.size() + rwBuffers.size() + rwStructuredBuffers.size()
			+ textures.size() + rwTextures.size()
			+ accelerationStructures.size();
	}

public:
	std::vector<PushConstant>       pushConstants;
	std::vector<ConstantBuffer>     constantBuffers;
	std::vector<StructuredBuffer>   structuredBuffers;
	std::vector<RWBuffer>           rwBuffers;
	std::vector<RWStructuredBuffer> rwStructuredBuffers;
	std::vector<ByteAddressBuffer>  byteAddressBuffers;
	std::vector<Texture>            textures;
	std::vector<RWTexture>          rwTextures;
	std::vector<AccelerationStruct> accelerationStructures;
};
