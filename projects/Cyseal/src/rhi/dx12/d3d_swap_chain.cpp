#include "d3d_swap_chain.h"
#include "d3d_device.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "d3d_into.h"

D3DSwapChain::D3DSwapChain()
{
	for (auto i = 0u; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		swapChainBuffers[i] = std::make_unique<D3DSwapChainBuffer>();
		backBufferRTVs[i] = std::make_unique<D3DRenderTargetView>();
	}
}

void D3DSwapChain::initialize(
	RenderDevice* renderDevice,
	void*         nativeWindowHandle,
	uint32        width,
	uint32        height)
{
	HWND hwnd = (HWND)nativeWindowHandle;

	device = static_cast<D3DDevice*>(renderDevice);
	backbufferWidth  = width;
	backbufferHeight = height;
	backbufferFormat = device->getBackbufferFormat();
	backbufferDepthFormat = device->getBackbufferDepthFormat();

	auto dxgiFactory  = device->getDXGIFactory();
	auto commandQueue = device->getRawCommandQueue();
	auto rawDevice    = device->getRawDevice();

	rawSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{
		.Width       = width,
		.Height      = height,
		.Format      = into_d3d::pixelFormat(backbufferFormat),
		.Stereo      = FALSE,
		// You can't create a MSAA swap chain.
		// https://gamedev.stackexchange.com/questions/149822/direct3d-12-cant-create-a-swap-chain
		.SampleDesc  = {1, 0},
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = SWAP_CHAIN_BUFFER_COUNT,
		.Scaling     = DXGI_SCALING_STRETCH,
		.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED,
		.Flags       = 0,
	};

	IDXGISwapChain1* tempSwapchain = nullptr;
	HR( dxgiFactory->CreateSwapChainForHwnd(
			commandQueue,
			hwnd,
			&swapChainDesc,
			nullptr, nullptr,
			&tempSwapchain)
	);
	rawSwapChain.Attach(static_cast<IDXGISwapChain3*>(tempSwapchain));

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
		.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT,
		.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		.NodeMask       = 0,
	};
	HR( rawDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heapRTV.GetAddressOf())) );

	createSwapchainImages();
}

void D3DSwapChain::resize(uint32 newWidth, uint32 newHeight)
{
	backbufferWidth = newWidth;
	backbufferHeight = newHeight;

	for (auto i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		swapChainBuffers[i]->setRawResource(nullptr);
		rawSwapChainBuffers[i].Reset();
	}
	rawSwapChain->ResizeBuffers(
		SWAP_CHAIN_BUFFER_COUNT,
		newWidth, newHeight,
		into_d3d::pixelFormat(backbufferFormat),
		0 /*DXGI_SWAP_CHAIN_FLAG*/);

	createSwapchainImages();
}

void D3DSwapChain::swapBackbuffer()
{
	// Do nothing here. DXGI swapchain automatically flips the back buffers.
	// 
	// https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains
	// -> With the flip model swap chain, back buffer "flips" are queued whenever your game calls IDXGISwapChain::Present.
}

uint32 D3DSwapChain::getCurrentBackbufferIndex() const
{
	return rawSwapChain->GetCurrentBackBufferIndex();
}

GPUResource* D3DSwapChain::getSwapchainBuffer(uint32 ix) const
{
	return swapChainBuffers[ix].get();
}

RenderTargetView* D3DSwapChain::getSwapchainBufferRTV(uint32 ix) const
{
	return backBufferRTVs[ix].get();
}

void D3DSwapChain::createSwapchainImages()
{
	auto rawDevice = device->getRawDevice();

	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		auto bufferPtr = rawSwapChainBuffers[i].GetAddressOf();
		HR(rawSwapChain->GetBuffer(i, IID_PPV_ARGS(bufferPtr)));
		swapChainBuffers[i]->setRawResource(rawSwapChainBuffers[i].Get());

		wchar_t debugName[256];
		swprintf_s(debugName, L"Backbuffer%u", i);
		rawSwapChainBuffers[i]->SetName(debugName);
	}

	// Create RTVs
	auto descSizeRTV = rawDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(heapRTV->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		auto rawResource = rawSwapChainBuffers[i].Get();
		rawDevice->CreateRenderTargetView(rawResource, nullptr, rtvHeapHandle);
		backBufferRTVs[i]->setCPUHandle(rtvHeapHandle);
		rtvHeapHandle.ptr += descSizeRTV;
	}
}

void D3DSwapChain::present()
{
	UINT SyncInterval = 0;
	UINT Flags = 0;
	HR( rawSwapChain->Present(SyncInterval, Flags) );
}
