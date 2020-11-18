#pragma once

#include "gpu_resource.h"
#include "pipeline_state.h"

// Forward Declarations
class RenderDevice;
class RenderTargetView;
class DepthStencilView;
class PipelineState;
class RootSignature;
class VertexBuffer;
class IndexBuffer;

// #todo: implement
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

	virtual void iaSetPrimitiveTopology(EPrimitiveTopology topology) = 0;
	virtual void iaSetVertexBuffers(
		int32 startSlot,
		uint32 numViews,
		VertexBuffer* const* vertexBuffers) = 0;
	virtual void iaSetIndexBuffer(IndexBuffer* indexBuffer) = 0;

	// #todo: multiple viewports and scissor rects
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

	// #todo: MRT
	virtual void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) = 0;

	virtual void setPipelineState(PipelineState* state) = 0;
	virtual void setGraphicsRootSignature(RootSignature* rootSignature) = 0;

	// #todo-fatal: setDescriptorHeaps
	virtual void setDescriptorHeaps(uint32 count, DescriptorHeap* heaps) = 0;
	// #todo-fatal: setGraphicsRootDescriptorTable
	virtual void setGraphicsRootParameter(uint32 rootParameterIndex, ConstantBuffer* constantBuffer) = 0;

	virtual void drawIndexedInstanced(
		uint32 indexCountPerInstance,
		uint32 instanceCount,
		uint32 startIndexLocation,
		int32 baseVertexLocation,
		uint32 startInstanceLocation) = 0;

};
