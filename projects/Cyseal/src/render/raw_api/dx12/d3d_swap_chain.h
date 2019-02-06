#pragma once

#include "d3d_util.h"
#include "d3d_resource.h"
#include "render/swap_chain.h"
#include <memory>

class D3DDevice;
class D3DResource;
class D3DRenderTargetView;

class D3DSwapChain : public SwapChain
{
	static constexpr UINT SWAP_CHAIN_BUFFER_COUNT = 2;

public:
	D3DSwapChain();

	virtual void initialize(
		RenderDevice* renderDevice,
		HWND          hwnd,
		uint32_t      width,
		uint32_t      height) override;
	virtual void present() override;
	virtual void swapBackBuffer() override;

	virtual GPUResource* getCurrentBackBuffer() const override;
	virtual RenderTargetView* getCurrentBackBufferRTV() const override;

	inline IDXGISwapChain* getRaw() const { return rawSwapChain.Get(); }

	inline ID3D12Resource* getRawSwapChainBuffer(int ix) const
	{ return rawSwapChainBuffers[ix].Get(); }

	inline ID3D12Resource* d3d_getCurrentBackBuffer() const
	{ return rawSwapChainBuffers[currentBackBuffer].Get(); }

	inline UINT getCurrentBackBufferIndex() const
	{ return currentBackBuffer; }

	inline DXGI_FORMAT getBackBufferFormat() const
	{ return backBufferFormat; }

	// #todo: check 4xMSAA support
	inline bool supports4xMSAA() const
	{ return false; }

	// #todo: check 4xMSAA quality
	inline UINT get4xMSAAQuality() const
	{ return 1; }

private:
	D3DDevice* device;

	std::unique_ptr<D3DResource> swapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];
	std::unique_ptr<D3DRenderTargetView> backBufferRTVs[SWAP_CHAIN_BUFFER_COUNT];
	DXGI_FORMAT backBufferFormat;

	WRL::ComPtr<IDXGISwapChain1> rawSwapChain;
	WRL::ComPtr<ID3D12Resource> rawSwapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];
	UINT currentBackBuffer = 0;

	WRL::ComPtr<ID3D12DescriptorHeap> heapRTV;

};