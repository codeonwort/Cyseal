#pragma once

class RenderDevice;

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

};
