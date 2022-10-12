#pragma once

#include "render/resource_view.h"
#include "d3d_util.h"
#include "d3d_texture.h"

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

class D3DShaderResourceView : public ShaderResourceView
{
public:
	D3DShaderResourceView(Texture* inOwner) : ShaderResourceView(inOwner) {}
	D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle() const { return cpuHandle; }
	void setCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE inHandle) { cpuHandle = inHandle; }
	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress()
	{
		return static_cast<D3DTexture*>(owner)->getGPUVirtualAddress();
	}
private:
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { NULL };
};

class D3DUnorderedAccessView : public UnorderedAccessView
{
	//
};
