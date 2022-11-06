#pragma once

#include "render/gpu_resource_view.h"
#include "d3d_util.h"
#include "d3d_texture.h"

class D3DConstantBuffer;

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

// #todo-dx12: D3DUnorderedAccessView
class D3DUnorderedAccessView : public UnorderedAccessView
{
	//
};

class D3DConstantBufferView : public ConstantBufferView
{
public:
	D3DConstantBufferView(D3DConstantBuffer* inBuffer, uint32 inOffsetInBuffer, uint32 inSizeAligned, uint32 inBufferingCount)
		: buffer(inBuffer)
		, offsetInBuffer(inOffsetInBuffer)
		, sizeAligned(inSizeAligned)
	{
		descriptorIndexArray.resize(inBufferingCount, 0xffffffff);
	}

	virtual void upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex);

	virtual uint32 getDescriptorIndexInHeap(uint32 bufferingIndex) const
	{
		return descriptorIndexArray[bufferingIndex];
	}

	void initialize(uint32 inDescriptorIndex, uint32 inBufferingIndex)
	{
		descriptorIndexArray[inBufferingIndex] = inDescriptorIndex;
	}

private:
	D3DConstantBuffer* buffer;
	uint32 offsetInBuffer;
	uint32 sizeAligned;
	std::vector<uint32> descriptorIndexArray;
};
