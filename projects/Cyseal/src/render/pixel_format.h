#pragma once

#include "core/int_types.h"
#include "core/assertion.h"

enum class EPixelFormat : uint8
{
	UNKNOWN,

	// TYPELESS
	R32_TYPELESS,

	// UNORM
	R8G8B8A8_UNORM,
	
	// FLOAT
	R32G32_FLOAT,
	R32G32B32_FLOAT,
	R32G32B32A32_FLOAT,

	// UINT
	R32_UINT,
	R16_UINT,

	// DepthStencil
	D24_UNORM_S8_UINT
};

inline uint32 getPixelFormatBytes(EPixelFormat format)
{
	switch (format)
	{
		case EPixelFormat::R32_TYPELESS       : return 4;
		case EPixelFormat::R8G8B8A8_UNORM     : return 4;
		case EPixelFormat::R32G32_FLOAT       : return 8;
		case EPixelFormat::R32G32B32_FLOAT    : return 12;
		case EPixelFormat::R32G32B32A32_FLOAT : return 16;
		case EPixelFormat::R32_UINT           : return 4;
		case EPixelFormat::R16_UINT           : return 2;
		default: CHECK_NO_ENTRY();
	}
	return 0;
}
