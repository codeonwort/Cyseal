#pragma once

#include <wrl.h>

#include <d3dx12.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <assert.h>

using namespace Microsoft;

#define HR(x) if (FAILED(x)) { assert(0); }

class D3DDevice* getD3DDevice();
