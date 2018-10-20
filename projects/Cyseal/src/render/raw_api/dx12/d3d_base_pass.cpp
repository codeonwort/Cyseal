#include "d3d_base_pass.h"
#include "d3d_shader.h"
#include "d3d_device.h"
#include "d3d_pipeline_state.h"

void D3DBasePass::initialize()
{
	createPSO();
}

void D3DBasePass::createPSO()
{
	auto device = getD3DDevice();
	auto swapChain = static_cast<D3DSwapChain*>(device->getSwapChain());

	createRootSignature();
	createInputLayout();

	D3DShader shader;
	shader.loadVertexShader(L"base_pass.hlsl", "mainVS");
	shader.loadPixelShader(L"base_pass.hlsl", "mainPS");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.InputLayout           = { inputLayout.data(), (UINT)inputLayout.size() };
	desc.pRootSignature        = rawRootSignature.Get();
	desc.VS                    = shader.getBytecode(EShaderType::VERTEX_SHADER);
	desc.PS                    = shader.getBytecode(EShaderType::PIXEL_SHADER);
	desc.RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.SampleMask            = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets      = 1; 
	desc.RTVFormats[0]         = swapChain->getBackBufferFormat();
	desc.SampleDesc.Count      = swapChain->supports4xMSAA() ? 4 : 1;
	desc.SampleDesc.Quality    = swapChain->supports4xMSAA() ? (swapChain->get4xMSAAQuality() - 1) : 0;
	desc.DSVFormat             = device->getBackBufferDSVFormat();

	HR( device->getRawDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&rawPipelineState)) );
	pipelineState = std::make_unique<D3DPipelineState>(rawPipelineState.Get());
}

void D3DBasePass::createRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameters[1];

	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	slotRootParameters[0].InitAsDescriptorTable(1, &cbvTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameters, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	WRL::ComPtr<ID3DBlob> serializedRootSig;
	WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());

	HR(hr);

	auto device = static_cast<D3DDevice*>(gRenderDevice)->getRawDevice();
	HR( device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(&rawRootSignature)) );

	rootSignature = std::make_unique<D3DRootSignature>(rawRootSignature.Get());
}

void D3DBasePass::createInputLayout()
{
 	// { SemanticName, SemanticIndex, Format, InputSlot, AlignedByteOffset, InputSlotClass, InstanceDataStepRate }
	inputLayout.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } );
}
