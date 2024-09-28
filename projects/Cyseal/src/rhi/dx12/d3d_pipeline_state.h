#pragma once

#include "core/assertion.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "d3d_shader.h"
#include "d3d_util.h"

#include <vector>
#include <map>
#include <set>

class VertexBuffer;
class IndexBuffer;

inline uint32 align(uint32 size, uint32 alignment)
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

class D3DGraphicsPipelineState : public GraphicsPipelineState
{
public:
	void initialize(ID3D12Device* device, const GraphicsPipelineDesc& inDesc);

	inline ID3D12RootSignature* getRootSignature() const { return rootSignature.Get(); }
	inline ID3D12PipelineState* getPipelineState() const { return pipelineState.Get(); }

	const D3DShaderParameter* findShaderParameter(const std::string& name) const;

private:
	void createRootSignature(ID3D12Device* device, ShaderStage* vertexShader, ShaderStage* pixelShader, ShaderStage* domainShader, ShaderStage* hullShader, ShaderStage* geometryShader);

	D3DShaderParameterTable parameterTable; // Copied from D3DShaderStage
	std::map<std::string, const D3DShaderParameter*> parameterHashMap; // For fast query

	WRL::ComPtr<ID3D12RootSignature> rootSignature;
	WRL::ComPtr<ID3D12PipelineState> pipelineState;
};

class D3DComputePipelineState : public ComputePipelineState
{
public:
	void initialize(ID3D12Device* device, const ComputePipelineDesc& inDesc);

	inline ID3D12RootSignature* getRootSignature() const { return rootSignature.Get(); }
	inline ID3D12PipelineState* getPipelineState() const { return pipelineState.Get(); }

	const D3DShaderParameter* findShaderParameter(const std::string& name) const;

private:
	void createRootSignature(ID3D12Device* device, D3DShaderStage* computeShader);

	D3DShaderParameterTable parameterTable; // Copied from D3DShaderStage
	std::map<std::string, const D3DShaderParameter*> parameterHashMap; // For fast query

	WRL::ComPtr<ID3D12RootSignature> rootSignature;
	WRL::ComPtr<ID3D12PipelineState> pipelineState;
};

class D3DRaytracingPipelineStateObject : public RaytracingPipelineStateObject
{
public:
	void initialize(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC& desc);

	void initialize(ID3D12Device5* device, const RaytracingPipelineStateObjectDesc& desc);

	inline ID3D12RootSignature* getGlobalRootSignature() const { return globalRootSignature.Get(); }
	//inline ID3D12RootSignature* getLocalRootSignatureRaygen() const { return localRootSignatureRaygen.Get(); }
	//inline ID3D12RootSignature* getLocalRootSignatureClosestHit() const { return localRootSignatureClosestHit.Get(); }
	//inline ID3D12RootSignature* getLocalRootSignatureMiss() const { return localRootSignatureMiss.Get(); }
	//inline ID3D12RootSignature* getLocalRootSignatureAnyHit() const { return localRootSignatureAnyHit.Get(); }
	//inline ID3D12RootSignature* getLocalRootSignatureIntersection() const { return localRootSignatureIntersection.Get(); }

	inline ID3D12StateObject* getRaw() const { return rawRTPSO.Get(); }
	inline ID3D12StateObjectProperties* getRawProperties() const { return rawProperties.Get(); }

	const D3DShaderParameter* findGlobalShaderParameter(const std::string& name) const;

private:
	void createRootSignatures(ID3D12Device* device, const RaytracingPipelineStateObjectDesc& desc);

	D3DShaderParameterTable globalParameterTable; // Copied from D3DShaderStage
	std::map<std::string, const D3DShaderParameter*> globalParameterHashMap; // For fast query

	D3DShaderParameterTable localParameterTableRaygen;
	D3DShaderParameterTable localParameterTableClosestHit;
	D3DShaderParameterTable localParameterTableMiss;
	D3DShaderParameterTable localParameterTableAnyHit;
	D3DShaderParameterTable localParameterTableIntersection;

	WRL::ComPtr<ID3D12RootSignature> globalRootSignature;
	WRL::ComPtr<ID3D12RootSignature> localRootSignatureRaygen;
	WRL::ComPtr<ID3D12RootSignature> localRootSignatureClosestHit;
	WRL::ComPtr<ID3D12RootSignature> localRootSignatureMiss;
	WRL::ComPtr<ID3D12RootSignature> localRootSignatureAnyHit;
	WRL::ComPtr<ID3D12RootSignature> localRootSignatureIntersection;

	WRL::ComPtr<ID3D12StateObject> rawRTPSO;
	WRL::ComPtr<ID3D12StateObjectProperties> rawProperties;
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

	inline D3D12_GPU_VIRTUAL_ADDRESS getGpuVirtualAddress() const
	{
		return rawUploadBuffer->GetGPUVirtualAddress();
	}
	inline uint32 getSizeInBytes() const { return rawUploadBufferSize; }
	inline uint32 getStrideInBytes() const { return shaderRecordSize; }

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

class D3DCommandSignature : public CommandSignature
{
public:
	void initialize(
		ID3D12Device* d3dDevice,
		const D3D12_COMMAND_SIGNATURE_DESC& desc,
		ID3D12RootSignature* rootSignature)
	{
		HR(d3dDevice->CreateCommandSignature(&desc, rootSignature, IID_PPV_ARGS(&rawCommandSignature)));
	}

	inline ID3D12CommandSignature* getRaw() const { return rawCommandSignature.Get(); }

private:
	WRL::ComPtr<ID3D12CommandSignature> rawCommandSignature;
};

class D3DIndirectCommandGenerator : public IndirectCommandGenerator
{
public:
	~D3DIndirectCommandGenerator();

	virtual void initialize(const CommandSignatureDesc& desc, uint32 maxCommandCount) override;

	virtual void resizeMaxCommandCount(uint32 newMaxCount) override;

	//~ BEGIN stateful API
	virtual void beginCommand(uint32 commandIx) override;

	virtual void writeConstant32(uint32 constant) override;
	virtual void writeVertexBufferView(VertexBuffer* vbuffer) override;
	virtual void writeIndexBufferView(IndexBuffer* ibuffer) override;
	virtual void writeDrawArguments(
		uint32 vertexCountPerInstance,
		uint32 instanceCount,
		uint32 startVertexLocation,
		uint32 startInstanceLocation) override;
	virtual void writeDrawIndexedArguments(
		uint32 indexCountPerInstance,
		uint32 instanceCount,
		uint32 startIndexLocation,
		int32  baseVertexLocation,
		uint32 startInstanceLocation) override;
	virtual void writeDispatchArguments(
		uint32 threadGroupCountX,
		uint32 threadGroupCountY,
		uint32 threadGroupCountZ) override;
	virtual void writeConstantBufferView(ConstantBufferView* view) override;
	virtual void writeShaderResourceView(ShaderResourceView* view) override;
	virtual void writeUnorderedAccessView(UnorderedAccessView* view) override;
	virtual void writeDispatchMeshArguments(
		uint32 threadGroupCountX,
		uint32 threadGroupCountY,
		uint32 threadGroupCountZ) override;

	virtual void endCommand() override;
	//~ END stateful API

	virtual uint32 getMaxCommandCount() const override { return maxCommandCount; }
	virtual uint32 getCommandByteStride() const override { return byteStride; }
	virtual void copyToBuffer(RenderCommandList* commandList, uint32 numCommands, Buffer* destBuffer, uint64 destOffset) override;

private:
	uint32 maxCommandCount = 0;
	uint32 byteStride = 0;
	uint32 paddingBytes = 0;

	uint8* memblock = nullptr;
	uint8* currentWritePtr = nullptr;
};
