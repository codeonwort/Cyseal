#pragma once

#include "core/int_types.h"
#include "core/assertion.h"

// NOTE: Should modify following places when adding a new enum:
// - getPixelFormatBytes()
// - isDepthStencilFormat()
// - into_d3d::pixelFormat()
// - into_vk::pixelFormat()
enum class EPixelFormat : uint8
{
	UNKNOWN,

	// TYPELESS
	R32_TYPELESS,
	R24G8_TYPELESS,
	R24_UNORM_X8_TYPELESS,
	R32G8X24_TYPELESS,
	R32_FLOAT_X8X24_TYPELESS,

	// UNORM
	R8G8B8A8_UNORM,
	B8G8R8A8_UNORM,
	
	// FLOAT
	R32_FLOAT,
	R32G32_FLOAT,
	R32G32B32_FLOAT,
	R32G32B32A32_FLOAT,
	R16G16B16A16_FLOAT,
	R16G16_FLOAT,
	R16_FLOAT,

	// UINT
	R32_UINT,
	R16_UINT,
	R32G32B32A32_UINT,

	// DepthStencil
	D24_UNORM_S8_UINT,
	D32_FLOAT_S8_UINT,
};

inline uint32 getPixelFormatBytes(EPixelFormat format)
{
	// #todo-rhi: Ignore depth formats?
	switch (format)
	{
		case EPixelFormat::R32_TYPELESS             : return 4;
		case EPixelFormat::R24G8_TYPELESS           : return 4;
		case EPixelFormat::R24_UNORM_X8_TYPELESS    : return 4;
		case EPixelFormat::R32G8X24_TYPELESS        : return 8;
		case EPixelFormat::R32_FLOAT_X8X24_TYPELESS : return 8;
		case EPixelFormat::R8G8B8A8_UNORM           : return 4;
		case EPixelFormat::B8G8R8A8_UNORM           : return 4;
		case EPixelFormat::R32_FLOAT                : return 4;
		case EPixelFormat::R32G32_FLOAT             : return 8;
		case EPixelFormat::R32G32B32_FLOAT          : return 12;
		case EPixelFormat::R32G32B32A32_FLOAT       : return 16;
		case EPixelFormat::R16G16B16A16_FLOAT       : return 8;
		case EPixelFormat::R16G16_FLOAT             : return 4;
		case EPixelFormat::R16_FLOAT                : return 2;
		case EPixelFormat::R32_UINT                 : return 4;
		case EPixelFormat::R16_UINT                 : return 2;
		case EPixelFormat::R32G32B32A32_UINT        : return 16;
		default: CHECK_NO_ENTRY();
	}
	return 0;
}

inline bool isDepthStencilFormat(EPixelFormat format)
{
	switch (format)
	{
		case EPixelFormat::D24_UNORM_S8_UINT:
		case EPixelFormat::D32_FLOAT_S8_UINT:
			return true;
	}
	return false;
}
