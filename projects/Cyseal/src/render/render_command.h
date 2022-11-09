#pragma once

#include "gpu_resource.h"
#include "pipeline_state.h"
#include "gpu_resource_binding.h"
#include <functional>

// Forward Declarations
class RenderDevice;
class RenderTargetView;
class DepthStencilView;
class ShaderResourceView;
class PipelineState;
class RootSignature;
class VertexBuffer;
class IndexBuffer;
class DescriptorHeap;

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
	using CustomCommandType = std::function<void(RenderCommandList&)>;

	virtual ~RenderCommandList();

	virtual void initialize(RenderDevice* renderDevice) = 0;

	// ------------------------------------------------------------------------
	// Common

	// Begin command recording.
	virtual void reset(RenderCommandAllocator* allocator) = 0;
	
	// End command recording.
	virtual void close() = 0;

	virtual void resourceBarriers(uint32 numBarriers, const ResourceBarrier* barriers) = 0;

	// #todo-rendercommand: Maybe not the best way to clear RTV.
	// (Need to check how loadOp=CLEAR maps to DX12 and Vulkan.)
	virtual void clearRenderTargetView(
		RenderTargetView* RTV,
		const float* rgba) = 0;

	virtual void clearDepthStencilView(
		DepthStencilView* DSV,
		EDepthClearFlags clearFlags,
		float depth,
		uint8_t stencil) = 0;

	// ------------------------------------------------------------------------
	// Pipeline state object (graphics & compute)

	virtual void setPipelineState(PipelineState* state) = 0;
	virtual void setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps) = 0;
	virtual void setGraphicsRootSignature(RootSignature* rootSignature) = 0;
	virtual void setComputeRootSignature(RootSignature* rootSignature) = 0;

	// ------------------------------------------------------------------------
	// Graphics pipeline

	virtual void iaSetPrimitiveTopology(EPrimitiveTopology topology) = 0;
	virtual void iaSetVertexBuffers(
		int32 startSlot,
		uint32 numViews,
		VertexBuffer* const* vertexBuffers) = 0;
	virtual void iaSetIndexBuffer(IndexBuffer* indexBuffer) = 0;

	// #todo-rendercommand: multiple viewports and scissor rects
	virtual void rsSetViewport(const Viewport& viewport) = 0;
	virtual void rsSetScissorRect(const ScissorRect& scissorRect) = 0;

	virtual void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) = 0;
	virtual void omSetRenderTargets(uint32 numRTVs, RenderTargetView* const* RTVs, DepthStencilView* DSV) = 0;

	// #todo-rendercommand: What is DestOffsetIn32BitValues in ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants() method?
	virtual void setGraphicsRootConstant32(
		uint32 rootParameterIndex,
		uint32 constant32,
		uint32 destOffsetIn32BitValues) = 0;
	
	// NOTE: A sequence of 32-bit values are bound to the corresponding single register.
	virtual void setGraphicsRootConstant32Array(
		uint32 rootParameterIndex,
		uint32 numValuesToSet,
		const void* srcData,
		uint32 destOffsetIn32BitValues) = 0;

	// NOTE: SRV or UAV root descriptors can only be Raw or Structured buffers.
	virtual void setGraphicsRootDescriptorSRV(uint32 rootParameterIndex, ShaderResourceView* srv) = 0;
	virtual void setGraphicsRootDescriptorCBV(uint32 rootParameterIndex, ConstantBufferView* cbv) = 0;
	virtual void setGraphicsRootDescriptorUAV(uint32 rootParameterIndex, UnorderedAccessView* uav) = 0;

	// #todo-rendercommand: Is this the best form?
	virtual void setGraphicsRootDescriptorTable(
		uint32 rootParameterIndex,
		DescriptorHeap* descriptorHeap,
		uint32 descriptorStartOffset) = 0;

	virtual void drawIndexedInstanced(
		uint32 indexCountPerInstance,
		uint32 instanceCount,
		uint32 startIndexLocation,
		int32 baseVertexLocation,
		uint32 startInstanceLocation) = 0;

	virtual void drawInstanced(
		uint32 vertexCountPerInstance,
		uint32 instanceCount,
		uint32 startVertexLocation,
		uint32 startInstanceLocation) = 0;

	// ------------------------------------------------------------------------
	// Compute pipeline

	virtual void setComputeRootConstant32(
		uint32 rootParameterIndex,
		uint32 constant32,
		uint32 destOffsetIn32BitValues) = 0;
	virtual void setComputeRootConstant32Array(
		uint32 rootParameterIndex,
		uint32 numValuesToSet,
		const void* srcData,
		uint32 destOffsetIn32BitValues) = 0;

	// NOTE: SRV or UAV root descriptors can only be Raw or Structured buffers.
	virtual void setComputeRootDescriptorSRV(uint32 rootParameterIndex, ShaderResourceView* srv) = 0;
	virtual void setComputeRootDescriptorCBV(uint32 rootParameterIndex, ConstantBufferView* cbv) = 0;
	virtual void setComputeRootDescriptorUAV(uint32 rootParameterIndex, UnorderedAccessView* uav) = 0;

	virtual void setComputeRootDescriptorTable(
		uint32 rootParameterIndex,
		DescriptorHeap* descriptorHeap,
		uint32 descriptorStartOffset) = 0;

	virtual void dispatchCompute(
		uint32 threadGroupX,
		uint32 threadGroupY,
		uint32 threadGroupZ) = 0;

	// ------------------------------------------------------------------------
	// Auxiliaries

	virtual void beginEventMarker(const char* eventName) = 0; // For GPU debuggers
	virtual void endEventMarker() = 0;

	void enqueueCustomCommand(CustomCommandType lambda);
	void executeCustomCommands();

private:
	std::vector<CustomCommandType> customCommands;
};

// #todo-rendercommand: Currently every custom commands are executed prior to whole internal rendering pipeline.
// Needs a lambda wrapper for each internal command for perfect queueing.
struct EnqueueCustomRenderCommand
{
	EnqueueCustomRenderCommand(RenderCommandList::CustomCommandType inLambda);
};
// #todo-rendercommand: Resets the list and only executes custom commands registered so far.
// Just a hack due to incomplete render command list support.
struct FlushRenderCommands
{
	FlushRenderCommands();
};

#define ENQUEUE_RENDER_COMMAND(CommandName) EnqueueCustomRenderCommand CommandName

#define FLUSH_RENDER_COMMANDS_INTERNAL(x, y) x ## y
#define FLUSH_RENDER_COMMANDS_INTERNAL2(x, y) FLUSH_RENDER_COMMANDS_INTERNAL(x, y)
#define FLUSH_RENDER_COMMANDS() FlushRenderCommands FLUSH_RENDER_COMMANDS_INTERNAL2(flushRenderCommands_, __LINE__)

struct ScopedDrawEvent
{
	ScopedDrawEvent(RenderCommandList* inCommandList, const char* inEventName)
		: commandList(inCommandList)
	{
		commandList->beginEventMarker(inEventName);
	}
	~ScopedDrawEvent()
	{
		commandList->endEventMarker();
	}
	RenderCommandList* commandList;
};

#define SCOPED_DRAW_EVENT(commandList, eventName) ScopedDrawEvent scopedDrawEvent_##eventName(commandList, #eventName)
