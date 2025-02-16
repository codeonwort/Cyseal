#pragma once

#if COMPILE_BACKEND_VULKAN

#include "rhi/render_command.h"
#include "rhi/gpu_resource_barrier.h"
#include "vk_device.h"
#include <vulkan/vulkan_core.h>

class ShaderResourceView;

class VulkanRenderCommandQueue : public RenderCommandQueue
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;

	virtual void executeCommandList(class RenderCommandList* commandList) override;

private:
	VulkanDevice* deviceWrapper = nullptr;
	VkQueue vkGraphicsQueue = VK_NULL_HANDLE;
};

class VulkanRenderCommandAllocator : public RenderCommandAllocator
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;

	inline VkCommandPool getRawCommandPool() const { return vkCommandPool; }
	inline VkCommandBuffer getRawCommandBuffer() const { return vkCommandBuffer; }

protected:
	virtual void onReset() override;

private:
	VkDevice vkDevice = VK_NULL_HANDLE;
	VkCommandPool vkCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
};

class VulkanRenderCommandList : public RenderCommandList
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;

	// ------------------------------------------------------------------------
	// Common

	virtual void reset(RenderCommandAllocator* allocator) override;
	virtual void close() override;

	virtual void resourceBarriers(
		uint32 numBufferMemoryBarriers, const BufferMemoryBarrier* bufferMemoryBarriers,
		uint32 numTexureMemoryBarriers, const TextureMemoryBarrier* textureMemoryBarriers,
		uint32 numUAVBarriers, GPUResource* const* uavBarrierResources) override;

	virtual void clearRenderTargetView(RenderTargetView* RTV, const float* rgba) override;
	virtual void clearDepthStencilView(DepthStencilView* DSV, EDepthClearFlags clearFlags, float depth, uint8_t stencil) override;

	virtual void copyTexture2D(Texture* src, Texture* dst) override;

	// ------------------------------------------------------------------------
	// Pipeline state (graphics, compute, raytracing)

	virtual void setGraphicsPipelineState(GraphicsPipelineState* state) override;
	virtual void setComputePipelineState(ComputePipelineState* state) override;
	virtual void setRaytracingPipelineState(RaytracingPipelineStateObject* rtpso) override;

	virtual void setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps) override;

	// ------------------------------------------------------------------------
	// Graphics pipeline

	virtual void iaSetPrimitiveTopology(EPrimitiveTopology topology) override;
	virtual void iaSetVertexBuffers(int32 startSlot, uint32 numViews, VertexBuffer* const* vertexBuffers) override;
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

	virtual void bindComputeShaderParameters(PipelineState* pipelineState, const ShaderParameterTable* parameters, DescriptorHeap* descriptorHeap, DescriptorIndexTracker* tracker) override;

	virtual void dispatchCompute(uint32 threadGroupX, uint32 threadGroupY, uint32 threadGroupZ) override;

	// ------------------------------------------------------------------------
	// Raytracing pipeline

	virtual AccelerationStructure* buildRaytracingAccelerationStructure(uint32 numBLASDesc, BLASInstanceInitDesc* blasDescArray) override;

	virtual void bindRaytracingShaderParameters(RaytracingPipelineStateObject* pipelineState, const ShaderParameterTable* parameters, DescriptorHeap* descriptorHeap, DescriptorHeap* samplerHeap) override;

	virtual void dispatchRays(const DispatchRaysDesc& dispatchDesc) override;

	// ------------------------------------------------------------------------
	// Auxiliaries

	virtual void beginEventMarker(const char* eventName) override;
	virtual void endEventMarker() override;

public:
//private:
	VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
