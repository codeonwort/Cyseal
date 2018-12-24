#pragma once

#include <stdint.h>

enum class EPrimitiveTopology : uint8_t
{
	UNDEFINED         = 0,
	POINTLIST         = 1,
	LINELIST          = 2,
	LINESTRIP         = 3,
	TRIANGLELIST      = 4,
	TRIANGLESTRIP     = 5,
	LINELIST_ADJ      = 10,
	LINESTRIP_ADJ     = 11,
	TRIANGLELIST_ADJ  = 12,
	TRIANGLESTRIP_ADJ = 13
};

class PipelineState
{
};

class RootSignature
{
};
