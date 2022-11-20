#pragma once

#include "render/gpu_resource_view.h"
#include "d3d_util.h"
#include "d3d_texture.h"

class D3DConstantBuffer;
class DescriptorHeap;

class D3DRenderTargetView : public RenderTargetView
{
public:
	D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle() const { return handle; }
	void setCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE rawHandle) { handle = rawHandle; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE handle;
};

class D3DDepthStencilView : public DepthStencilView
{
public:
	D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle() const { return handle; }
	void setCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE rawHandle) { handle = rawHandle; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE handle;
};

class D3DShaderResourceView : public ShaderResourceView
{
public:
	D3DShaderResourceView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, D3D12_CPU_DESCRIPTOR_HANDLE inCpuHandle)
		: ShaderResourceView(inOwner, inSourceHeap, inDescriptorIndex)
		, cpuHandle(inCpuHandle)
	{}

	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const;
	D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle() const { return cpuHandle; }
	
private:
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { NULL };
};

class D3DUnorderedAccessView : public UnorderedAccessView
{
public:
	D3DUnorderedAccessView(
			GPUResource* inOwner,
			DescriptorHeap* inSourceHeap,
			uint32 inDescriptorIndex,
			D3D12_CPU_DESCRIPTOR_HANDLE inCpuHandle)
		: UnorderedAccessView(inOwner, inSourceHeap, inDescriptorIndex)
		, cpuHandle(inCpuHandle)
	{}

	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const;
	D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle() const { return cpuHandle; }
	
private:
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = { NULL };
};

class D3DConstantBufferView : public ConstantBufferView
{
public:
	D3DConstantBufferView(D3DConstantBuffer* inBuffer, DescriptorHeap* inSourceHeap, uint32 inOffsetInBuffer, uint32 inSizeAligned, uint32 inBufferingCount)
		: buffer(inBuffer)
		, sourceHeap(inSourceHeap)
		, offsetInBuffer(inOffsetInBuffer)
		, sizeAligned(inSizeAligned)
	{
		descriptorIndexArray.resize(inBufferingCount, 0xffffffff);
	}

	virtual void upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex) override;

	virtual DescriptorHeap* getSourceHeap() override
	{
		return sourceHeap;
	}

	virtual uint32 getDescriptorIndexInHeap(uint32 bufferingIndex) const override
	{
		return descriptorIndexArray[bufferingIndex];
	}

	void initialize(uint32 inDescriptorIndex, uint32 inBufferingIndex)
	{
		descriptorIndexArray[inBufferingIndex] = inDescriptorIndex;
	}

	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress();

private:
	D3DConstantBuffer* buffer;
	DescriptorHeap* sourceHeap;
	uint32 offsetInBuffer;
	uint32 sizeAligned;
	std::vector<uint32> descriptorIndexArray;
};
