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
public:
	static constexpr UINT SWAP_CHAIN_BUFFER_COUNT = 2;

public:
	D3DSwapChain();

	virtual void initialize(
		RenderDevice* renderDevice,
		void*         nativeWindowHandle,
		uint32        width,
		uint32        height) override;

	virtual void present() override;
	virtual void swapBackbuffer() override;
	virtual uint32 getBufferCount() override { return SWAP_CHAIN_BUFFER_COUNT; }

	virtual uint32 getCurrentBackbufferIndex() const override;
	virtual GPUResource* getCurrentBackbuffer() const override;
	virtual RenderTargetView* getCurrentBackbufferRTV() const override;

	inline IDXGISwapChain* getRaw() const { return rawSwapChain.Get(); }

	inline ID3D12Resource* getRawSwapChainBuffer(int ix) const
	{ return rawSwapChainBuffers[ix].Get(); }

	inline ID3D12Resource* d3d_getCurrentBackBuffer() const
	{ return rawSwapChainBuffers[currentBackBuffer].Get(); }

	inline UINT getCurrentBackBufferIndex() const
	{ return currentBackBuffer; }

private:
	D3DDevice* device;

	std::unique_ptr<D3DResource> swapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];
	std::unique_ptr<D3DRenderTargetView> backBufferRTVs[SWAP_CHAIN_BUFFER_COUNT];

	WRL::ComPtr<IDXGISwapChain3> rawSwapChain;
	WRL::ComPtr<ID3D12Resource> rawSwapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];
	UINT currentBackBuffer = 0; // #todo-deprecated: Is this necessary? See IDXGISwapChain3::GetCurrentBackBufferIndex()

	WRL::ComPtr<ID3D12DescriptorHeap> heapRTV;

};
