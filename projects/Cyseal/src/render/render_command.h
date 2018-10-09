#pragma once

#include "viewport.h"
#include "gpu_resource.h"

class RenderDevice;
class RenderTargetView;
class DepthStencilView;

class RenderCommand
{

public:
	//

};

// ID3D12CommandQueue
// VkQueue
class RenderCommandQueue
{
	
public:
	virtual ~RenderCommandQueue();

	virtual void initialize(RenderDevice* renderDevice) = 0;
	virtual void executeCommandList(class RenderCommandList* commandList) = 0;

};

// ID3D12CommandAllocator
// VkCommandPool
class RenderCommandAllocator
{

public:
	virtual ~RenderCommandAllocator();

	virtual void initialize(RenderDevice* renderDevice) = 0;
	virtual void reset() = 0;

};

// ID3D12CommandList
// VkCommandBuffer
class RenderCommandList
{
	
public:
	virtual ~RenderCommandList();

	virtual void initialize(RenderDevice* renderDevice) = 0;
	virtual void reset() = 0;
	virtual void close() = 0;

	// TODO: multiple viewports and scissor rects
	virtual void rsSetViewport(const Viewport& viewport) = 0;
	virtual void rsSetScissorRect(const ScissorRect& scissorRect) = 0;

	virtual void transitionResource(
		GPUResource* resource,
		EGPUResourceState stateBefore,
		EGPUResourceState stateAfter) = 0;

	virtual void clearRenderTargetView(
		RenderTargetView* RTV,
		const float* rgba) = 0;

	virtual void clearDepthStencilView(
		DepthStencilView* DSV,
		EClearFlags clearFlags,
		float depth,
		uint8_t stencil) = 0;

	// TODO: MRT
	virtual void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) = 0;

};
