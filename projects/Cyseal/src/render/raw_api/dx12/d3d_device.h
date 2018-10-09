#pragma once

#include "render/render_device.h"
#include "d3d_util.h"
#include "d3d_swap_chain.h"

#include <d3dcompiler.h>
#include <wrl.h>
using namespace Microsoft;

class D3DDevice : public RenderDevice
{

public:
	D3DDevice();
	~D3DDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) override;
	virtual void recreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) override;
	virtual void flushCommandQueue() override;

	inline IDXGIFactory4* getDXGIFactory() const { return dxgiFactory.Get(); }
	inline ID3D12Device* getRawDevice() const { return device.Get(); }
	inline ID3D12CommandQueue* getRawCommandQueue() const { return rawCommandQueue; }

private:
	void getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter);

	inline D3D12_CPU_DESCRIPTOR_HANDLE rawGetDepthStencilView() const
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

	// Raw interfaces
	ID3D12CommandQueue* rawCommandQueue;
	ID3D12GraphicsCommandList* rawCommandList;
	class D3DRenderCommandAllocator* d3dCommandAllocator;
	D3DSwapChain* d3dSwapChain;

	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	WRL::ComPtr<ID3D12DescriptorHeap> heapDSV;

	WRL::ComPtr<ID3D12Resource> rawDepthStencilBuffer;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

	uint32_t screenWidth;
	uint32_t screenHeight;

};
