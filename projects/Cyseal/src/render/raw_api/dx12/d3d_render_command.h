#pragma once

#include "render/render_command.h"
#include "d3d_device.h"
#include "d3d_util.h"

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
	virtual void reset() override;
	virtual void close() override;

	virtual void iaSetPrimitiveTopology(EPrimitiveTopology topology) override;
	virtual void iaSetVertexBuffers(
		int32_t startSlot,
		uint32_t numViews,
		VertexBuffer* const* vertexBuffers) override;
	virtual void iaSetIndexBuffer(IndexBuffer* indexBuffer) override;

	virtual void rsSetViewport(const Viewport& viewport) override;
	virtual void rsSetScissorRect(const ScissorRect& scissorRect) override;

	virtual void transitionResource(
		GPUResource* resource,
		EGPUResourceState stateBefore,
		EGPUResourceState stateAfter) override;

	virtual void clearRenderTargetView(
		RenderTargetView* RTV,
		const float* rgba) override;

	virtual void clearDepthStencilView(
		DepthStencilView* DSV,
		EClearFlags clearFlags,
		float depth,
		uint8_t stencil) override;

	virtual void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) override;

	virtual void setPipelineState(PipelineState* state) override;
	virtual void setGraphicsRootSignature(RootSignature* rootSignature) override;

	virtual void drawIndexedInstanced(
		uint32_t indexCountPerInstance,
		uint32_t instanceCount,
		uint32_t startIndexLocation,
		int32_t baseVertexLocation,
		uint32_t startInstanceLocation) override;

	inline ID3D12GraphicsCommandList* getRaw() const { return commandList.Get(); }

private:
	D3DDevice* device;
	D3DRenderCommandAllocator* commandAllocator;
	WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

};