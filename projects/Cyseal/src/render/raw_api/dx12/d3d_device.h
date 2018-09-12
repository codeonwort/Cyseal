#pragma once

#include "render/render_device.h"
#include "d3d_util.h"
#include "d3d_swap_chain.h"

#include <d3dcompiler.h>
#include <wrl.h>
using namespace Microsoft;

class D3DDevice : public RenderDevice
{
	static constexpr UINT SWAP_CHAIN_BUFFER_COUNT = 2;
	
public:
	~D3DDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;
	virtual void recreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) override;
	virtual void draw() override;

	inline IDXGIFactory4* getDXGIFactory() const { return dxgiFactory.Get(); }
	inline ID3D12Device* getRawDevice() const { return device.Get(); }
	inline ID3D12CommandQueue* getRawCommandQueue() const { return commandQueue.Get(); }

private:
	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter);
	void flushCommandQueue();

	inline ID3D12Resource* getCurrentBackBuffer() const
	{
		return d3dSwapChain->getCurrentBackBuffer();
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE getCurrentBackBufferView() const
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			heapRTV->GetCPUDescriptorHandleForHeapStart(),
			d3dSwapChain->getCurrentBackBufferIndex(),
			descSizeRTV);
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE getDepthStencilView() const
	{
		return heapDSV->GetCPUDescriptorHandleForHeapStart();
	}

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
	WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	class D3DRenderCommandAllocator* d3dCommandAllocator;

	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	D3DSwapChain* d3dSwapChain;

	WRL::ComPtr<ID3D12DescriptorHeap> heapRTV;
	WRL::ComPtr<ID3D12DescriptorHeap> heapDSV;

	WRL::ComPtr<ID3D12Resource> depthStencilBuffer;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

	uint32_t screenWidth;
	uint32_t screenHeight;

};
