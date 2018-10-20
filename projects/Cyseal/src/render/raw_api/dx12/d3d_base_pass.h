#pragma once

#include "render/base_pass.h"
#include "d3d_util.h"
#include <vector>

class D3DBasePass : public BasePass
{

public:
	void initialize();

private:
	void createPSO();
	void createRootSignature();
	void createInputLayout();

	WRL::ComPtr<ID3D12PipelineState> rawPipelineState;
	WRL::ComPtr<ID3D12RootSignature> rawRootSignature;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

};
