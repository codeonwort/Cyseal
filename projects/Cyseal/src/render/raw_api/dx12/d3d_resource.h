#pragma once

#include "render/gpu_resource.h"
#include "render/gpu_resource_binding.h"
#include "core/assertion.h"
#include "d3d_util.h"
#include <memory>

class D3DShaderResourceView;
class D3DUnorderedAccessView;

// --------------------------------------------------------------

// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12descriptorheap
class D3DDescriptorHeap : public DescriptorHeap
{
public:
	D3DDescriptorHeap(const DescriptorHeapDesc& desc)
		: DescriptorHeap(desc)
	{}

	virtual void setDebugName(const wchar_t* name)
	{
		rawState->SetName(name);
	}

	void initialize(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
	{
		HR( device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rawState)) );
	}

	ID3D12DescriptorHeap* getRaw() const { return rawState.Get(); }

private:
	WRL::ComPtr<ID3D12DescriptorHeap> rawState;
};

// https://www.braynzarsoft.net/viewtutorial/q16390-directx-12-constant-buffers-root-descriptor-tables
class D3DConstantBuffer : public ConstantBuffer
{
	friend class D3DConstantBufferView;

public:
	~D3DConstantBuffer();

	virtual void initialize(uint32 sizeInBytes);

	virtual ConstantBufferView* allocateCBV(DescriptorHeap* descHeap, uint32 sizeInBytes, uint32 bufferingCount);

	void destroy();

	// NOTE: CBVs should add their offset.
	D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const
	{
		return memoryPool->GetGPUVirtualAddress();
	}

private:
	WRL::ComPtr<ID3D12Resource> memoryPool;
	uint32 totalBytes = 0;
	uint32 allocatedBytes = 0;
	uint8* mapPtr = nullptr;
};

class D3DStructuredBuffer : public StructuredBuffer
{
public:
	void initialize(
		uint32 inNumElements,
		uint32 inStride,
		EBufferAccessFlags inAccessFlags);

	virtual void uploadData(
		RenderCommandList* commandList,
		void* data,
		uint32 sizeInBytes,
		uint32 destOffsetInBytes) override;
	
	virtual ShaderResourceView* getSRV() const override;
	virtual UnorderedAccessView* getUAV() const override;

	virtual void* getRawResource() const override { return rawBuffer.Get(); }

private:
	WRL::ComPtr<ID3D12Resource> rawBuffer;
	EBufferAccessFlags accessFlags;
	uint32 totalBytes = 0;
	uint32 numElements = 0;
	uint32 stride = 0;

	std::unique_ptr<ShaderResourceView> srv;
	std::unique_ptr<UnorderedAccessView> uav;

	// #todo: Don't wanna hold an upload heap here...
	// At least create it only if accessFlags has EBufferAccessFlags::CPU_WRITE.
	WRL::ComPtr<ID3D12Resource> rawUploadBuffer;
};

class D3DAccelerationStructure : public AccelerationStructure
{
public:
	virtual ~D3DAccelerationStructure();

	virtual void* getRawResource() const override { return tlasResource.Get(); }

	virtual ShaderResourceView* getSRV() const override;

	void initialize(uint32 numBLAS);

	void buildBLAS(
		ID3D12GraphicsCommandList4* commandList,
		uint32 blasIndex,
		const BLASInstanceInitDesc& blasDesc,
		const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs);

	void waitForBLASBuild(ID3D12GraphicsCommandList4* commandList);

	void buildTLAS(
		ID3D12GraphicsCommandList4* commandList,
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags);

	virtual void rebuildTLAS(
		RenderCommandList* commandList,
		uint32 numInstanceUpdates,
		const BLASInstanceUpdateDesc* updateDescs) override;

	// TLAS is bound as SRV when setting root parameters.
	inline D3D12_GPU_VIRTUAL_ADDRESS getTLASGpuVirtualAddress() const {
		return tlasResource->GetGPUVirtualAddress();
	}

private:
	// #todo-dx12: Promote to common utils.
	// They are all around in my D3D wrapper classes...
	void allocateUAVBuffer(
		UINT64 bufferSize,
		ID3D12Resource** ppResource,
		D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON,
		const wchar_t* resourceName = nullptr);

	void allocateUploadBuffer(
		void* pData,
		UINT64 datasize,
		ID3D12Resource** ppResource,
		const wchar_t* resourceName = nullptr);

	std::unique_ptr<D3DShaderResourceView> srv;

	uint32 totalBLAS = 0;
	std::vector<WRL::ComPtr<ID3D12Resource>> blasResourceArray;
	std::vector<WRL::ComPtr<ID3D12Resource>> blasScratchResourceArray;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS tlasBuildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	WRL::ComPtr<ID3D12Resource> tlasResource;
	WRL::ComPtr<ID3D12Resource> tlasScratchResource;

	WRL::ComPtr<ID3D12Resource> instanceDescBuffer;
	uint8* instanceDescMapPtr = nullptr;
};
