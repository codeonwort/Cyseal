#include "app.h"
#include "d3d_util.h"

// engine test
#include "core/engine.h"
#include "memory/mem_alloc.h"
#include "util/unit_test.h"

CysealEngine cysealEngine;

class UnitTestHello : public UnitTest
{
	virtual bool runTest() override
	{
		return true;
	}
};
DEFINE_UNIT_TEST(UnitTestHello);

// How to initialize D3D12
// 1. Create a ID3D12Device
// 2. Create a ID3D12Fence and get sizes of descriptors
// 3. Check 4X MSAA support
// 4. Create a command queue, a command list allocator, and a command list
// 5. Create a swap chain
// 6. Create descriptor heaps
// 7. Create a RTV for the backbuffer
// 8. Create a depth/stencil buffer
// 9. Set viewport and scissor rect

bool Application::onInitialize()
{
	cysealEngine.startup();

	UINT dxgiFactoryFlags = 0;

	// 1. D3D12CreateDevice()
#if _DEBUG
	WRL::ComPtr<ID3D12Debug> debugController;
	HR(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	debugController->EnableDebugLayer();
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
	getHardwareAdapter(dxgiFactory.Get(), &hardwareAdapter);

	HR(D3D12CreateDevice(
		hardwareAdapter.Get(),
		D3D_FEATURE_LEVEL_12_1,
		IID_PPV_ARGS(&device)));

	// 2. Create a ID3D12Fence and retrieve sizes of descriptors
	HR(device->CreateFence(0, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	currentFence = 0;

	descSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	descSizeCBV_SRV_UAV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 3. Check 4X MSAA support
	// It gives good result and the overload is not so big.
	// All D3D11 level devices support 4X MSAA for all render target types.
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = backBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	HR(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

	quality4xMSAA = msQualityLevels.NumQualityLevels;
	check(quality4xMSAA > 0);

	// 4. Create command queues, command list allocators, and main command queue
	// command queue: ID3D12CommandQueue
	// command allocator: ID3D12CommandAllocator
	// command list: ID3D12GraphicsCommandList
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	HR(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
	HR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAlloc.GetAddressOf())));
	HR(device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(commandList.GetAddressOf())));
	commandList->Close();

	// 5. Create a swap chain
	{
		swapChain.Reset();

		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.BufferCount = swapChainBufferCount;
		desc.Width = getWidth();
		desc.Height = getHeight();
		desc.Format = backBufferFormat;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		// You can't create a MSAA swapchain.
		// https://gamedev.stackexchange.com/questions/149822/direct3d-12-cant-create-a-swap-chain
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		HR(dxgiFactory->CreateSwapChainForHwnd(
			commandQueue.Get(),
			getHWND(),
			&desc,
			nullptr, nullptr,
			&swapChain));
	}

	// 6. Create descriptor heaps
	// descriptor heap: ID3D12DescriptorHeap
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = swapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	HR(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(heapRTV.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	HR(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(heapDSV.GetAddressOf())));

	// 7. Create RTV
	// First, get the buffer resource stored in swap chain
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(heapRTV->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < swapChainBufferCount; ++i)
	{
		HR(swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBuffers[i])));
		device->CreateRenderTargetView(swapChainBuffers[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.ptr += descSizeRTV;
	}

	// 8. Create D/S buffer and DSV
	// construct D3D12_RESOURCE_DESC
	// ID3D12Device::CreateCommittedResource
	// D3D12_HEAP_PROPERTIES
	D3D12_RESOURCE_DESC dsDesc;
	dsDesc.Dimension = D3D12_RESOURCE_DIMENSION::D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dsDesc.Alignment = 0;
	dsDesc.Width = getWidth();
	dsDesc.Height = getHeight();
	dsDesc.DepthOrArraySize = 1;
	dsDesc.MipLevels = 1;
	dsDesc.Format = depthStencilFormat;
	dsDesc.SampleDesc.Count = 1;
	dsDesc.SampleDesc.Quality = 0;
	dsDesc.Layout = D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_UNKNOWN;
	dsDesc.Flags = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = depthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES dsHeapProps;
	dsHeapProps.Type = D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT;
	dsHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY::D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	dsHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL::D3D12_MEMORY_POOL_UNKNOWN;
	dsHeapProps.CreationNodeMask = 0;
	dsHeapProps.VisibleNodeMask = 0;

	HR(device->CreateCommittedResource(&dsHeapProps,
		D3D12_HEAP_FLAG_NONE, &dsDesc, D3D12_RESOURCE_STATE_COMMON, &optClear,
		IID_PPV_ARGS(depthStencilBuffer.GetAddressOf())));

	device->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, getDepthStencilView());

	// 9. Viewport
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(getWidth());
	viewport.Height = static_cast<float>(getHeight());
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// 10. Scissor rect
	scissorRect.left = scissorRect.top = 0;
	scissorRect.right = getWidth();
	scissorRect.bottom = getHeight();
	
	return true;
}

bool Application::onUpdate(float dt)
{
	wchar_t buf[256];
	swprintf_s(buf, L"Hello World / FPS: %.2f", 1.0f / dt);
	setTitle(std::wstring(buf));

	draw();

	return true;
}

bool Application::onTerminate()
{
	cysealEngine.shutdown();

	return true;
}

void Application::draw()
{
	HR(commandAlloc->Reset());
	HR(commandList->Reset(commandAlloc.Get(), nullptr));

	commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			getCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET));

	commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			depthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_DEPTH_WRITE));
	
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	float red = 0.5f + 0.5f * sinf(getElapsedSecondsFromStart());
	FLOAT clearColor[4] = { red, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(
		getCurrentBackBufferView(),
		clearColor,
		0, nullptr);

	commandList->ClearDepthStencilView(
		getDepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0,
		0, nullptr);

	commandList->OMSetRenderTargets(1, &getCurrentBackBufferView(), true, &getDepthStencilView());

	commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			getCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT));

	commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			depthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_COMMON));

	HR(commandList->Close());

	ID3D12CommandList* cmdLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	HR(swapChain->Present(0, 0));
	currentBackBuffer = (currentBackBuffer + 1) % swapChainBufferCount;

	flushCommandQueue();
}

void Application::getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** ppAdapter)
{
	WRL::ComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adapter); ++i)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}

void Application::flushCommandQueue()
{
	// Advance the fence value to mark commands up to this fence point.
	++currentFence;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	HR(commandQueue->Signal(fence.Get(), currentFence));

	// Wait until the GPU has completed commands up to this fence point.
	if (fence->GetCompletedValue() < currentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		HR(fence->SetEventOnCompletion(currentFence, eventHandle));

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}
