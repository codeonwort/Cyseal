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
	D3DGraphicsPipelineState() {}

	void initialize(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
	{
		HR( device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&rawState)) );
	}

	ID3D12PipelineState* getRaw() const { return rawState.Get(); }

private:
	WRL::ComPtr<ID3D12PipelineState> rawState;
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
