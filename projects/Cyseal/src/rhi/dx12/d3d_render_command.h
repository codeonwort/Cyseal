#pragma once

#include "rhi/render_command.h"
#include "d3d_device.h"
#include "d3d_util.h"

class ShaderResourceView;

class D3DRenderCommandQueue : public RenderCommandQueue
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;
	virtual void executeCommandList(class RenderCommandList* commandList) override;

	inline ID3D12CommandQueue* getRaw() const { return queue.Get(); }

private:
	D3DDevice* device;
	WRL::ComPtr<ID3D12CommandQueue> queue;
};

class D3DRenderCommandAllocator : public RenderCommandAllocator
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;

	inline ID3D12CommandAllocator* getRaw() const { return allocator.Get(); }

protected:
	virtual void onReset() override;

private:
	D3DDevice* device;
	WRL::ComPtr<ID3D12CommandAllocator> allocator;
};

class D3DRenderCommandList : public RenderCommandList
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;

	// ------------------------------------------------------------------------
	// Common

	virtual void reset(RenderCommandAllocator* allocator) override;
	virtual void close() override;

	virtual void resourceBarriers(
		uint32 numBufferMemoryBarriers, const BufferMemoryBarrier* bufferMemoryBarriers,
		uint32 numTextureMemoryBarriers, const TextureMemoryBarrier* textureMemoryBarriers) override;

	virtual void clearRenderTargetView(
		RenderTargetView* RTV,
		const float* rgba) override;

	virtual void clearDepthStencilView(
		DepthStencilView* DSV,
		EDepthClearFlags clearFlags,
		float depth,
		uint8_t stencil) override;

	// ------------------------------------------------------------------------
	// Pipeline state (graphics, compute, raytracing)

	virtual void setGraphicsPipelineState(GraphicsPipelineState* state) override;
	virtual void setComputePipelineState(ComputePipelineState* state) override;
	virtual void setRaytracingPipelineState(RaytracingPipelineStateObject* rtpso) override;

	virtual void setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps) override;
	virtual void setComputeRootSignature(RootSignature* rootSignature) override;

	// ------------------------------------------------------------------------
	// Graphics pipeline

	virtual void iaSetPrimitiveTopology(EPrimitiveTopology topology) override;
	virtual void iaSetVertexBuffers(
		int32 startSlot,
		uint32 numViews,
		VertexBuffer* const* vertexBuffers) override;
	virtual void iaSetIndexBuffer(IndexBuffer* indexBuffer) override;

	virtual void rsSetViewport(const Viewport& viewport) override;
	virtual void rsSetScissorRect(const ScissorRect& scissorRect) override;

	virtual void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) override;
	virtual void omSetRenderTargets(uint32 numRTVs, RenderTargetView* const* RTVs, DepthStencilView* DSV) override;

	virtual void bindGraphicsShaderParameters(PipelineState* pipelineState, const ShaderParameterTable* parameters, DescriptorHeap* descriptorHeap) override;

	virtual void updateGraphicsRootConstants(PipelineState* pipelineState, const ShaderParameterTable* parameters) override;

	virtual void drawIndexedInstanced(
		uint32 indexCountPerInstance,
		uint32 instanceCount,
		uint32 startIndexLocation,
		int32 baseVertexLocation,
		uint32 startInstanceLocation) override;

	virtual void drawInstanced(
		uint32 vertexCountPerInstance,
		uint32 instanceCount,
		uint32 startVertexLocation,
		uint32 startInstanceLocation) override;

	virtual void executeIndirect(
		CommandSignature* commandSignature,
		uint32 maxCommandCount,
		Buffer* argumentBuffer,
		uint64 argumentBufferOffset,
		Buffer* countBuffer = nullptr,
		uint64 countBufferOffset = 0) override;

	// ------------------------------------------------------------------------
	// Compute pipeline

	// NOTE: SRV or UAV root descriptors can only be Raw or Structured buffers.
	virtual void setComputeRootDescriptorSRV(uint32 rootParameterIndex, ShaderResourceView* srv) override;

	virtual void setComputeRootDescriptorTable(
		uint32 rootParameterIndex,
		DescriptorHeap* descriptorHeap,
		uint32 descriptorStartOffset) override;

	virtual void bindComputeShaderParameters(PipelineState* pipelineState, const ShaderParameterTable* parameters, DescriptorHeap* descriptorHeap) override;

	virtual void dispatchCompute(uint32 threadGroupX, uint32 threadGroupY, uint32 threadGroupZ) override;

	// ------------------------------------------------------------------------
	// Raytracing pipeline

	virtual AccelerationStructure* buildRaytracingAccelerationStructure(
		uint32 numBLASDesc,
		BLASInstanceInitDesc* blasDescArray) override;

	virtual void dispatchRays(const DispatchRaysDesc& dispatchDesc) override;

	// ------------------------------------------------------------------------
	// Auxiliaries

	virtual void beginEventMarker(const char* eventName) override;
	virtual void endEventMarker() override;

	inline ID3D12GraphicsCommandList4* getRaw() const { return commandList.Get(); }

private:
	D3DDevice* device;
	D3DRenderCommandAllocator* commandAllocator;
	WRL::ComPtr<ID3D12GraphicsCommandList4> commandList;
};
