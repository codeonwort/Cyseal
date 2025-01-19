#pragma once

#include "pipeline_state.h"
#include "gpu_resource_view.h"
#include "descriptor_heap.h"
#include "core/int_types.h"
#include "util/enum_util.h"

#include <string>

// Common interface for DX12 root signature and Vulkan descriptor set.
// NOTE: Just direct wrapping of d3d12 structs. Needs complete rewrite for Vulkan.

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
