#include "d3d_swap_chain.h"
#include "d3d_device.h"
#include "d3d_resource_view.h"

D3DSwapChain::D3DSwapChain()
{
	for (auto i = 0u; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		swapChainBuffers[i] = std::make_unique<D3DResource>();
		backBufferRTVs[i] = std::make_unique<D3DRenderTargetView>();
	}
}

void D3DSwapChain::initialize(
	RenderDevice* renderDevice,
	HWND          hwnd,
	uint32_t      width,
	uint32_t      height)
{
	device = static_cast<D3DDevice*>(renderDevice);
	backBufferWidth  = width;
	backBufferHeight = height;
	backBufferFormat = device->getBackBufferFormat();

	auto dxgiFactory  = device->getDXGIFactory();
	auto commandQueue = device->getRawCommandQueue();
	auto rawDevice    = device->getRawDevice();

	rawSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.BufferCount        = SWAP_CHAIN_BUFFER_COUNT;
	desc.Width              = width;
	desc.Height             = height;
	desc.Format             = backBufferFormat;
	desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// You can't create a MSAA swap chain.
	// https://gamedev.stackexchange.com/questions/149822/direct3d-12-cant-create-a-swap-chain
	desc.SampleDesc.Count   = 1;
	desc.SampleDesc.Quality = 0;

	HR( dxgiFactory->CreateSwapChainForHwnd(
			commandQueue,
			hwnd,
			&desc,
			nullptr, nullptr,
			rawSwapChain.GetAddressOf())
	);

	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		auto bufferPtr = rawSwapChainBuffers[i].GetAddressOf();
		HR( rawSwapChain->GetBuffer(i, IID_PPV_ARGS(bufferPtr)) );
		swapChainBuffers[i]->setRaw(rawSwapChainBuffers[i].Get());
	}

	// Create RTV heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc;
		desc.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 0;

		HR( rawDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heapRTV.GetAddressOf())) );
	}

	// Create RTVs
	auto descSizeRTV = rawDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(heapRTV->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		auto buffer = getRawSwapChainBuffer(i);
		rawDevice->CreateRenderTargetView(buffer, nullptr, rtvHeapHandle);
		backBufferRTVs[i]->setRaw(rtvHeapHandle);
		rtvHeapHandle.ptr += descSizeRTV;
	}
}

void D3DSwapChain::swapBackBuffer()
{
	currentBackBuffer = (currentBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;
}

GPUResource* D3DSwapChain::getCurrentBackBuffer() const
{
	return swapChainBuffers[currentBackBuffer].get();
}

RenderTargetView* D3DSwapChain::getCurrentBackBufferRTV() const
{
	return backBufferRTVs[currentBackBuffer].get();
}

void D3DSwapChain::present()
{
	HR( rawSwapChain->Present(0, 0) );
}
