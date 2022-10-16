#pragma once

#include "render/render_command.h"
#include "vk_device.h"

#if !COMPILE_BACKEND_VULKAN

class VulkanRenderCommandQueue : public RenderCommandQueue {};
class VulkanRenderCommandAllocator : public RenderCommandAllocator {};
class VulkanRenderCommandList : public RenderCommandList {};

#else // !COMPILE_BACKEND_VULKAN

#include <vulkan/vulkan_core.h>

class ShaderResourceView;

class VulkanRenderCommandQueue : public RenderCommandQueue
{
public:
	void initialize(RenderDevice* renderDevice) override;

	void executeCommandList(class RenderCommandList* commandList) override;

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

	void initialize(RenderDevice* renderDevice) override;

	void reset(RenderCommandAllocator* allocator) override;

	void close() override;

	void iaSetPrimitiveTopology(EPrimitiveTopology topology) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void iaSetVertexBuffers(int32 startSlot, uint32 numViews, VertexBuffer* const* vertexBuffers) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void iaSetIndexBuffer(IndexBuffer* indexBuffer) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void rsSetViewport(const Viewport& viewport) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void rsSetScissorRect(const ScissorRect& scissorRect) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void transitionResource(GPUResource* resource, EGPUResourceState stateBefore, EGPUResourceState stateAfter) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void resourceBarriers(
		uint32 numBarriers,
		const ResourceBarrier* barriers) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	void clearRenderTargetView(RenderTargetView* RTV, const float* rgba) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void clearDepthStencilView(DepthStencilView* DSV, EDepthClearFlags clearFlags, float depth, uint8_t stencil) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void setPipelineState(PipelineState* state) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void setGraphicsRootSignature(RootSignature* rootSignature) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void setGraphicsRootConstant32(uint32 rootParameterIndex, uint32 constant32, uint32 destOffsetIn32BitValues) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void setGraphicsRootDescriptorTable(uint32 rootParameterIndex, DescriptorHeap* descriptorHeap, uint32 descriptorStartOffset) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void setGraphicsRootDescriptorSRV(
		uint32 rootParameterIndex,
		ShaderResourceView* srv)
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}


	void drawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount, uint32 startIndexLocation, int32 baseVertexLocation, uint32 startInstanceLocation) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void drawInstanced(
		uint32 vertexCountPerInstance,
		uint32 instanceCount,
		uint32 startVertexLocation,
		uint32 startInstanceLocation) override
	{
		//throw std::logic_error("The method or operation is not implemented.");
	}

	virtual void beginEventMarker(const char* eventName) override {}
	virtual void endEventMarker() override {}

public:
//private:
	VkCommandBuffer currentCommandBuffer = VK_NULL_HANDLE;
};

#endif // !COMPILE_BACKEND_VULKAN
