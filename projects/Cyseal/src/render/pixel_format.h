#pragma once

#include <stdint.h>

enum class EPixelFormat : uint8_t
{
	UNKNOWN,

	// TYPELESS
	R32_TYPELESS,

	// UNORM
	R8G8B8A8_UNORM,
	
	// FLOAT
	R32G32B32_FLOAT,
	R32G32B32A32_FLOAT,

	// UINT
	R32_UINT,
	R16_UINT,

	// DepthStencil
	D24_UNORM_S8_UINT
};
