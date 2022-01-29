#pragma once

#include "core/int_types.h"
#include "util/enum_util.h"
#include "pipeline_state.h"

// Common interface for DX12 root signature and Vulkan descriptor set.
// NOTE 1: This file might be merged into another file.
// NOTE 2: Just direct wrapping of d3d12 structs. Needs complete rewrite for Vulkan.

// D3D12_SHADER_VISIBILITY
enum class EShaderVisibility : uint8
{
	All      = 0,
	Vertex   = 1,
	Hull     = 2,
	Domain   = 3,
	Geometry = 4,
	Pixel    = 5
};

// D3D12_ROOT_PARAMETER_TYPE
enum class ERootParameterType : uint8
{
	DescriptorTable = 0,
	Constants32Bit  = 1,
	CBV             = 2,
	SRV             = 3,
	UAV             = 4
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
	MIN_MAG_MIP_POINT = 0,
	MIN_MAG_POINT_MIP_LINEAR = 0x1,
	MIN_POINT_MAG_LINEAR_MIP_POINT = 0x4,
	MIN_POINT_MAG_MIP_LINEAR = 0x5,
	MIN_LINEAR_MAG_MIP_POINT = 0x10,
	MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11,
	MIN_MAG_LINEAR_MIP_POINT = 0x14,
	MIN_MAG_MIP_LINEAR = 0x15,
	ANISOTROPIC = 0x55,
	COMPARISON_MIN_MAG_MIP_POINT = 0x80,
	COMPARISON_MIN_MAG_POINT_MIP_LINEAR = 0x81,
	COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x84,
	COMPARISON_MIN_POINT_MAG_MIP_LINEAR = 0x85,
	COMPARISON_MIN_LINEAR_MAG_MIP_POINT = 0x90,
	COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x91,
	COMPARISON_MIN_MAG_LINEAR_MIP_POINT = 0x94,
	COMPARISON_MIN_MAG_MIP_LINEAR = 0x95,
	COMPARISON_ANISOTROPIC = 0xd5,
	MINIMUM_MIN_MAG_MIP_POINT = 0x100,
	MINIMUM_MIN_MAG_POINT_MIP_LINEAR = 0x101,
	MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x104,
	MINIMUM_MIN_POINT_MAG_MIP_LINEAR = 0x105,
	MINIMUM_MIN_LINEAR_MAG_MIP_POINT = 0x110,
	MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x111,
	MINIMUM_MIN_MAG_LINEAR_MIP_POINT = 0x114,
	MINIMUM_MIN_MAG_MIP_LINEAR = 0x115,
	MINIMUM_ANISOTROPIC = 0x155,
	MAXIMUM_MIN_MAG_MIP_POINT = 0x180,
	MAXIMUM_MIN_MAG_POINT_MIP_LINEAR = 0x181,
	MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x184,
	MAXIMUM_MIN_POINT_MAG_MIP_LINEAR = 0x185,
	MAXIMUM_MIN_LINEAR_MAG_MIP_POINT = 0x190,
	MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x191,
	MAXIMUM_MIN_MAG_LINEAR_MIP_POINT = 0x194,
	MAXIMUM_MIN_MAG_MIP_LINEAR = 0x195,
	MAXIMUM_ANISOTROPIC = 0x1d5
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
	ETextureFilter filter;
	ETextureAddressMode addressU;
	ETextureAddressMode addressV;
	ETextureAddressMode addressW;
	float mipLODBias;
	uint32 maxAnisotropy;
	EComparisonFunc comparisonFunc;
	EStaticBorderColor borderColor;
	float minLOD;
	float maxLOD;
	uint32 shaderRegister;
	uint32 registerSpace;
	EShaderVisibility shaderVisibility;
};

// D3D12_ROOT_SIGNATURE_FLAGS
enum class ERootSignatureFlags : uint8
{
	None                           = 0,
	AllowInputAssemblerInputLayout = 0x1,
	DenyVertexShaderRootAccess     = 0x2,
	DenyHullShaderRootAccess       = 0x4,
	DenyDomainShaderRootAccess     = 0x8,
	DenyGeometryShaderRootAccess   = 0x10,
	DenyPixelShaderRootAccess      = 0x20,
	AllowStreamOutput              = 0x40,
	LocalRootSignature             = 0x80
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
// - Defines resource binding for drawcall.
// - It's a collection of root parameters.
// - A root parameter is one of root constant, root descirptor, or descriptor table.
class RootSignature
{
};

// ----------------------------------------------------------------------------
// Descriptor Heap

// D3D12_DESCRIPTOR_HEAP_TYPE
enum class EDescriptorHeapType : uint8
{
	CBV_SRV_UAV = 0,
	SAMPLER     = 1,
	RTV         = 2,
	DSV         = 3,
	NUM_TYPES   = 4
};

// D3D12_DESCRIPTOR_HEAP_FLAGS
enum class EDescriptorHeapFlags : uint8
{
	None          = 0,
	ShaderVisible = 1,
};

// D3D12_DESCRIPTOR_HEAP_DESC
struct DescriptorHeapDesc
{
	EDescriptorHeapType type   = EDescriptorHeapType::NUM_TYPES;
	uint32 numDescriptors      = 0;
	EDescriptorHeapFlags flags = EDescriptorHeapFlags::None;
	uint32 nodeMask            = 0;
};

class DescriptorHeap
{
	//
};
