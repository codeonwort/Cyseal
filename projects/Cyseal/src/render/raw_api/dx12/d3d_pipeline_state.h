#pragma once

#include "core/assertion.h"
#include "render/pipeline_state.h"
#include "render/gpu_resource_binding.h"
#include "d3d_util.h"
#include <vector>

class D3DRootSignature;
class D3DShaderStage;

inline uint32 align(uint32 size, uint32 alignment)
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

class D3DGraphicsPipelineState : public PipelineState
{
public:
	void initialize(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
	{
		HR( device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&rawPSO)) );
	}
	ID3D12PipelineState* getRaw() const { return rawPSO.Get(); }
private:
	WRL::ComPtr<ID3D12PipelineState> rawPSO;
};

class D3DComputePipelineState : public PipelineState
{
public:
	void initialize(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc)
	{
		HR(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&rawPSO)));
	}
	ID3D12PipelineState* getRaw() const { return rawPSO.Get(); }
private:
	WRL::ComPtr<ID3D12PipelineState> rawPSO;
};

class D3DRaytracingPipelineStateObject : public RaytracingPipelineStateObject
{
public:
	void initialize(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC& desc)
	{
		HR(device->CreateStateObject(&desc, IID_PPV_ARGS(&rawRTPSO)));
		HR(rawRTPSO.As(&rawProperties));
	}
	ID3D12StateObject* getRaw() const { return rawRTPSO.Get(); }
	ID3D12StateObjectProperties* getRawProperties() const { return rawProperties.Get(); }

private:
	WRL::ComPtr<ID3D12StateObject> rawRTPSO;
	WRL::ComPtr<ID3D12StateObjectProperties> rawProperties;
};

class D3DRootSignature : public RootSignature
{
public:
	void initialize(
		ID3D12Device* device,
		uint32 nodeMask,
		const void* blobWithRootSignature,
		size_t blobLengthInBytes)
	{
		HR( device->CreateRootSignature(
			nodeMask,
			blobWithRootSignature,
			blobLengthInBytes,
			IID_PPV_ARGS(&rawRootSignature))
		);
	}
	inline ID3D12RootSignature* getRaw() const
	{
		return rawRootSignature.Get();
	}
private:
	WRL::ComPtr<ID3D12RootSignature> rawRootSignature;
};

class D3DRaytracingShaderTable : public RaytracingShaderTable
{
public:
	D3DRaytracingShaderTable(
		ID3D12Device* inDevice,
		D3DRaytracingPipelineStateObject* inRTPSO,
		uint32 inNumShaderRecords,
		uint32 inRootArgumentSize,
		const wchar_t* inDebugName)
	{
		RTPSO = inRTPSO;
		allocateUploadBuffer(inDevice, inNumShaderRecords, inRootArgumentSize, inDebugName);
	}

	~D3DRaytracingShaderTable()
	{
		if (rawUploadBuffer.Get())
		{
			rawUploadBuffer->Unmap(0, nullptr);
		}
	}

	virtual void uploadRecord(
		uint32 recordIndex,
		ShaderStage* raytracingShader,
		void* rootArgumentData,
		uint32 rootArgumentSize) override
	{
		const wchar_t* exportName = raytracingShader->getEntryPointW();
		uploadRecord(recordIndex, exportName, rootArgumentData, rootArgumentSize);
	}

	virtual void uploadRecord(
		uint32 recordIndex,
		const wchar_t* shaderExportName,
		void* rootArgumentData,
		uint32 rootArgumentSize) override
	{
		void* shaderIdPtr = RTPSO->getRawProperties()->GetShaderIdentifier(shaderExportName);

		uint8* mapDest = mappedResource + (recordIndex * shaderRecordSize);
		memcpy_s(mapDest, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
			shaderIdPtr, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		if (rootArgumentData != nullptr && rootArgumentSize > 0)
		{
			memcpy_s(mapDest + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, rootArgumentSize,
				rootArgumentData, rootArgumentSize);
		}
	}

private:
	void allocateUploadBuffer(
		ID3D12Device* d3dDevice,
		uint32 numShaderRecords,
		uint32 rootArgumentSize,
		const wchar_t* debugName = nullptr)
	{
		uint32 shaderRecordSizeTemp = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + rootArgumentSize;
		shaderRecordSize = align(shaderRecordSizeTemp, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		
		rawUploadBufferSize = numShaderRecords * shaderRecordSize;

		auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(rawUploadBufferSize);

		HR(d3dDevice->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&rawUploadBuffer)));
		
		if (debugName != nullptr)
		{
			rawUploadBuffer->SetName(debugName);
		}

		CD3DX12_RANGE readRange(0, 0);
		rawUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedResource));
	}

private:
	D3DRaytracingPipelineStateObject* RTPSO;
	uint32 shaderRecordSize; // Aligned

	WRL::ComPtr<ID3D12Resource> rawUploadBuffer;
	uint32 rawUploadBufferSize;
	uint8* mappedResource = nullptr;
};
