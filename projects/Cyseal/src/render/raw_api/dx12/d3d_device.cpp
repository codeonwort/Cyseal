#include "d3d_device.h"
#include "d3d_buffer.h"
#include "d3d_shader.h"
#include "d3d_render_command.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "core/assertion.h"
#include "util/logging.h"

// #todo-crossapi: Dynamic loading
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

DEFINE_LOG_CATEGORY_STATIC(LogDirectX);

// How to initialize D3D12
// 1. Create a ID3D12Device
// 2. Create a ID3D12Fence and get sizes of descriptors
// 3. Check 4X MSAA support
// 4. Create a command queue, a command list allocator, and a command list
// 5. Create a swap chain
// 6. Create descriptor heaps
// 7. Create a RTV for the back buffer
// 8. Create a depth/stencil buffer
// 9. Set viewport and scissor rect

D3DDevice::D3DDevice()
{
	defaultDepthStencilBuffer = new D3DResource;
	defaultDSV = new D3DDepthStencilView;
}

D3DDevice::~D3DDevice()
{
	delete swapChain;
	delete defaultDepthStencilBuffer;
	delete defaultDSV;

	delete commandAllocator;
	delete commandQueue;
	delete commandList;
}

void D3DDevice::initialize(const RenderDeviceCreateParams& createParams)
{
	UINT dxgiFactoryFlags = 0;

	// 1. Create a device.
#if _DEBUG
	WRL::ComPtr<ID3D12Debug> debugController;
	HR( D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) );
	debugController->EnableDebugLayer();
	dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	HR( CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)) );

	WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
	getHardwareAdapter(dxgiFactory.Get(), &hardwareAdapter);

	// #todo-debug: Fails here if the process is launched by Start Graphics Debugging. (GRFXTool::ToolException)
	// Create a device with feature level 11.0 to verify if the graphics card supports DX12.
	const D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
	if (FAILED(D3D12CreateDevice(
			hardwareAdapter.Get(),
			minFeatureLevel,
			IID_PPV_ARGS(&device))))
	{
		CHECK(0);
	}

	// Check the max supported feature level.
	const D3D_FEATURE_LEVEL dx12FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevelCandidates =
	{
		_countof(dx12FeatureLevels), dx12FeatureLevels, D3D_FEATURE_LEVEL_11_0
	};

	HR( device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevelCandidates, sizeof(featureLevelCandidates)) );

	// #todo-dxr: Check feature level
	if (createParams.rayTracingTier == ERayTracingTier::Tier_1_0)
	{
		CHECK(featureLevelCandidates.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_12_1);
	}

	// If possible, recreate the device with max feature level.
	if (featureLevelCandidates.MaxSupportedFeatureLevel != minFeatureLevel)
	{
		device = nullptr;

		HR( D3D12CreateDevice(
				hardwareAdapter.Get(),
				featureLevelCandidates.MaxSupportedFeatureLevel,
				IID_PPV_ARGS(&device)) );
	}

	rayTracingEnabled = supportsRayTracing();

	if (createParams.rayTracingTier == ERayTracingTier::Tier_1_0)
	{
		if (rayTracingEnabled)
		{
			CYLOG(LogDirectX, Log, TEXT("DXR enabled: tier %d"), 1);
		}
		else
		{
			CYLOG(LogDirectX, Warning, TEXT("DXR requested, but failed to be initialized"));
		}
	}

	// 2. Create a ID3D12Fence and retrieve sizes of descriptors.
	HR( device->CreateFence(0, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) );
	currentFence = 0;

	descSizeRTV         = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descSizeDSV         = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	descSizeCBV_SRV_UAV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 3. Check 4X MSAA support.
	// It gives good result and the overload is not so big.
	// All D3D11 level devices support 4X MSAA for all render target types.
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format           = into_d3d::pixelFormat(backbufferFormat);
	msQualityLevels.SampleCount      = 4;
	msQualityLevels.Flags            = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	HR( device->CheckFeatureSupport(
			D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			&msQualityLevels,
			sizeof(msQualityLevels))
	);

	quality4xMSAA = msQualityLevels.NumQualityLevels;
	CHECK(quality4xMSAA > 0);

	// 4. Create command queues, command list allocators, and main command queue.
	commandQueue = new D3DRenderCommandQueue;
	commandQueue->initialize(this);

	commandAllocator = ( d3dCommandAllocator = new D3DRenderCommandAllocator );
	commandAllocator->initialize(this);

	commandList = new D3DRenderCommandList;
	commandList->initialize(this);

	// Get raw interfaces
	rawCommandQueue = static_cast<D3DRenderCommandQueue*>(commandQueue)->getRaw();
	rawCommandList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();

	// 5. Create a swap chain.
	swapChain = (d3dSwapChain = new D3DSwapChain);
	recreateSwapChain(createParams.hwnd, createParams.windowWidth, createParams.windowHeight);

	// 6. Create descriptor heaps for CBV/SRV/UAV.
	for (int32 i = 0; i < (int32)D3DSwapChain::SWAP_CHAIN_BUFFER_COUNT; ++i)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.NumDescriptors = 1;
		desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HR( device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heapCBV_SRV_UAV[i])) );
	}

	// #todo-shader: Shader Model 6
	D3D12_FEATURE_DATA_SHADER_MODEL SM = { D3D_SHADER_MODEL::D3D_SHADER_MODEL_5_1 };
	HR( device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &SM, sizeof(SM)) );
	CHECK(SM.HighestShaderModel >= D3D_SHADER_MODEL::D3D_SHADER_MODEL_5_1);
}

void D3DDevice::recreateSwapChain(HWND hwnd, uint32 width, uint32 height)
{
	screenWidth = width;
	screenHeight = height;

	swapChain->initialize(this, hwnd, width, height);

	// Create DSV heap.
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc;
		desc.NumDescriptors = 1;
		desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask       = 0;
		HR( device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heapDSV.GetAddressOf())) );
	}
	{
		D3D12_RESOURCE_DESC dsDesc;
		dsDesc.Dimension          = D3D12_RESOURCE_DIMENSION::D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		dsDesc.Alignment          = 0;
		dsDesc.Width              = width;
		dsDesc.Height             = height;
		dsDesc.DepthOrArraySize   = 1;
		dsDesc.MipLevels          = 1;
		dsDesc.Format             = into_d3d::pixelFormat(backbufferDepthFormat);
		dsDesc.SampleDesc.Count   = 1;
		dsDesc.SampleDesc.Quality = 0;
		dsDesc.Layout             = D3D12_TEXTURE_LAYOUT::D3D12_TEXTURE_LAYOUT_UNKNOWN;
		dsDesc.Flags              = D3D12_RESOURCE_FLAGS::D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optClear;
		optClear.Format               = into_d3d::pixelFormat(backbufferDepthFormat);
		optClear.DepthStencil.Depth   = 1.0f;
		optClear.DepthStencil.Stencil = 0;

		D3D12_HEAP_PROPERTIES dsHeapProps;
		dsHeapProps.Type                 = D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT;
		dsHeapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY::D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		dsHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL::D3D12_MEMORY_POOL_UNKNOWN;
		dsHeapProps.CreationNodeMask     = 0;
		dsHeapProps.VisibleNodeMask      = 0;

		HR( device->CreateCommittedResource(
				&dsHeapProps,
				D3D12_HEAP_FLAG_NONE, &dsDesc, D3D12_RESOURCE_STATE_COMMON, &optClear,
				IID_PPV_ARGS(rawDepthStencilBuffer.GetAddressOf()))
		);

		device->CreateDepthStencilView(
			rawDepthStencilBuffer.Get(),
			nullptr,
			rawGetDepthStencilView());
	}

	static_cast<D3DResource*>(defaultDepthStencilBuffer)->setRaw(rawDepthStencilBuffer.Get());
	static_cast<D3DDepthStencilView*>(defaultDSV)->setRaw(rawGetDepthStencilView());
}

void D3DDevice::getHardwareAdapter(IDXGIFactory2* factory, IDXGIAdapter1** outAdapter)
{
	WRL::ComPtr<IDXGIAdapter1> adapter;
	*outAdapter = nullptr;

	for (UINT i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adapter); ++i)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		if (SUCCEEDED(D3D12CreateDevice(
			adapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			//D3D_FEATURE_LEVEL_12_1,
			__uuidof(ID3D12Device),
			nullptr)))
		{
			break;
		}
	}

	*outAdapter = adapter.Detach();
}

void D3DDevice::flushCommandQueue()
{
	// Advance the fence value to mark commands up to this fence point.
	++currentFence;

	auto queue = rawCommandQueue;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	HR( queue->Signal(fence.Get(), currentFence) );

	// Wait until the GPU has completed commands up to this fence point.
	if (fence->GetCompletedValue() < currentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		HR( fence->SetEventOnCompletion(currentFence, eventHandle) );

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

bool D3DDevice::supportsRayTracing()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps = {};
	HR( device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &caps, sizeof(caps)) );

	if (caps.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
	{
		return false;
	}
	return true;
}

VertexBuffer* D3DDevice::createVertexBuffer(void* data, uint32 sizeInBytes, uint32 strideInBytes)
{
	D3DVertexBuffer* buffer = new D3DVertexBuffer;
	buffer->initialize(data, sizeInBytes, strideInBytes);
	return buffer;
}

IndexBuffer* D3DDevice::createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format)
{
	D3DIndexBuffer* buffer = new D3DIndexBuffer;
	buffer->initialize(data, sizeInBytes, format);
	return buffer;
}

Shader* D3DDevice::createShader()
{
	return new D3DShader;
}

RootSignature* D3DDevice::createRootSignature(const RootSignatureDesc& desc)
{
	into_d3d::TempAlloc tempAlloc;
	D3D12_ROOT_SIGNATURE_DESC d3d_desc;
	into_d3d::rootSignatureDesc(desc, d3d_desc, tempAlloc);

	WRL::ComPtr<ID3DBlob> serializedRootSig;
	WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT result = D3D12SerializeRootSignature(
		&d3d_desc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());
	
	if (errorBlob != nullptr)
	{
		const char* message = reinterpret_cast<char*>(errorBlob->GetBufferPointer());
		::OutputDebugStringA(message);
	}

	HR(result);

	D3DRootSignature* rootSignature = new D3DRootSignature;
	rootSignature->initialize(device.Get(), 0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize());

	return rootSignature;
}

PipelineState* D3DDevice::createGraphicsPipelineState(const GraphicsPipelineDesc& desc)
{
	into_d3d::TempAlloc tempAlloc;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC d3d_desc;
	into_d3d::graphicsPipelineDesc(desc, d3d_desc, tempAlloc);

	D3DPipelineState* pipeline = new D3DPipelineState;
	pipeline->initialize(device.Get(), d3d_desc);

	return pipeline;
}

DescriptorHeap* D3DDevice::createDescriptorHeap(const DescriptorHeapDesc& desc)
{
	D3D12_DESCRIPTOR_HEAP_DESC d3d_desc;
	into_d3d::descriptorHeapDesc(desc, d3d_desc);

	D3DDescriptorHeap* heap = new D3DDescriptorHeap;
	heap->initialize(device.Get(), d3d_desc);

	return heap;
}

ConstantBuffer* D3DDevice::createConstantBuffer(DescriptorHeap* descriptorHeap, uint32 heapSize, uint32 payloadSize)
{
	ID3D12DescriptorHeap* rawHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();

	D3DConstantBuffer* cb = new D3DConstantBuffer;
	cb->initialize(device.Get(), rawHeap, heapSize, payloadSize);

	return cb;
}
