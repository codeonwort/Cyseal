#include "d3d_device.h"
#include "d3d_buffer.h"
#include "d3d_texture.h"
#include "d3d_shader.h"
#include "d3d_render_command.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "d3d_into.h"
#include "rhi/global_descriptor_heaps.h"
#include "core/assertion.h"
#include "util/logging.h"

#include "imgui_impl_dx12.h"

// #todo-crossapi: Dynamic loading
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#if _DEBUG
#include <dxgidebug.h>
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
#define CYSEAL_D3D_SHADER_MODEL_MINIMUM D3D_SHADER_MODEL_6_0 /* Minimum required SM to run Cyseal */
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
	: RenderDevice()
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
	for (size_t i = 0; i < commandLists.size(); ++i)
	{
		delete commandLists[i];
	}
	delete commandQueue;
}

void D3DDevice::onInitialize(const RenderDeviceCreateParams& createParams)
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
		CYLOG(LogDirectX, Log, L"> min(requested, maxSupported) tiers will be used");
		CYLOG(LogDirectX, Log, L"Cap: DXR             requested=%S\tmaxSupported=%S", toString(createParams.raytracingTier), toString(raytracingTier));
		CYLOG(LogDirectX, Log, L"Cap: VRS             requested=%S\tmaxSupported=%S", toString(createParams.vrsTier), toString(vrsTier));
		CYLOG(LogDirectX, Log, L"Cap: MeshShader      requested=%S\tmaxSupported=%S", toString(createParams.meshShaderTier), toString(meshShaderTier));
		CYLOG(LogDirectX, Log, L"Cap: SamplerFeedback requested=%S\tmaxSupported=%S", toString(createParams.samplerFeedbackTier), toString(samplerFeedbackTier));

		raytracingTier = static_cast<ERaytracingTier>((std::min)((uint8)createParams.raytracingTier, (uint8)raytracingTier));
		vrsTier = static_cast<EVariableShadingRateTier>((std::min)((uint8)createParams.vrsTier, (uint8)vrsTier));
		meshShaderTier = static_cast<EMeshShaderTier>((std::min)((uint8)createParams.meshShaderTier, (uint8)meshShaderTier));
		samplerFeedbackTier = static_cast<ESamplerFeedbackTier>((std::min)((uint8)createParams.samplerFeedbackTier, (uint8)samplerFeedbackTier));
	}

	// 2. Create a ID3D12Fence and retrieve sizes of descriptors.
	HR( device->CreateFence(currentFence, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) );

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

		RenderCommandList* commandList = new D3DRenderCommandList;
		commandList->initialize(this);
		commandLists.push_back(commandList);
	}

	// Shader management

	D3D12_FEATURE_DATA_SHADER_MODEL SM = { CYSEAL_D3D_SHADER_MODEL_HIGHEST };
	HR( device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &SM, sizeof(SM)) );
	if (SM.HighestShaderModel < CYSEAL_D3D_SHADER_MODEL_MINIMUM)
	{
		CYLOG(LogDirectX, Fatal, L"Current PC does not support minimum required Shader Model");
		CHECK_NO_ENTRY();
	}
	highestShaderModel = SM.HighestShaderModel;

	HR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)));
	HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)));
	HR(dxcUtils->CreateDefaultIncludeHandler(&dxcIncludeHandler));
}

void D3DDevice::initializeDearImgui()
{
	RenderDevice::initializeDearImgui();

	ID3D12DescriptorHeap* d3dHeap = static_cast<D3DDescriptorHeap*>(getDearImguiSRVHeap())->getRaw();
	auto backbufferFormat = into_d3d::pixelFormat(swapChain->getBackbufferFormat());

	ImGui_ImplDX12_Init(
		device.Get(),
		swapChain->getBufferCount(),
		backbufferFormat,
		d3dHeap,
		d3dHeap->GetCPUDescriptorHandleForHeapStart(),
		d3dHeap->GetGPUDescriptorHandleForHeapStart());
}

void D3DDevice::beginDearImguiNewFrame()
{
	ImGui_ImplDX12_NewFrame();
}

void D3DDevice::renderDearImgui(RenderCommandList* commandList)
{
	auto d3dCmdList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3dCmdList);
}

void D3DDevice::shutdownDearImgui()
{
	RenderDevice::shutdownDearImgui();

	ImGui_ImplDX12_Shutdown();
}

void D3DDevice::recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height)
{
	swapChain->resize(width, height);

	// Recreate command lists.
	// If a command list refers to a backbuffer that is already deleted, it results in an error.
	for (RenderCommandList* commandList : commandLists)
	{
		delete commandList;
	}
	commandLists.clear();

	for (uint32 ix = 0; ix < swapChain->getBufferCount(); ++ix)
	{
		RenderCommandList* commandList = new D3DRenderCommandList;
		commandList->initialize(this);
		commandLists.push_back(commandList);
	}
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

VertexBuffer* D3DDevice::createVertexBuffer(
	uint32 sizeInBytes, EBufferAccessFlags usageFlags,
	const wchar_t* inDebugName)
{
	D3DVertexBuffer* buffer = new D3DVertexBuffer;
	buffer->initialize(sizeInBytes, usageFlags);
	if (inDebugName != nullptr)
	{
		buffer->setDebugName(inDebugName);
	}
	return buffer;
}

VertexBuffer* D3DDevice::createVertexBuffer(
	VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes)
{
	D3DVertexBuffer* buffer = new D3DVertexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

IndexBuffer* D3DDevice::createIndexBuffer(
	uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags,
	const wchar_t* inDebugName)
{
	D3DIndexBuffer* buffer = new D3DIndexBuffer;
	buffer->initialize(sizeInBytes, format, usageFlags);
	if (inDebugName != nullptr)
	{
		buffer->setDebugName(inDebugName);
	}
	return buffer;
}

IndexBuffer* D3DDevice::createIndexBuffer(
	IndexBufferPool* pool, uint64 offsetInPool,
	uint32 sizeInBytes, EPixelFormat format)
{
	D3DIndexBuffer* buffer = new D3DIndexBuffer;
	buffer->initializeWithinPool(pool, offsetInPool, sizeInBytes);
	return buffer;
}

Buffer* D3DDevice::createBuffer(const BufferCreateParams& createParams)
{
	D3DBuffer* buffer = new D3DBuffer;
	buffer->initialize(createParams);
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

GraphicsPipelineState* D3DDevice::createGraphicsPipelineState(const GraphicsPipelineDesc& inDesc)
{
	D3DGraphicsPipelineState* pipeline = new D3DGraphicsPipelineState;
	pipeline->initialize(device.Get(), inDesc);
	return pipeline;
}

ComputePipelineState* D3DDevice::createComputePipelineState(const ComputePipelineDesc& inDesc)
{
	D3DComputePipelineState* pipeline = new D3DComputePipelineState;
	pipeline->initialize(device.Get(), inDesc);
	return pipeline;
}

RaytracingPipelineStateObject* D3DDevice::createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc& desc)
{
	CD3DX12_STATE_OBJECT_DESC d3d_desc{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

	// DXIL library
	auto createRTShaderSubobject = [&](ShaderStage* shaderStage)
	{
		if (shaderStage != nullptr)
		{
			D3DShaderStage* d3dShader = static_cast<D3DShaderStage*>(shaderStage);
			D3D12_SHADER_BYTECODE shaderBytecode = d3dShader->getBytecode();

			auto lib = d3d_desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			lib->SetDXILLibrary(&shaderBytecode);
			lib->DefineExport(d3dShader->getEntryPointW());
		}
	};
	createRTShaderSubobject(desc.raygenShader);
	createRTShaderSubobject(desc.closestHitShader);
	createRTShaderSubobject(desc.missShader);

	// Hit group
	auto hitGroup = d3d_desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	if (desc.closestHitShader != nullptr)
	{
		hitGroup->SetClosestHitShaderImport(
			static_cast<D3DShaderStage*>(desc.closestHitShader)->getEntryPointW());
	}
	// #todo-dxr: anyHitShader, intersectionShader
	hitGroup->SetHitGroupExport(desc.hitGroupName.c_str());
	hitGroup->SetHitGroupType(into_d3d::hitGroupType(desc.hitGroupType));

	// Shader config
	auto shaderConfig = d3d_desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	CHECK(desc.maxAttributeSizeInBytes < D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES);
	shaderConfig->Config(desc.maxPayloadSizeInBytes, desc.maxAttributeSizeInBytes);

	// Local root signature
	auto createLocalRootSignature = [&](ShaderStage* shader, RootSignature* rootSig)
	{
		if (shader != nullptr && rootSig != nullptr)
		{
			auto shaderName = static_cast<D3DShaderStage*>(shader)->getEntryPointW();
			auto d3dRootSig = static_cast<D3DRootSignature*>(rootSig)->getRaw();

			auto localSig = d3d_desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			localSig->SetRootSignature(d3dRootSig);
			auto assoc = d3d_desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			assoc->SetSubobjectToAssociate(*localSig);
			assoc->AddExport(shaderName);
		}
	};
	createLocalRootSignature(desc.raygenShader, desc.raygenLocalRootSignature);
	createLocalRootSignature(desc.closestHitShader, desc.closestHitLocalRootSignature);
	createLocalRootSignature(desc.missShader, desc.missLocalRootSignature);

	// Global root signature
	auto globalSig = d3d_desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalSig->SetRootSignature(static_cast<D3DRootSignature*>(desc.globalRootSignature)->getRaw());

	// Pipeline config
	auto pipelineConfig = d3d_desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	pipelineConfig->Config(desc.maxTraceRecursionDepth);

	D3DRaytracingPipelineStateObject* RTPSO = new D3DRaytracingPipelineStateObject;
	RTPSO->initialize(device.Get(), d3d_desc);
	return RTPSO;
}

RaytracingPipelineStateObject* D3DDevice::createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc2& desc)
{
	D3DRaytracingPipelineStateObject* RTPSO = new D3DRaytracingPipelineStateObject;
	RTPSO->initialize(device.Get(), desc);
	return RTPSO;
}

RaytracingShaderTable* D3DDevice::createRaytracingShaderTable(
	RaytracingPipelineStateObject* RTPSO,
	uint32 numShaderRecords,
	uint32 rootArgumentSize,
	const wchar_t* debugName)
{
	auto d3dRTPSO = static_cast<D3DRaytracingPipelineStateObject*>(RTPSO);
	D3DRaytracingShaderTable* table = new D3DRaytracingShaderTable(device.Get(), d3dRTPSO, numShaderRecords, rootArgumentSize, debugName);
	return table;
}

DescriptorHeap* D3DDevice::createDescriptorHeap(const DescriptorHeapDesc& desc)
{
	D3D12_DESCRIPTOR_HEAP_DESC d3dDesc;
	into_d3d::descriptorHeapDesc(desc, d3dDesc);

	D3DDescriptorHeap* heap = new D3DDescriptorHeap(desc);
	heap->initialize(device.Get(), d3dDesc);

	return heap;
}

ConstantBufferView* D3DDevice::createCBV(
	Buffer* buffer,
	DescriptorHeap* descriptorHeap,
	uint32 sizeInBytes,
	uint32 offsetInBytes)
{
	CHECK(descriptorHeap->getCreateParams().type == EDescriptorHeapType::CBV || descriptorHeap->getCreateParams().type == EDescriptorHeapType::CBV_SRV_UAV);
	CHECK(offsetInBytes % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0); // 256

	D3DDevice* d3dDevice = static_cast<D3DDevice*>(gRenderDevice);
	D3DBuffer* d3dBuffer = static_cast<D3DBuffer*>(buffer);
	ID3D12DescriptorHeap* id3d12Heap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	uint32 sizeAligned = align(sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	D3DConstantBufferView* cbv = new D3DConstantBufferView(
		d3dBuffer, descriptorHeap, offsetInBytes, sizeAligned);

	D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc;
	viewDesc.BufferLocation = into_d3d::id3d12Resource(buffer)->GetGPUVirtualAddress() + offsetInBytes;
	viewDesc.SizeInBytes = sizeAligned;

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = id3d12Heap->GetCPUDescriptorHandleForHeapStart();
	uint32 descriptorIndex = descriptorHeap->allocateDescriptorIndex();
	cpuHandle.ptr += descriptorIndex * d3dDevice->getDescriptorSizeCbvSrvUav();

	d3dDevice->getRawDevice()->CreateConstantBufferView(&viewDesc, cpuHandle);

	cbv->initialize(descriptorIndex);

	return cbv;
}

ShaderResourceView* D3DDevice::createSRV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const ShaderResourceViewDesc& createParams)
{
	CHECK(descriptorHeap->getCreateParams().type == EDescriptorHeapType::SRV || descriptorHeap->getCreateParams().type == EDescriptorHeapType::CBV_SRV_UAV);

	ID3D12DescriptorHeap* d3dHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	const uint32 descriptorIndex = descriptorHeap->allocateDescriptorIndex();

	ID3D12Resource* d3dResource = into_d3d::id3d12Resource(gpuResource);
	D3D12_SHADER_RESOURCE_VIEW_DESC d3dDesc = into_d3d::srvDesc(createParams);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = d3dHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += SIZE_T(descriptorIndex) * SIZE_T(descSizeCBV_SRV_UAV);
	device->CreateShaderResourceView(d3dResource, &d3dDesc, cpuHandle);

	return new D3DShaderResourceView(gpuResource, descriptorHeap, descriptorIndex, cpuHandle);
}

ShaderResourceView* D3DDevice::createSRV(GPUResource* gpuResource, const ShaderResourceViewDesc& createParams)
{
	return createSRV(gpuResource, gDescriptorHeaps->getSRVHeap(), createParams);
}

RenderTargetView* D3DDevice::createRTV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const RenderTargetViewDesc& createParams)
{
	CHECK(descriptorHeap->getCreateParams().type == EDescriptorHeapType::RTV);

	ID3D12DescriptorHeap* d3dHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	const uint32 descriptorIndex = descriptorHeap->allocateDescriptorIndex();

	ID3D12Resource* d3dResource = into_d3d::id3d12Resource(gpuResource);
	D3D12_RENDER_TARGET_VIEW_DESC d3dDesc = into_d3d::rtvDesc(createParams);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = d3dHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += SIZE_T(descriptorIndex) * SIZE_T(descSizeRTV);
	device->CreateRenderTargetView(d3dResource, &d3dDesc, cpuHandle);

	return new D3DRenderTargetView(gpuResource, descriptorHeap, descriptorIndex, cpuHandle);
}

RenderTargetView* D3DDevice::createRTV(GPUResource* gpuResource, const RenderTargetViewDesc& createParams)
{
	return createRTV(gpuResource, gDescriptorHeaps->getRTVHeap(), createParams);
}

UnorderedAccessView* D3DDevice::createUAV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const UnorderedAccessViewDesc& createParams)
{
	CHECK(descriptorHeap->getCreateParams().type == EDescriptorHeapType::UAV);

	ID3D12DescriptorHeap* d3dHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	const uint32 descriptorIndex = descriptorHeap->allocateDescriptorIndex();

	ID3D12Resource* d3dResource = into_d3d::id3d12Resource(gpuResource);
	ID3D12Resource* counterResource = NULL; // #todo-renderdevice: UAV counter resource
	D3D12_UNORDERED_ACCESS_VIEW_DESC d3dDesc = into_d3d::uavDesc(createParams);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = d3dHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += SIZE_T(descriptorIndex) * SIZE_T(descSizeCBV_SRV_UAV);
	device->CreateUnorderedAccessView(d3dResource, counterResource, &d3dDesc, cpuHandle);

	return new D3DUnorderedAccessView(gpuResource, descriptorHeap, descriptorIndex, cpuHandle);
}

UnorderedAccessView* D3DDevice::createUAV(GPUResource* gpuResource, const UnorderedAccessViewDesc& createParams)
{
	return createUAV(gpuResource, gDescriptorHeaps->getUAVHeap(), createParams);
}

DepthStencilView* D3DDevice::createDSV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const DepthStencilViewDesc& createParams)
{
	CHECK(descriptorHeap->getCreateParams().type == EDescriptorHeapType::DSV);

	ID3D12DescriptorHeap* d3dHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	const uint32 descriptorIndex = descriptorHeap->allocateDescriptorIndex();

	ID3D12Resource* d3dResource = into_d3d::id3d12Resource(gpuResource);
	D3D12_DEPTH_STENCIL_VIEW_DESC d3dDesc = into_d3d::dsvDesc(createParams);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = d3dHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += SIZE_T(descriptorIndex) * SIZE_T(descSizeDSV);
	device->CreateDepthStencilView(d3dResource, &d3dDesc, cpuHandle);

	return new D3DDepthStencilView(gpuResource, descriptorHeap, descriptorIndex, cpuHandle);
}

DepthStencilView* D3DDevice::createDSV(GPUResource* gpuResource, const DepthStencilViewDesc& createParams)
{
	return createDSV(gpuResource, gDescriptorHeaps->getDSVHeap(), createParams);
}

CommandSignature* D3DDevice::createCommandSignature(const CommandSignatureDesc& inDesc, GraphicsPipelineState* inPipelineState)
{
	D3DGraphicsPipelineState* d3dPipelineState = static_cast<D3DGraphicsPipelineState*>(inPipelineState);

	into_d3d::TempAlloc tempAlloc;
	D3D12_COMMAND_SIGNATURE_DESC d3dDesc;
	into_d3d::commandSignature(inDesc, d3dDesc, d3dPipelineState, tempAlloc);

	ID3D12RootSignature* rootSig = (d3dPipelineState != nullptr) ? d3dPipelineState->getRootSignature() : nullptr;

	D3DCommandSignature* cmdSig = new D3DCommandSignature;
	cmdSig->initialize(device.Get(), d3dDesc, rootSig);
	return cmdSig;
}

IndirectCommandGenerator* D3DDevice::createIndirectCommandGenerator(const CommandSignatureDesc& sigDesc, uint32 maxCommandCount)
{
	D3DIndirectCommandGenerator* gen = new D3DIndirectCommandGenerator;
	gen->initialize(sigDesc, maxCommandCount);
	return gen;
}

void D3DDevice::copyDescriptors(
	uint32 numDescriptors,
	DescriptorHeap* destHeap,
	uint32 destHeapDescriptorStartOffset,
	DescriptorHeap* srcHeap,
	uint32 srcHeapDescriptorStartOffset)
{
	EDescriptorHeapType srcType = srcHeap->getCreateParams().type;
	EDescriptorHeapType dstType = destHeap->getCreateParams().type;
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
