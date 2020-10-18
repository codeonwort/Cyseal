#include "d3d_pipeline_state.h"
#include "d3d_shader.h"

namespace into_d3d
{
	void graphicsPipelineDesc(const GraphicsPipelineDesc& inDesc, D3D12_GRAPHICS_PIPELINE_STATE_DESC& outDesc, TempAlloc& tempAlloc)
	{
		::memset(&outDesc, 0, sizeof(outDesc));

		outDesc.pRootSignature = static_cast<D3DRootSignature*>(inDesc.rootSignature)->getRaw();
		if (inDesc.vs != nullptr) outDesc.VS = static_cast<D3DShaderStage*>(inDesc.vs)->getBytecode();
		if (inDesc.ps != nullptr) outDesc.PS = static_cast<D3DShaderStage*>(inDesc.ps)->getBytecode();
		if (inDesc.ds != nullptr) outDesc.DS = static_cast<D3DShaderStage*>(inDesc.ds)->getBytecode();
		if (inDesc.hs != nullptr) outDesc.HS = static_cast<D3DShaderStage*>(inDesc.hs)->getBytecode();
		if (inDesc.gs != nullptr) outDesc.GS = static_cast<D3DShaderStage*>(inDesc.gs)->getBytecode();
		blendDesc(inDesc.blendDesc, outDesc.BlendState);
		outDesc.SampleMask = inDesc.sampleMask;
		rasterizerDesc(inDesc.rasterizerDesc, outDesc.RasterizerState);
		depthstencilDesc(inDesc.depthstencilDesc, outDesc.DepthStencilState);
		inputLayout(inDesc.inputLayout, outDesc.InputLayout, tempAlloc);
		outDesc.PrimitiveTopologyType = primitiveTopologyType(inDesc.primitiveTopologyType);
		outDesc.NumRenderTargets = inDesc.numRenderTargets;
		for (uint32 i = 0; i < 8; ++i)
		{
			outDesc.RTVFormats[i] = pixelFormat(inDesc.rtvFormats[i]);
		}
		outDesc.DSVFormat = pixelFormat(inDesc.dsvFormat);
		sampleDesc(inDesc.sampleDesc, outDesc.SampleDesc);
	}
}
