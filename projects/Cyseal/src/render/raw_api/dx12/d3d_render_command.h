#pragma once

#include "render/render_command.h"
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
	virtual void reset() override;

	inline ID3D12CommandAllocator* getRaw() const { return allocator.Get(); }

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
		uint32 numBarriers,
		const ResourceBarrier* barriers) override;

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

	virtual void setPipelineState(PipelineState* state) override;
	virtual void setRaytracingPipelineState(RaytracingPipelineStateObject* rtpso) override;

	virtual void setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps) override;
	virtual void setGraphicsRootSignature(RootSignature* rootSignature) override;
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

	virtual void setGraphicsRootConstant32(
		uint32 rootParameterIndex,
		uint32 constant32,
		uint32 destOffsetIn32BitValues) override;
	virtual void setGraphicsRootConstant32Array(
		uint32 rootParameterIndex,
		uint32 numValuesToSet,
		const void* srcData,
		uint32 destOffsetIn32BitValues) override;

	virtual void setGraphicsRootDescriptorTable(
		uint32 rootParameterIndex,
		DescriptorHeap* descriptorHeap,
		uint32 descriptorStartOffset) override;

	virtual void setGraphicsRootDescriptorSRV(uint32 rootParameterIndex, ShaderResourceView* srv) override;
	virtual void setGraphicsRootDescriptorCBV(uint32 rootParameterIndex, ConstantBufferView* cbv) override;
	virtual void setGraphicsRootDescriptorUAV(uint32 rootParameterIndex, UnorderedAccessView* uav) override;

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

	// ------------------------------------------------------------------------
	// Compute pipeline

	virtual void setComputeRootConstant32(
		uint32 rootParameterIndex,
		uint32 constant32,
		uint32 destOffsetIn32BitValues) override;
	virtual void setComputeRootConstant32Array(
		uint32 rootParameterIndex,
		uint32 numValuesToSet,
		const void* srcData,
		uint32 destOffsetIn32BitValues) override;

	// NOTE: SRV or UAV root descriptors can only be Raw or Structured buffers.
	virtual void setComputeRootDescriptorSRV(uint32 rootParameterIndex, ShaderResourceView* srv) override;
	virtual void setComputeRootDescriptorCBV(uint32 rootParameterIndex, ConstantBufferView* cbv) override;
	virtual void setComputeRootDescriptorUAV(uint32 rootParameterIndex, UnorderedAccessView* uav) override;

	virtual void setComputeRootDescriptorTable(
		uint32 rootParameterIndex,
		DescriptorHeap* descriptorHeap,
		uint32 descriptorStartOffset) override;

	virtual void dispatchCompute(
		uint32 threadGroupX,
		uint32 threadGroupY,
		uint32 threadGroupZ) override;

	// ------------------------------------------------------------------------
	// Raytracing pipeline

	virtual AccelerationStructure* buildRaytracingAccelerationStructure(
		uint32 numGeomDesc,
		RaytracingGeometryDesc* geomDescArray) override;

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
