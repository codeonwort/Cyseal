#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <assert.h>

#define HR(x) if (FAILED(x)) assert(0);
#define check(x) assert(x);

using namespace Microsoft;

WRL::ComPtr<ID3D12Resource> createDefaultBuffer(
	ID3D12Device*					device,
	ID3D12GraphicsCommandList*		commandList,
	const void*						initData,
	UINT64							byteSize,
	WRL::ComPtr<ID3D12Resource>&	uploadBuffer);
