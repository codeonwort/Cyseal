#include "d3d_swap_chain.h"
#include "d3d_device.h"

void D3DSwapChain::initialize(
	RenderDevice* renderDevice,
	HWND          hwnd,
	uint32_t      width,
	uint32_t      height)
{
	device = static_cast<D3DDevice*>(renderDevice);
	backbufferWidth = width;
	backbufferHeight = height;

	auto dxgiFactory = device->getDXGIFactory();
	auto commandQueue = device->getRawCommandQueue();

	swapChain.Reset();

	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
	desc.Width = width;
	desc.Height = height;
	desc.Format = BACK_BUFFER_FORMAT;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// You can't create a MSAA swapchain.
	// https://gamedev.stackexchange.com/questions/149822/direct3d-12-cant-create-a-swap-chain
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	HR( dxgiFactory->CreateSwapChainForHwnd(
			commandQueue,
			hwnd,
			&desc,
			nullptr, nullptr,
			swapChain.GetAddressOf())
	);

	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		auto bufferPtr = swapChainBuffers[i].GetAddressOf();
		HR(swapChain->GetBuffer(i, IID_PPV_ARGS(bufferPtr)));
	}
}

void D3DSwapChain::swapBackBuffer()
{
	currentBackBuffer = (currentBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;
}

void D3DSwapChain::present()
{
	HR( swapChain->Present(0, 0) );
}
