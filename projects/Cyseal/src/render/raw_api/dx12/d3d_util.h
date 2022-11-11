#pragma once

#include <wrl.h>

// #todo-dx12: d3dx12.h should be included prior to any d3d headers.
// 
// Also... I don't know which version of d3dx12.h is proper for current Windows Kit?
// d3dx12.h in https://github.com/microsoft/DirectX-Headers is too up to date
// that it relies on typedefs within the latest d3d12.h.
// 
// Or should I use Agility SDK? (https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/)
#include <d3dx12.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <assert.h>

using namespace Microsoft;

#define HR(x) if (FAILED(x)) { assert(0); }

class D3DDevice* getD3DDevice();
