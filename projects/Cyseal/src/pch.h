#pragma once

// STL
#include <vector>
#include <string>
#include <map>

#include "core/types.h"
#include "core/cymath.h"
#include "core/smart_pointer.h"
#include "core/assertion.h"

#include "util/enum_util.h"

// Preprocessor Defintion
#if COMPILE_BACKEND_DX12
	#include <wrl.h>
	#include <d3dx12.h>
	#include <d3d12.h>
	#include <dxgi1_6.h>
	#include <d3dcommon.h>
#endif
