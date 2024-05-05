#pragma once

#include "rhi/gpu_resource_view.h"
#include "d3d_util.h"
#include "d3d_texture.h"

class D3DBuffer;
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
	D3DDepthStencilView(GPUResource* inOwner, DescriptorHeap* inSourceHeap, uint32 inDescriptorIndex, D3D12_CPU_DESCRIPTOR_HANDLE inCpuHandle)
		: DepthStencilView(inOwner, inSourceHeap, inDescriptorIndex)
		, cpuHandle(inCpuHandle)
	{
	}

	D3D12_CPU_DESCRIPTOR_HANDLE getCPUHandle() const { return cpuHandle; }
	void setCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE rawHandle) { cpuHandle = rawHandle; }

private:
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
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
	D3DConstantBufferView(D3DBuffer* inBuffer, DescriptorHeap* inSourceHeap, uint32 inOffsetInBuffer, uint32 inSizeAligned)
		: buffer(inBuffer)
		, sourceHeap(inSourceHeap)
		, offsetInBuffer(inOffsetInBuffer)
		, sizeAligned(inSizeAligned)
	{
		descriptorIndex = 0xffffffff;
	}

	virtual void writeToGPU(RenderCommandList* commandList, void* srcData, uint32 sizeInBytes) override;

	virtual DescriptorHeap* getSourceHeap() const override { return sourceHeap; }

	virtual uint32 getDescriptorIndexInHeap() const override { return descriptorIndex; }

	void initialize(uint32 inDescriptorIndex) { descriptorIndex = inDescriptorIndex; }

	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress();

private:
	D3DBuffer* buffer;
	DescriptorHeap* sourceHeap;
	uint32 offsetInBuffer;
	uint32 sizeAligned;
	uint32 descriptorIndex;
};
