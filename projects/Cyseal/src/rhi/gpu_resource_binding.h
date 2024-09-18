#pragma once

#include "core/int_types.h"
#include "util/enum_util.h"
#include "pipeline_state.h"
#include "descriptor_heap.h"

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

// D3D12_ROOT_PARAMETER_TYPE
// VkDescriptorType
enum class ERootParameterType : uint8
{
	DescriptorTable = 0,
	Constants32Bit  = 1,
	CBV             = 2,
	SRVBuffer       = 3,
	UAVBuffer       = 4,
	SRVImage        = 5,
	UAVImage        = 6,
};

// D3D12_DESCRIPTOR_RANGE_TYPE
enum class EDescriptorRangeType : uint8
{
	SRV     = 0,
	UAV     = 1,
	CBV     = 2,
	SAMPLER = 3
};

// D3D12_DESCRIPTOR_RANGE
struct DescriptorRange
{
	EDescriptorRangeType rangeType;
	uint32 numDescriptors;
	uint32 baseShaderRegister;
	uint32 registerSpace;
	uint32 offsetInDescriptorsFromTableStart;

	inline void init(
		EDescriptorRangeType inRangeType,
		uint32 inNumDescriptors,
		uint32 inBaseShaderRegister,
		uint32 inRegisterSpace = 0,
		// 0xffffffff = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
		uint32 inOffsetDescriptorsFromTableStart = 0xffffffff)
	{
		rangeType                         = inRangeType;
		numDescriptors                    = inNumDescriptors;
		baseShaderRegister                = inBaseShaderRegister;
		registerSpace                     = inRegisterSpace;
		offsetInDescriptorsFromTableStart = inOffsetDescriptorsFromTableStart;
	}
};

// D3D12_ROOT_DESCRIPTOR_TABLE
struct RootDescriptorTable
{
	uint32 numDescriptorRanges;
	const DescriptorRange* descriptorRanges;
};

// D3D12_ROOT_CONSTANTS
struct RootConstants
{
	uint32 shaderRegister;
	uint32 registerSpace;
	uint32 num32BitValues;
};

// D3D12_ROOT_DESCRIPTOR
struct RootDescriptor
{
	uint32 shaderRegister;
	uint32 registerSpace;
};

// D3D12_ROOT_PARAMETER
struct RootParameter
{
	ERootParameterType parameterType;
	union
	{
		RootDescriptorTable descriptorTable;
		RootConstants constants;
		RootDescriptor descriptor;
	};
	EShaderVisibility shaderVisibility;

	void initAsDescriptorTable(
		uint32 numDescriptorRanges,
		const DescriptorRange* descriptorRanges)
	{
		parameterType = ERootParameterType::DescriptorTable;
		{
			descriptorTable.numDescriptorRanges = numDescriptorRanges;
			descriptorTable.descriptorRanges = descriptorRanges;
		}
		shaderVisibility = EShaderVisibility::All;
	}
	void initAsSRVBuffer(
		uint32 shaderRegister,
		uint32 registerSpace)
	{
		parameterType = ERootParameterType::SRVBuffer;
		{
			descriptor.shaderRegister = shaderRegister;
			descriptor.registerSpace = registerSpace;
		}
		shaderVisibility = EShaderVisibility::All;
	}
	void initAsUAVBuffer(
		uint32 shaderRegister,
		uint32 registerSpace)
	{
		parameterType = ERootParameterType::UAVBuffer;
		{
			descriptor.shaderRegister = shaderRegister;
			descriptor.registerSpace = registerSpace;
		}
		shaderVisibility = EShaderVisibility::All;
	}
	void initAsSRVImage(
		uint32 shaderRegister,
		uint32 registerSpace)
	{
		parameterType = ERootParameterType::SRVImage;
		{
			descriptor.shaderRegister = shaderRegister;
			descriptor.registerSpace = registerSpace;
		}
		shaderVisibility = EShaderVisibility::All;
	}
	void initAsUAVImage(
		uint32 shaderRegister,
		uint32 registerSpace)
	{
		parameterType = ERootParameterType::UAVImage;
		{
			descriptor.shaderRegister = shaderRegister;
			descriptor.registerSpace = registerSpace;
		}
		shaderVisibility = EShaderVisibility::All;
	}
	void initAsConstants(
		uint32 shaderRegister,
		uint32 registerSpace,
		uint32 num32BitValues)
	{
		parameterType = ERootParameterType::Constants32Bit;
		{
			constants.shaderRegister = shaderRegister;
			constants.registerSpace = registerSpace;
			constants.num32BitValues = num32BitValues;
		}
		shaderVisibility = EShaderVisibility::All;
	}
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

// D3D12_ROOT_SIGNATURE_FLAGS
enum class ERootSignatureFlags : uint32
{
	None                            = 0,
	AllowInputAssemblerInputLayout  = 0x1,
	DenyVertexShaderRootAccess      = 0x2,
	DenyHullShaderRootAccess        = 0x4,
	DenyDomainShaderRootAccess      = 0x8,
	DenyGeometryShaderRootAccess    = 0x10,
	DenyPixelShaderRootAccess       = 0x20,
	AllowStreamOutput               = 0x40,
	LocalRootSignature              = 0x80,
	DenyAmplicationShaderRootAccess = 0x100,
	DenyMeshShaderRootAccess        = 0x200,
	CbvSrvUavHeapDirectlyIndexed    = 0x400,
	SamplerHeapDirectlyIndexed      = 0x800,
};
ENUM_CLASS_FLAGS(ERootSignatureFlags);

// D3D12_ROOT_SIGNATURE_DESC
struct RootSignatureDesc
{
	RootSignatureDesc(
		uint32 inNumParameters                      = 0
		, const RootParameter* inParameters         = nullptr
		, uint32 inNumStaticSamplers                = 0
		, const StaticSamplerDesc* inStaticSamplers = nullptr
		, ERootSignatureFlags inFlags               = ERootSignatureFlags::None)
	{
		numParameters = inNumParameters;
		parameters = inParameters;
		numStaticSamplers = inNumStaticSamplers;
		staticSamplers = inStaticSamplers;
		flags = inFlags;
	}

	uint32 numParameters;
	const RootParameter* parameters;
	uint32 numStaticSamplers;
	const StaticSamplerDesc* staticSamplers;
	ERootSignatureFlags flags;
};

// https://docs.microsoft.com/en-us/windows/win32/direct3d12/root-signatures-overview
// ID3D12RootSignature
// VkPipelineLayout
// - Defines resource binding for drawcall.
// - It's a collection of root parameters.
// - A root parameter is one of root constant, root descirptor, or descriptor table.
class RootSignature
{
public:
	virtual ~RootSignature() = default;
};

// -----------------------------------------------------------------------

struct ShaderParameterTable
{
	using ParameterName = const char*;
	struct PushConstant       { std::string name; uint32 value; uint32 destOffsetIn32BitValues; };
	struct ConstantBuffer     { std::string name; ConstantBufferView* buffer; };
	struct StructuredBuffer   { std::string name; ShaderResourceView* buffer; };
	struct RWBuffer           { std::string name; UnorderedAccessView* buffer; };
	struct RWStructuredBuffer { std::string name; UnorderedAccessView* buffer; };
	struct Texture            { std::string name; ShaderResourceView* texture; };
	struct RWTexture          { std::string name; UnorderedAccessView* texture; };

public:
	void pushConstant(ParameterName name, uint32 value, uint32 destOffsetIn32BitValues = 0) { pushConstants.emplace_back(PushConstant{ name, value, destOffsetIn32BitValues }); }
	void constantBuffer(ParameterName name, ConstantBufferView* buffer)                     { constantBuffers.emplace_back(ConstantBuffer{ name, buffer });                     }
	void structuredBuffer(ParameterName name, ShaderResourceView* buffer)                   { structuredBuffers.emplace_back(StructuredBuffer{ name, buffer });                 }
	void rwBuffer(ParameterName name, UnorderedAccessView* buffer)                          { rwBuffers.emplace_back(RWBuffer{ name, buffer });                                 }
	void rwStructuredBuffer(ParameterName name, UnorderedAccessView* buffer)                { rwStructuredBuffers.emplace_back(RWStructuredBuffer{ name, buffer });             }
	void texture(ParameterName name, ShaderResourceView* texture)                           { textures.emplace_back(Texture{ name, texture });                                  }
	void rwTexture(ParameterName name, UnorderedAccessView* texture)                        { rwTextures.emplace_back(RWTexture{ name, texture });                              }

	size_t totalParameters() const
	{
		return pushConstants.size() + constantBuffers.size()
			+ structuredBuffers.size() + rwBuffers.size() + rwStructuredBuffers.size()
			+ textures.size() + rwTextures.size();
	}

public:
	std::vector<PushConstant>       pushConstants;
	std::vector<ConstantBuffer>     constantBuffers;
	std::vector<StructuredBuffer>   structuredBuffers;
	std::vector<RWBuffer>           rwBuffers;
	std::vector<RWStructuredBuffer> rwStructuredBuffers;
	std::vector<Texture>            textures;
	std::vector<RWTexture>          rwTextures;
};
