#pragma once

#include "d3d_util.h"
#include "d3d_resource.h"
#include "rhi/swap_chain.h"
#include <memory>

class D3DDevice;
class D3DResource;
class D3DRenderTargetView;

class D3DSwapChainBuffer : public GPUResource
{
public:
	virtual void* getRawResource() const override { return raw; }
	virtual void setRawResource(void* inRawResource) override
	{
		raw = reinterpret_cast<ID3D12Resource*>(inRawResource);
	}
private:
	ID3D12Resource* raw = nullptr;
};

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

	virtual void resize(uint32 newWidth, uint32 newHeight) override;

	virtual void present() override;
	virtual void swapBackbuffer() override;
	virtual uint32 getBufferCount() const override { return SWAP_CHAIN_BUFFER_COUNT; }

	virtual uint32 getCurrentBackbufferIndex() const override;
	virtual GPUResource* getSwapchainBuffer(uint32 ix) const override;
	virtual RenderTargetView* getSwapchainBufferRTV(uint32 ix) const override;

	inline IDXGISwapChain* getRaw() const { return rawSwapChain.Get(); }

private:
	void createSwapchainImages();

	D3DDevice* device;

	std::unique_ptr<D3DSwapChainBuffer> swapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];
	std::unique_ptr<D3DRenderTargetView> backBufferRTVs[SWAP_CHAIN_BUFFER_COUNT];

	WRL::ComPtr<IDXGISwapChain3> rawSwapChain;
	WRL::ComPtr<ID3D12Resource> rawSwapChainBuffers[SWAP_CHAIN_BUFFER_COUNT];

	WRL::ComPtr<ID3D12DescriptorHeap> heapRTV;

};
