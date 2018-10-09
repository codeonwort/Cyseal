#pragma once

#include "render/resource_view.h"
#include "d3d_util.h"

class D3DRenderTargetView : public RenderTargetView
{

public:
	D3D12_CPU_DESCRIPTOR_HANDLE getRaw() const { return handle; }
	void setRaw(D3D12_CPU_DESCRIPTOR_HANDLE rawHandle) { handle = rawHandle; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE handle;

};

class D3DDepthStencilView : public DepthStencilView
{

public:
	D3D12_CPU_DESCRIPTOR_HANDLE getRaw() const { return handle; }
	void setRaw(D3D12_CPU_DESCRIPTOR_HANDLE rawHandle) { handle = rawHandle; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE handle;

};

class D3DUnorderedAccessView : public UnorderedAccessView
{
	//
};
