#pragma once

#include "render/render_command.h"
#include "d3d_device.h"
#include "d3d_util.h"

class D3DRenderCommandQueue : public RenderCommandQueue
{
	//
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
	//

};
