#pragma once

#include "render/pipeline_state.h"
#include "d3d_util.h"

class D3DPipelineState : public PipelineState
{
	
public:
	D3DPipelineState(ID3D12PipelineState* state)
	{
		setRaw(state);
	}

	inline void setRaw(ID3D12PipelineState* state)
	{
		rawState = state;
	}

	inline ID3D12PipelineState* getRaw() const
	{
		return rawState.Get();
	}

private:
	WRL::ComPtr<ID3D12PipelineState> rawState;

};

class D3DRootSignature : public RootSignature
{

public:
	D3DRootSignature(ID3D12RootSignature* signature)
	{
		setRaw(signature);
	}

	inline void setRaw(ID3D12RootSignature* signature)
	{
		rawRootSignature = signature;
	}

	inline ID3D12RootSignature* getRaw() const
	{
		return rawRootSignature.Get();
	}

private:
	WRL::ComPtr<ID3D12RootSignature> rawRootSignature;
	
};
