#include "d3d_swap_chain.h"
#include "d3d_device.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "d3d_into.h"

D3DSwapChain::D3DSwapChain()
{
}

D3DSwapChain::~D3DSwapChain()
{
	backBufferRTVs.reset();
	heapRTV.reset();

	swapChainBuffers.reset();
	for (auto i = 0u; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		rawSwapChainBuffers[i].Reset();
	}
	rawSwapChain.Reset();
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

	// #todo-swapchain: Support fullscreen
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc{};
	fullscreenDesc.Windowed = TRUE;

	WRL::ComPtr<IDXGISwapChain1> dxgiSwapchain1;
	HR( dxgiFactory->CreateSwapChainForHwnd(
			commandQueue,
			hwnd,
			&swapChainDesc,
			&fullscreenDesc,
			nullptr,
			&dxgiSwapchain1)
	);
	HR( dxgiSwapchain1->QueryInterface(IID_PPV_ARGS(&rawSwapChain)) );

	// CAUTION: gDescriptorHeaps is not initialized yet.
	DescriptorHeapDesc heapDesc{
		.type           = EDescriptorHeapType::RTV,
		.numDescriptors = SWAP_CHAIN_BUFFER_COUNT,
		.flags          = EDescriptorHeapFlags::None,
		.nodeMask       = 0,
	};
	heapRTV = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(heapDesc));

	swapChainBuffers.initialize(SWAP_CHAIN_BUFFER_COUNT);
	backBufferRTVs.initialize(SWAP_CHAIN_BUFFER_COUNT);
	for (auto i = 0u; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		swapChainBuffers[i] = makeUnique<D3DSwapChainBuffer>();
	}

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
	return swapChainBuffers.at(ix);
}

RenderTargetView* D3DSwapChain::getSwapchainBufferRTV(uint32 ix) const
{
	return backBufferRTVs.at(ix);
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

	for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		backBufferRTVs[i].reset(); // Need to reset first because heapRTV is only as large as swapchain count.
		RenderTargetViewDesc rtvDesc{
			.format        = backbufferFormat,
			.viewDimension = ERTVDimension::Texture2D,
			.texture2D     = Texture2DRTVDesc {.mipSlice = 0, .planeSlice = 0 },
		};
		auto rtv = gRenderDevice->createRTV(swapChainBuffers.at(i), heapRTV.get(), rtvDesc);
		backBufferRTVs[i] = UniquePtr<RenderTargetView>(rtv);
	}
}

void D3DSwapChain::present()
{
	UINT syncInterval = 1;
	UINT flags = 0;
	HRESULT hResult = rawSwapChain->Present(syncInterval, flags);

	// #todo-dx12: Report DRED log
	// https://microsoft.github.io/DirectX-Specs/d3d/DeviceRemovedExtendedData.html
	if (hResult == DXGI_ERROR_DEVICE_REMOVED || hResult == DXGI_ERROR_DEVICE_RESET)
	{
		CHECK_NO_ENTRY();
	}

	HR(hResult);
}
