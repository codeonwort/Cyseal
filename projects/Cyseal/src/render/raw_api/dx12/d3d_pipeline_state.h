#pragma once

#include "core/assertion.h"
#include "render/pipeline_state.h"
#include "render/gpu_resource_binding.h"
#include "d3d_util.h"
#include <vector>

class D3DRootSignature;
class D3DShaderStage;

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
	}
	ID3D12StateObject* getRaw() const { return rawRTPSO.Get(); }
private:
	WRL::ComPtr<ID3D12StateObject> rawRTPSO;
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
