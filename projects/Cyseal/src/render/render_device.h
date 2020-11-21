#pragma once

#include "core/types.h"
#include "pixel_format.h"
#include <Windows.h> // #todo-crossplatform: Windows only for now

class SwapChain;
class RenderCommandAllocator;
class RenderCommandList;
class RenderCommandQueue;
class GPUResource;
class DepthStencilView;
class VertexBuffer;
class IndexBuffer;
class Shader;
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

enum class ERayTracingTier
{
	NotSupported,
	Tier_1_0
};

enum class EWindowType
{
	FULLSCREEN,
	BORDERLESS,
	WINDOWED
};

struct RenderDeviceCreateParams
{
	HWND hwnd;
	ERenderDeviceRawAPI rawAPI;
	ERayTracingTier rayTracingTier;
	bool enableDebugLayer = true; // Enable debug layer (dx) or validation layer (vk)

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

	virtual void recreateSwapChain(HWND hwnd, uint32 width, uint32 height) = 0;

	virtual void flushCommandQueue() = 0;

	virtual bool supportsRayTracing() = 0;

	virtual VertexBuffer* createVertexBuffer(void* data, uint32 sizeInBytes, uint32 strideInBytes) = 0;
	virtual IndexBuffer* createIndexBuffer(void* data, uint32 sizeInBytes, EPixelFormat format) = 0;
	virtual Shader* createShader() = 0;
	virtual RootSignature* createRootSignature(const RootSignatureDesc& desc) = 0;
	virtual PipelineState* createGraphicsPipelineState(const GraphicsPipelineDesc& desc) = 0;

	virtual DescriptorHeap* createDescriptorHeap(const DescriptorHeapDesc& desc) = 0;
	// heapSize must be a multiple of 64K
	// payloadSize must be a multiple of 256
	virtual ConstantBuffer* createConstantBuffer(DescriptorHeap* descriptorHeap, uint32 heapSize, uint32 payloadSize) = 0;

	inline EPixelFormat getBackbufferFormat() const { return backbufferFormat; }
	inline EPixelFormat getBackbufferDepthFormat() const { return backbufferDepthFormat; }
	inline SwapChain* getSwapChain() const { return swapChain; }
	inline GPUResource* getDefaultDepthStencilBuffer() const { return defaultDepthStencilBuffer; }
	inline DepthStencilView* getDefaultDSV() const { return defaultDSV; }

	inline RenderCommandAllocator* getCommandAllocator() const { return commandAllocator; }
	inline RenderCommandList* getCommandList() const { return commandList; }
	inline RenderCommandQueue* getCommandQueue() const { return commandQueue; }

protected:
	EPixelFormat            backbufferFormat = EPixelFormat::R8G8B8A8_UNORM;
	EPixelFormat            backbufferDepthFormat = EPixelFormat::D24_UNORM_S8_UINT;
	SwapChain*              swapChain;
	GPUResource*            defaultDepthStencilBuffer;
	DepthStencilView*       defaultDSV;

	RenderCommandAllocator* commandAllocator;
	RenderCommandQueue*     commandQueue;
	RenderCommandList*      commandList;

};

extern RenderDevice* gRenderDevice;
