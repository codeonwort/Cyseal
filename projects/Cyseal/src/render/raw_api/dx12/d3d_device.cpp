#include "d3d_device.h"
#include "d3d_buffer.h"
#include "d3d_texture.h"
#include "d3d_shader.h"
#include "d3d_render_command.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "d3d_into.h"
#include "core/assertion.h"
#include "render/texture_manager.h"
#include "util/logging.h"

// #todo-crossapi: Dynamic loading
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if _DEBUG
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

// https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model
// SM 5.1: Dynamic indexing of descriptors within a shader
// SM 6.0: Wave intrinsics / 64-bit int
// SM 6.1: SV_ViewID / Barycentric semantics / GetAttributeAtVertex intrinsic
// SM 6.2: float16 / Denorm mode selection
// SM 6.3: DXR
// SM 6.4: VRS / Low-precision packed dot product intrinsics / Library sub-objects for raytracing
// SM 6.5: DXR 1.1 / Sampler Feedback / Mesh & amplication shaders / More Wave intrinsics
// SM 6.6: New atomic ops / Dynamic resources / IsHelperLane()
//         / Derivatives in compute & mesh & amp shaders / Pack & unpack intrinsics
//         / WaveSize / Raytracing Payload Access Qualifiers
// SM 6.7: https://devblogs.microsoft.com/directx/shader-model-6-7/
#define CYSEAL_D3D_SHADER_MODEL_MINIMUM D3D_SHADER_MODEL_5_1 /* Minimum required SM to run Cyseal */
#define CYSEAL_D3D_SHADER_MODEL_HIGHEST D3D_SHADER_MODEL_6_6 /* Highest SM that Cyseal recognizes */

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

void reportD3DLiveObjects()
{
#if _DEBUG
	CYLOG(LogDirectX, Log, L"Checking live objects...");
	IDXGIDebug1* dxgiDebug = NULL;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
	{
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL,
			DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		dxgiDebug->Release();
	}
#endif
}

D3DDevice::D3DDevice()
{
	std::atexit(reportD3DLiveObjects);
}

D3DDevice::~D3DDevice()
{
	delete swapChain;
	for (size_t i = 0; i < commandAllocators.size(); ++i)
	{
		delete commandAllocators[i];
	}
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

	// Warning: Fails here if the process is launched by Start Graphics Debugging. (GRFXTool::ToolException)
	//          OK, seems VS-integrated Graphics Debugging is not maintained anymore and I have to use PIX :/
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
		D3D_FEATURE_LEVEL_12_2,
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

	// If possible, recreate the device with max feature level.
	if (featureLevelCandidates.MaxSupportedFeatureLevel != minFeatureLevel)
	{
		device.Reset();

		HR(D3D12CreateDevice(
			hardwareAdapter.Get(),
			featureLevelCandidates.MaxSupportedFeatureLevel,
			IID_PPV_ARGS(&device)));
	}
	
	// Check capabilities
	{
		// #todo-dx12: Use d3dx12 feature support helper?
	// https://devblogs.microsoft.com/directx/introducing-a-new-api-for-checking-feature-support-in-direct3d-12/

		D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps5 = {}; // DXR
		D3D12_FEATURE_DATA_D3D12_OPTIONS6 caps6 = {}; // VRS
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps7 = {}; // Mesh shader, sampler feedback
		HR(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &caps5, sizeof(caps5)));
		HR(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &caps6, sizeof(caps6)));
		HR(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &caps7, sizeof(caps7)));

		switch (caps5.RaytracingTier)
		{
			case D3D12_RAYTRACING_TIER_NOT_SUPPORTED: raytracingTier = ERaytracingTier::NotSupported; break;
			case D3D12_RAYTRACING_TIER_1_0: raytracingTier = ERaytracingTier::Tier_1_0; break;
			case D3D12_RAYTRACING_TIER_1_1: raytracingTier = ERaytracingTier::Tier_1_1; break;
			default: CHECK_NO_ENTRY();
		}
		switch (caps6.VariableShadingRateTier)
		{
			case D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED: vrsTier = EVariableShadingRateTier::NotSupported; break;
			case D3D12_VARIABLE_SHADING_RATE_TIER_1: vrsTier = EVariableShadingRateTier::Tier_1; break;
			case D3D12_VARIABLE_SHADING_RATE_TIER_2: vrsTier = EVariableShadingRateTier::Tier_2; break;
			default: CHECK_NO_ENTRY();
		}
		switch (caps7.MeshShaderTier)
		{
			case D3D12_MESH_SHADER_TIER_NOT_SUPPORTED: meshShaderTier = EMeshShaderTier::NotSupported; break;
			case D3D12_MESH_SHADER_TIER_1: meshShaderTier = EMeshShaderTier::Tier_1; break;
			default: CHECK_NO_ENTRY();
		}
		switch (caps7.SamplerFeedbackTier)
		{
			case D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED: samplerFeedbackTier = ESamplerFeedbackTier::NotSupported; break;
			case D3D12_SAMPLER_FEEDBACK_TIER_0_9: samplerFeedbackTier = ESamplerFeedbackTier::Tier_0_9; break;
			case D3D12_SAMPLER_FEEDBACK_TIER_1_0: samplerFeedbackTier = ESamplerFeedbackTier::Tier_1_0; break;
			default: CHECK_NO_ENTRY();
		}

		CYLOG(LogDirectX, Log, L"=== Hardware capabilities ===");
		CYLOG(LogDirectX, Log, L"DXR             requested=%S\t\tactual=%S", toString(createParams.raytracingTier), toString(raytracingTier));
		CYLOG(LogDirectX, Log, L"VRS             requested=%S\t\tactual=%S", toString(createParams.vrsTier), toString(vrsTier));
		CYLOG(LogDirectX, Log, L"MeshShader      requested=%S\t\tactual=%S", toString(createParams.meshShaderTier), toString(meshShaderTier));
		CYLOG(LogDirectX, Log, L"SamplerFeedback requested=%S\t\tactual=%S", toString(createParams.samplerFeedbackTier), toString(samplerFeedbackTier));
	}

	// 2. Create a ID3D12Fence and retrieve sizes of descriptors.
	HR( device->CreateFence(0, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) );
	currentFence = 0;

	descSizeRTV         = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descSizeDSV         = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	descSizeCBV_SRV_UAV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descSizeSampler     = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

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

	// 4. Create command queue.
	commandQueue = new D3DRenderCommandQueue;
	commandQueue->initialize(this);

	rawCommandQueue = static_cast<D3DRenderCommandQueue*>(commandQueue)->getRaw();

	// 5. Create swap chain.
	swapChain = (d3dSwapChain = new D3DSwapChain);
	swapChain->initialize(
		this,
		createParams.nativeWindowHandle,
		createParams.windowWidth,
		createParams.windowHeight);

	// 6. Create command allocators and command list.
	for (uint32 ix = 0; ix < swapChain->getBufferCount(); ++ix)
	{
		RenderCommandAllocator* allocator = new D3DRenderCommandAllocator;
		allocator->initialize(this);
		commandAllocators.push_back(allocator);
	}

	commandList = new D3DRenderCommandList;
	commandList->initialize(this);

	rawCommandList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();

	D3D12_FEATURE_DATA_SHADER_MODEL SM = { CYSEAL_D3D_SHADER_MODEL_HIGHEST };
	HR( device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &SM, sizeof(SM)) );
	if (SM.HighestShaderModel < CYSEAL_D3D_SHADER_MODEL_MINIMUM)
	{
		CYLOG(LogDirectX, Fatal, L"Current PC does not support minimum required Shader Model");
		CHECK_NO_ENTRY();
	}
	highestShaderModel = SM.HighestShaderModel;
}

void D3DDevice::recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height)
{
	swapChain->resize(width, height);
}

void D3DDevice::allocateSRVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex)
{
	ID3D12DescriptorHeap* viewHeap = static_cast<D3DDescriptorHeap*>(gTextureManager->getSRVHeap())->getRaw();
	const uint32 viewIndex = gTextureManager->allocateSRVIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE handle = viewHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += SIZE_T(viewIndex) * SIZE_T(descSizeCBV_SRV_UAV);

	outSourceHeap = gTextureManager->getSRVHeap();
	outHandle = handle;
	outDescriptorIndex = viewIndex;
}

void D3DDevice::allocateRTVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex)
{
	ID3D12DescriptorHeap* viewHeap = static_cast<D3DDescriptorHeap*>(gTextureManager->getRTVHeap())->getRaw();
	const uint32 viewIndex = gTextureManager->allocateRTVIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE handle = viewHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += SIZE_T(viewIndex) * SIZE_T(descSizeRTV);

	outSourceHeap = gTextureManager->getRTVHeap();
	outHandle = handle;
	outDescriptorIndex = viewIndex;
}

void D3DDevice::allocateDSVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex)
{
	ID3D12DescriptorHeap* viewHeap = static_cast<D3DDescriptorHeap*>(gTextureManager->getDSVHeap())->getRaw();
	const uint32 viewIndex = gTextureManager->allocateDSVIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE handle = viewHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += SIZE_T(viewIndex) * SIZE_T(descSizeDSV);

	outSourceHeap = gTextureManager->getDSVHeap();
	outHandle = handle;
	outDescriptorIndex = viewIndex;
}

void D3DDevice::allocateUAVHandle(DescriptorHeap*& outSourceHeap, D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32& outDescriptorIndex)
{
	ID3D12DescriptorHeap* viewHeap = static_cast<D3DDescriptorHeap*>(gTextureManager->getUAVHeap())->getRaw();
	const uint32 viewIndex = gTextureManager->allocateUAVIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE handle = viewHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += SIZE_T(viewIndex) * SIZE_T(descSizeCBV_SRV_UAV);

	outSourceHeap = gTextureManager->getUAVHeap();
	outHandle = handle;
	outDescriptorIndex = viewIndex;
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
			//D3D_FEATURE_LEVEL_12_2,
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
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		CHECK(eventHandle != 0);

		// Fire event when GPU hits current fence.  
		HR( fence->SetEventOnCompletion(currentFence, eventHandle) );

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

VertexBuffer* D3DDevice::createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName)
{
	D3DVertexBuffer* buffer = new D3DVertexBuffer;
	buffer->initialize(sizeInBytes);
	if (inDebugName != nullptr)
	{
		buffer->setDebugName(inDebugName);
	}
	return buffer;
}

VertexBuffer* D3DDevice::createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	D3DVertexBuffer* buffer = new D3DVertexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

IndexBuffer* D3DDevice::createIndexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName)
{
	D3DIndexBuffer* buffer = new D3DIndexBuffer;
	buffer->initialize(sizeInBytes);
	if (inDebugName != nullptr)
	{
		buffer->setDebugName(inDebugName);
	}
	return buffer;
}

IndexBuffer* D3DDevice::createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	D3DIndexBuffer* buffer = new D3DIndexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

Texture* D3DDevice::createTexture(const TextureCreateParams& createParams)
{
	D3DTexture* texture = new D3DTexture;
	texture->initialize(createParams);
	return texture;
}

ShaderStage* D3DDevice::createShader(EShaderStage shaderStage, const char* debugName)
{
	return new D3DShaderStage(shaderStage, debugName);
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

	D3DGraphicsPipelineState* pipeline = new D3DGraphicsPipelineState;
	pipeline->initialize(device.Get(), d3d_desc);

	return pipeline;
}

PipelineState* D3DDevice::createComputePipelineState(const ComputePipelineDesc& desc)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC d3d_desc;
	into_d3d::computePipelineDesc(desc, d3d_desc);

	D3DComputePipelineState* pipeline = new D3DComputePipelineState;
	pipeline->initialize(device.Get(), d3d_desc);

	return pipeline;
}

DescriptorHeap* D3DDevice::createDescriptorHeap(const DescriptorHeapDesc& desc)
{
	D3D12_DESCRIPTOR_HEAP_DESC d3d_desc;
	into_d3d::descriptorHeapDesc(desc, d3d_desc);

	D3DDescriptorHeap* heap = new D3DDescriptorHeap(desc);
	heap->initialize(device.Get(), d3d_desc);

	return heap;
}

ConstantBuffer* D3DDevice::createConstantBuffer(uint32 totalBytes)
{
	D3DConstantBuffer* cb = new D3DConstantBuffer;
	cb->initialize(totalBytes);
	return cb;
}

StructuredBuffer* D3DDevice::createStructuredBuffer(
	uint32 numElements,
	uint32 stride,
	EBufferAccessFlags accessFlags)
{
	D3DStructuredBuffer* buffer = new D3DStructuredBuffer;
	buffer->initialize(numElements, stride, accessFlags);
	return buffer;
}

void D3DDevice::copyDescriptors(
	uint32 numDescriptors,
	DescriptorHeap* destHeap,
	uint32 destHeapDescriptorStartOffset,
	DescriptorHeap* srcHeap,
	uint32 srcHeapDescriptorStartOffset)
{
	EDescriptorHeapType srcType = srcHeap->getDesc().type;
	EDescriptorHeapType dstType = destHeap->getDesc().type;
	if (dstType == EDescriptorHeapType::CBV_SRV_UAV) {
		CHECK(srcType == EDescriptorHeapType::CBV
			|| srcType == EDescriptorHeapType::SRV
			|| srcType == EDescriptorHeapType::UAV
			|| srcType == EDescriptorHeapType::CBV_SRV_UAV);
	} else {
		CHECK(srcType == dstType);
	}

	ID3D12DescriptorHeap* rawDestHeap = static_cast<D3DDescriptorHeap*>(destHeap)->getRaw();
	ID3D12DescriptorHeap* rawSrcHeap = static_cast<D3DDescriptorHeap*>(srcHeap)->getRaw();

	uint64 descSize = 0;
	switch (dstType)
	{
		case EDescriptorHeapType::CBV:         descSize = descSizeCBV_SRV_UAV; break;
		case EDescriptorHeapType::SRV:         descSize = descSizeCBV_SRV_UAV; break;
		case EDescriptorHeapType::UAV:         descSize = descSizeCBV_SRV_UAV; break;
		case EDescriptorHeapType::CBV_SRV_UAV: descSize = descSizeCBV_SRV_UAV; break;
		case EDescriptorHeapType::SAMPLER:     descSize = descSizeSampler; break;
		case EDescriptorHeapType::RTV:         descSize = descSizeRTV; break;
		case EDescriptorHeapType::DSV:         descSize = descSizeDSV; break;
		default:                               CHECK_NO_ENTRY(); break;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE destHandle = rawDestHeap->GetCPUDescriptorHandleForHeapStart();
	destHandle.ptr += descSize * destHeapDescriptorStartOffset;

	D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = rawSrcHeap->GetCPUDescriptorHandleForHeapStart();
	srcHandle.ptr += descSize * srcHeapDescriptorStartOffset;

	device->CopyDescriptorsSimple(
		numDescriptors,
		destHandle,
		srcHandle,
		into_d3d::descriptorHeapType(dstType));
}
