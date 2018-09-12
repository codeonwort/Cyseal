#pragma once

#include "d3d_util.h"
#include "render/swap_chain.h"

class D3DDevice;

class D3DSwapChain : public SwapChain
{
	static constexpr UINT SWAP_CHAIN_BUFFER_COUNT = 2;
	static constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

public:
	virtual void initialize(
		RenderDevice* renderDevice,
		HWND          hwnd,
		uint32_t      width,
		uint32_t      height) override;
	virtual void present() override;
	virtual void swapBackBuffer() override;

	inline IDXGISwapChain* getRaw() const { return swapChain.Get(); }
	inline ID3D12Resource* getSwapChainBuffer(int ix) const
	{ return swapChainBuffers[ix].Get(); }
	inline ID3D12Resource* getCurrentBackBuffer() const
	{ return swapChainBuffers[currentBackBuffer].Get(); }
	inline UINT getCurrentBackBufferIndex() const
	{ return currentBackBuffer; }

private:
	D3DDevice* device;

	uint32_t backbufferWidth;
	uint32_t backbufferHeight;

	WRL::ComPtr<IDXGISwapChain1> swapChain;
	WRL::ComPtr<ID3D12Resource> swapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];
	UINT currentBackBuffer = 0;

};
