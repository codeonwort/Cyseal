#pragma once

#if COMPILE_BACKEND_VULKAN

#include "render/render_command.h"
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

	virtual void reset() override;

	inline VkCommandPool getRawCommandPool() const { return vkCommandPool; }
	inline VkCommandBuffer getRawCommandBuffer() const { return vkCommandBuffer; }

private:
	VkDevice vkDevice = VK_NULL_HANDLE;
	VkCommandPool vkCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer vkCommandBuffer = VK_NULL_HANDLE;
};

class VulkanRenderCommandList : public RenderCommandList
{
public:
	virtual void initialize(RenderDevice* renderDevice) override;

	virtual void reset(RenderCommandAllocator* allocator) override;
	virtual void close() override;

	virtual void iaSetPrimitiveTopology(EPrimitiveTopology topology) override;
	virtual void iaSetVertexBuffers(int32 startSlot, uint32 numViews, VertexBuffer* const* vertexBuffers) override;
	virtual void iaSetIndexBuffer(IndexBuffer* indexBuffer) override;

	virtual void rsSetViewport(const Viewport& viewport) override;
	virtual void rsSetScissorRect(const ScissorRect& scissorRect) override;

	virtual void resourceBarriers(
		uint32 numBarriers,
		const ResourceBarrier* barriers) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void clearRenderTargetView(RenderTargetView* RTV, const float* rgba) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void clearDepthStencilView(DepthStencilView* DSV, EDepthClearFlags clearFlags, float depth, uint8_t stencil) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setPipelineState(PipelineState* state) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setGraphicsRootSignature(RootSignature* rootSignature) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setGraphicsRootConstant32(uint32 rootParameterIndex, uint32 constant32, uint32 destOffsetIn32BitValues) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setGraphicsRootDescriptorTable(uint32 rootParameterIndex, DescriptorHeap* descriptorHeap, uint32 descriptorStartOffset) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setGraphicsRootDescriptorSRV(
		uint32 rootParameterIndex,
		ShaderResourceView* srv)
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

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

	virtual void beginEventMarker(const char* eventName) override;
	virtual void endEventMarker() override;

public:
//private:
	VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;
};

#endif // COMPILE_BACKEND_VULKAN
