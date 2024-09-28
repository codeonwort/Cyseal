#pragma once

#include "rhi_forward.h"
#include "gpu_resource.h"
#include "gpu_resource_view.h"
#include "buffer.h"
#include "texture.h"
#include "pipeline_state.h"
#include "shader.h"
#include "pixel_format.h"
#include "render_device_capabilities.h"
#include "util/logging.h"

enum class ERenderDeviceRawAPI
{
	DirectX12,
	Vulkan
};

enum class EWindowType
{
	FULLSCREEN,
	BORDERLESS,
	WINDOWED
};

struct RenderDeviceCreateParams
{
	void*                    nativeWindowHandle  = nullptr;
	ERenderDeviceRawAPI      rawAPI;

	// Required capability tiers
	ERaytracingTier          raytracingTier      = ERaytracingTier::MaxTier;
	EVariableShadingRateTier vrsTier             = EVariableShadingRateTier::MaxTier;
	EMeshShaderTier          meshShaderTier      = EMeshShaderTier::MaxTier;
	ESamplerFeedbackTier     samplerFeedbackTier = ESamplerFeedbackTier::MaxTier;

	// Enable debug layer (dx) or validation layer (vk)
	bool                     enableDebugLayer    = true;

	// true  : Render for current swapchain, record for next swapchain.
	// false : Record for current swapchain, render for current swapchain.
	bool                     bDoubleBuffering    = true;

	// #todo-renderdevice: These are not renderdevice params. Move to somewhere.
	// or leave here as initial values.
	EWindowType              windowType          = EWindowType::WINDOWED;
	uint32                   windowWidth         = 1920;
	uint32                   windowHeight        = 1080;
};

// ID3D12Device
// VkDevice
class RenderDevice
{
	
public:
	RenderDevice() = default;
	virtual ~RenderDevice() = default;

	void initialize(const RenderDeviceCreateParams& inCreateParams)
	{
		createParams = inCreateParams;
		onInitialize(createParams);
	}
	virtual void onInitialize(const RenderDeviceCreateParams& createParams) = 0;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) = 0;

	virtual void flushCommandQueue() = 0;

	// ------------------------------------------------------------------------
	// Plugin: DearImgui

	virtual void initializeDearImgui();
	virtual void beginDearImguiNewFrame() = 0;
	virtual void renderDearImgui(RenderCommandList* commandList) = 0;
	virtual void shutdownDearImgui();
	inline DescriptorHeap* getDearImguiSRVHeap() const { return imguiSRVHeap; }

	// ------------------------------------------------------------------------
	// Create

	// #todo-renderdevice: Remove createVertexBuffer and createIndexBuffer?
	// #todo-renderdevice: uint64 for sizeInBytes
	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, EBufferAccessFlags usageFlags, const wchar_t* inDebugName = nullptr) = 0;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual IndexBuffer* createIndexBuffer(uint32 sizeInBytes, EPixelFormat format, EBufferAccessFlags usageFlags, const wchar_t* inDebugName = nullptr) = 0;
	virtual IndexBuffer* createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes, EPixelFormat format) = 0;

	virtual Buffer* createBuffer(const BufferCreateParams& createParams) = 0;
	virtual Texture* createTexture(const TextureCreateParams& createParams) = 0;

	virtual ShaderStage* createShader(EShaderStage shaderStage, const char* debugName) = 0;

	virtual RootSignature* createRootSignature(const RootSignatureDesc& desc) = 0;
	virtual GraphicsPipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) = 0;
	virtual ComputePipelineState* createComputePipelineState(const ComputePipelineDesc& desc) = 0;
	
	virtual RaytracingPipelineStateObject* createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc& desc) = 0;
	virtual RaytracingPipelineStateObject* createRaytracingPipelineStateObject(const RaytracingPipelineStateObjectDesc2& desc) = 0;

	// NOTE: shaderRecordSize = shaderIdentifierSize + rootArgumentSize,
	// but shaderIdentifierSize is API-specific, so we specify only rootArgumentSize here.
	virtual RaytracingShaderTable* createRaytracingShaderTable(
		RaytracingPipelineStateObject* RTPSO,
		uint32 numShaderRecords,
		uint32 rootArgumentSize,
		const wchar_t* debugName) = 0;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) = 0;

	// Allocate a descriptor from the specified descriptor heap.
	virtual ConstantBufferView* createCBV(Buffer* buffer, DescriptorHeap* descriptorHeap, uint32 sizeInBytes, uint32 offsetInBytes) = 0;
	virtual ShaderResourceView* createSRV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const ShaderResourceViewDesc& createParams) = 0;
	virtual UnorderedAccessView* createUAV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const UnorderedAccessViewDesc& createParams) = 0;
	virtual RenderTargetView* createRTV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const RenderTargetViewDesc& createParams) = 0;
	virtual DepthStencilView* createDSV(GPUResource* gpuResource, DescriptorHeap* descriptorHeap, const DepthStencilViewDesc& createParams) = 0;

	// Allocate a descriptor from a global descriptor heap.
	virtual ShaderResourceView* createSRV(GPUResource* gpuResource, const ShaderResourceViewDesc& createParams) = 0;
	virtual UnorderedAccessView* createUAV(GPUResource* gpuResource, const UnorderedAccessViewDesc& createParams) = 0;
	virtual RenderTargetView* createRTV(GPUResource* gpuResource, const RenderTargetViewDesc& createParams) = 0;
	virtual DepthStencilView* createDSV(GPUResource* gpuResource, const DepthStencilViewDesc& createParams) = 0;

	// Indirect draw
	virtual CommandSignature* createCommandSignature(const CommandSignatureDesc& inDesc, GraphicsPipelineState* inPipelineState) = 0;
	virtual IndirectCommandGenerator* createIndirectCommandGenerator(const CommandSignatureDesc& inDesc, uint32 maxCommandCount) = 0;

	// ------------------------------------------------------------------------
	// Copy

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) = 0;

	// ------------------------------------------------------------------------
	// Getters

	inline const RenderDeviceCreateParams& getCreateParams() const { return createParams; }

	inline EPixelFormat getBackbufferFormat() const { return backbufferFormat; }
	inline EPixelFormat getBackbufferDepthFormat() const { return backbufferDepthFormat; }
	inline SwapChain* getSwapChain() const { return swapChain; }

	inline RenderCommandAllocator* getCommandAllocator(uint32 swapchainIndex) const { return commandAllocators[swapchainIndex]; }
	inline RenderCommandList* getCommandList(uint32 swapchainIndex) const { return commandLists[swapchainIndex]; }
	inline RenderCommandQueue* getCommandQueue() const { return commandQueue; }

	inline ERaytracingTier getRaytracingTier() const { return raytracingTier; }
	inline EVariableShadingRateTier getVRSTier() const { return vrsTier; }
	inline EMeshShaderTier getMeshShaderTier() const { return meshShaderTier; }
	inline ESamplerFeedbackTier getSamplerFeedbackTier() const { return samplerFeedbackTier; }

	virtual uint32 getConstantBufferDataAlignment() const = 0;

protected:
	RenderDeviceCreateParams createParams;

	// #todo-renderdevice: Move backbuffer formats to swapchain
	EPixelFormat            backbufferFormat = EPixelFormat::R8G8B8A8_UNORM;
	EPixelFormat            backbufferDepthFormat = EPixelFormat::D24_UNORM_S8_UINT;
	SwapChain*              swapChain = nullptr;

	DescriptorHeap*         imguiSRVHeap = nullptr;

	// https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles
	// Command allocators should hold memory for render commands while GPU is accessing them,
	// but command lists can immediately reset after a recording set is done.
	// So, it would be...
	//   0. Prepare alloc0 and alloc1 for double buffering
	//   1. cmdList->reset(alloc0)
	//   2. Record commands
	//   3. Wait until commands allocated in alloc1 are finished
	//   4. Submit commands allocated in alloc0 to the queue
	//   5. Repeat 1~4, but allocators swapped.
	// Therefore only one command list is needed in theory, but
	// RenderCommandList also has some utils like customCommands and deferredDeallocs.
	// So I'll just create command lists as many as allocators.
	std::vector<RenderCommandAllocator*> commandAllocators;
	std::vector<RenderCommandList*> commandLists;
	RenderCommandQueue* commandQueue = nullptr; // Primary graphics queue. Later other queues can be added (e.g., async compute queue).

	// Capabilities
	ERaytracingTier raytracingTier = ERaytracingTier::NotSupported;
	EVariableShadingRateTier vrsTier = EVariableShadingRateTier::NotSupported;
	EMeshShaderTier meshShaderTier = EMeshShaderTier::NotSupported;
	ESamplerFeedbackTier samplerFeedbackTier = ESamplerFeedbackTier::NotSupported;
};

extern RenderDevice* gRenderDevice;
DECLARE_LOG_CATEGORY(LogDevice);
