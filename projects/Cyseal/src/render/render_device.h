#pragma once

#include "core/types.h"
#include "util/logging.h"
#include "pixel_format.h"
#include "texture.h"
#include "shader.h"
#include "render_device_capabilities.h"

class SwapChain;
class RenderCommandAllocator;
class RenderCommandList;
class RenderCommandQueue;
class GPUResource;
class DepthStencilView;
class VertexBuffer;
class VertexBufferPool;
class IndexBuffer;
struct RootSignatureDesc;
class RootSignature;
struct GraphicsPipelineDesc;
class PipelineState;
struct DescriptorHeapDesc;
class DescriptorHeap;
class ConstantBuffer;

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
	void* nativeWindowHandle;
	ERenderDeviceRawAPI rawAPI;

	// Required capability tiers
	ERaytracingTier raytracingTier = ERaytracingTier::MaxTier;
	EVariableShadingRateTier vrsTier = EVariableShadingRateTier::MaxTier;
	EMeshShaderTier meshShaderTier = EMeshShaderTier::MaxTier;
	ESamplerFeedbackTier samplerFeedbackTier = ESamplerFeedbackTier::MaxTier;

	bool enableDebugLayer = true;   // Enable debug layer (dx) or validation layer (vk)

	// #todo-renderdevice: These are not renderdevice params. Move to somewhere.
	// or leave here as initial values.
	EWindowType windowType = EWindowType::WINDOWED;
	uint32 windowWidth = 1920;
	uint32 windowHeight = 1080;
};

// ID3D12Device
// VkDevice
class RenderDevice
{
	
public:
	RenderDevice();
	virtual ~RenderDevice();

	virtual void initialize(const RenderDeviceCreateParams& createParams) = 0;

	virtual void recreateSwapChain(void* nativeWindowHandle, uint32 width, uint32 height) = 0;

	virtual void flushCommandQueue() = 0;

	// #todo-renderdevice: uint64 for sizeInBytes
	virtual VertexBuffer* createVertexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName = nullptr) = 0;
	virtual VertexBuffer* createVertexBuffer(VertexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual IndexBuffer* createIndexBuffer(uint32 sizeInBytes, const wchar_t* inDebugName = nullptr) = 0;
	virtual IndexBuffer* createIndexBuffer(IndexBufferPool* pool, uint64 offsetInPool, uint32 sizeInBytes) = 0;

	virtual Texture* createTexture(const TextureCreateParams& createParams) = 0;

	virtual ShaderStage* createShader(EShaderStage shaderStage, const char* debugName) = 0;

	virtual RootSignature* createRootSignature(const RootSignatureDesc& desc) = 0;
	virtual PipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) = 0;
	virtual PipelineState* createComputePipelineState(const ComputePipelineDesc& desc) = 0;
	
	virtual RaytracingPipelineStateObject* createRaytracingPipelineStateObject(
		const RaytracingPipelineStateObjectDesc& desc) = 0;

	// NOTE: shaderRecordSize = shaderIdentifierSize + rootArgumentSize,
	// but shaderIdentifierSize is API-specific, so we specify only rootArgumentSize here.
	virtual RaytracingShaderTable* createRaytracingShaderTable(
		RaytracingPipelineStateObject* RTPSO,
		uint32 numShaderRecords,
		uint32 rootArgumentSize,
		const wchar_t* debugName) = 0;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) = 0;

	virtual ConstantBuffer* createConstantBuffer(uint32 totalBytes) = 0;
	virtual StructuredBuffer* createStructuredBuffer(
		uint32 numElements,
		uint32 stride,
		EBufferAccessFlags accessFlags) = 0;

	virtual void copyDescriptors(
		uint32 numDescriptors,
		DescriptorHeap* destHeap,
		uint32 destHeapDescriptorStartOffset,
		DescriptorHeap* srcHeap,
		uint32 srcHeapDescriptorStartOffset) = 0;

	inline EPixelFormat getBackbufferFormat() const { return backbufferFormat; }
	inline EPixelFormat getBackbufferDepthFormat() const { return backbufferDepthFormat; }
	inline SwapChain* getSwapChain() const { return swapChain; }

	inline RenderCommandAllocator* getCommandAllocator(uint32 swapchainIndex) const { return commandAllocators[swapchainIndex]; }
	inline RenderCommandList* getCommandList() const { return commandList; }
	inline RenderCommandQueue* getCommandQueue() const { return commandQueue; }

	inline ERaytracingTier getRaytracingTier() const { return raytracingTier; }
	inline EVariableShadingRateTier getVRSTier() const { return vrsTier; }
	inline EMeshShaderTier getMeshShaderTier() const { return meshShaderTier; }
	inline ESamplerFeedbackTier getSamplerFeedbackTier() const { return samplerFeedbackTier; }

protected:
	// #todo-renderdevice: Move backbuffer formats to swapchain
	EPixelFormat            backbufferFormat = EPixelFormat::R8G8B8A8_UNORM;
	EPixelFormat            backbufferDepthFormat = EPixelFormat::D24_UNORM_S8_UINT;
	SwapChain*              swapChain = nullptr;

	// https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles
	// Command allocators should hold memory for render commands while GPU is accessing them,
	// but command lists can immediately reset after a recording set is done.
	// So, it would be...
	// 0. Prepare alloc0 and alloc1 for double buffering
	// 1. cmdList->reset(alloc0)
	// 2. Record commands
	// 3. Wait until commands allocated in alloc1 are finished
	// 4. Submit commands allocated in alloc0 to the queue
	// 5. Repeat 1~4, but allocators swapped.
	std::vector<RenderCommandAllocator*> commandAllocators;
	RenderCommandQueue* commandQueue = nullptr; // Primary graphics queue. Later other queues can be added (e.g., async compute queue).
	RenderCommandList* commandList = nullptr;

	// Capabilities
	ERaytracingTier raytracingTier = ERaytracingTier::NotSupported;
	EVariableShadingRateTier vrsTier = EVariableShadingRateTier::NotSupported;
	EMeshShaderTier meshShaderTier = EMeshShaderTier::NotSupported;
	ESamplerFeedbackTier samplerFeedbackTier = ESamplerFeedbackTier::NotSupported;
};

extern RenderDevice* gRenderDevice;
DECLARE_LOG_CATEGORY(LogDevice);
