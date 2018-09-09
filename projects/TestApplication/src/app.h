#pragma once

#include "AppBase.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <d3dx12.h>

#include <wrl.h>
using namespace Microsoft;

// Steps to initialize Direct3D
// 1. D3D12CreateDevice()
// 2. Create a ID3D12Fence and retrieve sizes of descriptors
// 3. Check 4X MSAA support
// 4. Create commond queues, commond list allocators, and main command queue
// 5. Create a swap chain
// 6. Create descriptor heaps for the app
// 7. Set back buffer's dimension and create a RTV for the back buffer
// 8. Create a depth/stencil buffer and its DSV
// 9. Set viewport and scissor rect

class Application : public AppBase
{

private:
	WRL::ComPtr<IDXGIFactory4> dxgiFactory;

	WRL::ComPtr<ID3D12Device> device;

	WRL::ComPtr<ID3D12Fence> fence;
	UINT currentFence;

	UINT descSizeRTV;
	UINT descSizeDSV;
	UINT descSizeCBV_SRV_UAV;
	UINT quality4xMSAA;
	WRL::ComPtr<ID3D12CommandQueue> commandQueue;
	WRL::ComPtr<ID3D12CommandAllocator> commandAlloc;
	WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	static constexpr UINT swapChainBufferCount = 2;
	WRL::ComPtr<IDXGISwapChain1> swapChain;
	WRL::ComPtr<ID3D12Resource> swapChainBuffers[swapChainBufferCount];
	UINT currentBackBuffer = 0;

	WRL::ComPtr<ID3D12DescriptorHeap> heapRTV;
	WRL::ComPtr<ID3D12DescriptorHeap> heapDSV;
	WRL::ComPtr<ID3D12Resource> depthStencilBuffer;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

protected:
	virtual bool onInitialize() override;
	virtual bool onUpdate(float dt) override;
	virtual bool onTerminate() override;

	void draw();

	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** adapter);
	void flushCommandQueue();

	inline ID3D12Resource* getCurrentBackBuffer() const
	{
		return swapChainBuffers[currentBackBuffer].Get();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE getCurrentBackBufferView() const
	{
		/*D3D12_CPU_DESCRIPTOR_HANDLE hdl = heapRTV->GetCPUDescriptorHandleForHeapStart();
		hdl.ptr += currentBackBuffer * descSizeRTV;
		return hdl;*/
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			heapRTV->GetCPUDescriptorHandleForHeapStart(),
			currentBackBuffer,
			descSizeRTV);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE getDepthStencilView() const
	{
		return heapDSV->GetCPUDescriptorHandleForHeapStart();
	}

};