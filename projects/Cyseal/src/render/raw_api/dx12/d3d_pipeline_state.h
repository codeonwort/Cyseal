#pragma once

#include "core/assertion.h"
#include "render/pipeline_state.h"
#include "render/resource_binding.h"
#include "d3d_util.h"
#include <vector>

class D3DRootSignature;
class D3DShaderStage;

// Convert API-agnostic structs into D3D12 structs
namespace into_d3d
{
	class TempAlloc
	{
	public:
		~TempAlloc()
		{
			for (auto ptrArr : descriptorRanges) delete[] ptrArr;
			for (auto ptrArr : rootParameters) delete[] ptrArr;
			for (auto ptrArr : staticSamplers) delete[] ptrArr;
			for (auto ptrArr : inputElements) delete[] ptrArr;
		}

		D3D12_DESCRIPTOR_RANGE* allocDescriptorRanges(uint32 num)
		{
			D3D12_DESCRIPTOR_RANGE* ranges = new D3D12_DESCRIPTOR_RANGE[num];
			descriptorRanges.push_back(ranges);
			return ranges;
		}
		D3D12_ROOT_PARAMETER* allocRootParameters(uint32 num)
		{
			D3D12_ROOT_PARAMETER* params = new D3D12_ROOT_PARAMETER[num];
			rootParameters.push_back(params);
			return params;
		}
		D3D12_STATIC_SAMPLER_DESC* allocStaticSamplers(uint32 num)
		{
			D3D12_STATIC_SAMPLER_DESC* samplers = new D3D12_STATIC_SAMPLER_DESC[num];
			staticSamplers.push_back(samplers);
			return samplers;
		}
		D3D12_INPUT_ELEMENT_DESC* allocInputElements(uint32 num)
		{
			D3D12_INPUT_ELEMENT_DESC* elems = new D3D12_INPUT_ELEMENT_DESC[num];
			inputElements.push_back(elems);
			return elems;
		}
	private:
		std::vector<D3D12_DESCRIPTOR_RANGE*> descriptorRanges;
		std::vector<D3D12_ROOT_PARAMETER*> rootParameters;
		std::vector<D3D12_STATIC_SAMPLER_DESC*> staticSamplers;
		std::vector<D3D12_INPUT_ELEMENT_DESC*> inputElements;
	};

	inline D3D12_BLEND blend(EBlend inBlend)
	{
		return static_cast<D3D12_BLEND>(inBlend);
	}
	inline D3D12_BLEND_OP blendOp(EBlendOp inBlendOp)
	{
		return static_cast<D3D12_BLEND_OP>(inBlendOp);
	}
	inline D3D12_LOGIC_OP logicOp(ELogicOp inLogicOp)
	{
		return static_cast<D3D12_LOGIC_OP>(inLogicOp);
	}
	inline UINT8 colorWriteEnable(EColorWriteEnable inMask)
	{
		// D3D12_COLOR_WRITE_ENABLE
		return static_cast<UINT8>(inMask);
	}
	inline D3D12_COMPARISON_FUNC comparisonFunc(EComparisonFunc func)
	{
		return static_cast<D3D12_COMPARISON_FUNC>(func);
	}

	inline void renderTargetBlendDesc(const RenderTargetBlendDesc& inDesc, D3D12_RENDER_TARGET_BLEND_DESC& outDesc)
	{
		outDesc.BlendEnable           = inDesc.blendEnable;
		outDesc.LogicOpEnable         = inDesc.logicOpEnable;
		outDesc.SrcBlend              = blend(inDesc.srcBlend);
		outDesc.DestBlend             = blend(inDesc.destBlend);
		outDesc.BlendOp               = blendOp(inDesc.blendOp);
		outDesc.SrcBlendAlpha         = blend(inDesc.srcBlendAlpha);
		outDesc.DestBlendAlpha        = blend(inDesc.destBlendAlpha);
		outDesc.LogicOp               = logicOp(inDesc.logicOp);
		outDesc.RenderTargetWriteMask = colorWriteEnable(inDesc.renderTargetWriteMask);
	}

	inline void blendDesc(const BlendDesc& inDesc, D3D12_BLEND_DESC& outDesc)
	{
		::memset(&outDesc, 0, sizeof(outDesc));

		outDesc.AlphaToCoverageEnable = inDesc.alphaToCoverageEnable;
		outDesc.IndependentBlendEnable = inDesc.independentBlendEnable;
		for (uint32 i = 0; i < 8; ++i)
		{
			renderTargetBlendDesc(inDesc.renderTarget[i], outDesc.RenderTarget[i]);
		}
	}

	inline D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags(ERootSignatureFlags inFlags)
	{
		return static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(inFlags);
	}

	inline D3D12_ROOT_PARAMETER_TYPE rootParameterType(ERootParameterType inType)
	{
		return static_cast<D3D12_ROOT_PARAMETER_TYPE>(inType);
	}

	inline D3D12_SHADER_VISIBILITY shaderVisibility(EShaderVisibility inSV)
	{
		return static_cast<D3D12_SHADER_VISIBILITY>(inSV);
	}

	inline D3D12_DESCRIPTOR_RANGE_TYPE descriptorRangeType(EDescriptorRangeType inType)
	{
		return static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(inType);
	}

	inline D3D12_FILTER filter(ETextureFilter inFilter)
	{
		return static_cast<D3D12_FILTER>(inFilter);
	}
	inline D3D12_STATIC_BORDER_COLOR staticBorderColor(EStaticBorderColor color)
	{
		return static_cast<D3D12_STATIC_BORDER_COLOR>(color);
	}

	inline D3D12_TEXTURE_ADDRESS_MODE textureAddressMode(ETextureAddressMode mode)
	{
		return static_cast<D3D12_TEXTURE_ADDRESS_MODE>(mode);
	}

	inline void staticSamplerDesc(const StaticSamplerDesc& inDesc, D3D12_STATIC_SAMPLER_DESC& outDesc)
	{
		outDesc.Filter           = filter(inDesc.filter);
		outDesc.AddressU         = textureAddressMode(inDesc.addressU);
		outDesc.AddressV         = textureAddressMode(inDesc.addressV);
		outDesc.AddressW         = textureAddressMode(inDesc.addressW);
		outDesc.MipLODBias       = inDesc.mipLODBias;
		outDesc.MaxAnisotropy    = inDesc.maxAnisotropy;
		outDesc.ComparisonFunc   = comparisonFunc(inDesc.comparisonFunc);
		outDesc.BorderColor      = staticBorderColor(inDesc.borderColor);
		outDesc.MinLOD           = inDesc.minLOD;
		outDesc.MaxLOD           = inDesc.maxLOD;
		outDesc.ShaderRegister   = inDesc.shaderRegister;
		outDesc.RegisterSpace    = inDesc.registerSpace;
		outDesc.ShaderVisibility = shaderVisibility(inDesc.shaderVisibility);
	}

	inline void descriptorRange(const DescriptorRange& inRange, D3D12_DESCRIPTOR_RANGE& outRange)
	{
		outRange.RangeType                         = descriptorRangeType(inRange.rangeType);
		outRange.NumDescriptors                    = inRange.numDescriptors;
		outRange.BaseShaderRegister                = inRange.baseShaderRegister;
		outRange.RegisterSpace                     = inRange.registerSpace;
		outRange.OffsetInDescriptorsFromTableStart = inRange.offsetInDescriptorsFromTableStart;
	}

	inline void rootConstants(const RootConstants& inConsts, D3D12_ROOT_CONSTANTS& outConsts)
	{
		outConsts.ShaderRegister = inConsts.shaderRegister;
		outConsts.RegisterSpace = inConsts.registerSpace;
		outConsts.Num32BitValues = inConsts.num32BitValues;
	}

	inline void rootDescriptor(const RootDescriptor& inDesc, D3D12_ROOT_DESCRIPTOR& outDesc)
	{
		outDesc.ShaderRegister = inDesc.shaderRegister;
		outDesc.RegisterSpace = inDesc.registerSpace;
	}

	inline void rootParameter(const RootParameter& inParam, D3D12_ROOT_PARAMETER& outParam, TempAlloc& tempAlloc)
	{
		outParam.ParameterType = rootParameterType(inParam.parameterType);
		switch (inParam.parameterType)
		{
		case ERootParameterType::DescriptorTable:
			{
				const uint32 num = inParam.descriptorTable.numDescriptorRanges;
				D3D12_DESCRIPTOR_RANGE* tempDescriptorRanges = tempAlloc.allocDescriptorRanges(num);
				for (uint32 i = 0; i < num; ++i)
				{
					descriptorRange(inParam.descriptorTable.descriptorRanges[i], tempDescriptorRanges[i]);
				}
				outParam.DescriptorTable.NumDescriptorRanges = num;
				outParam.DescriptorTable.pDescriptorRanges = tempDescriptorRanges;
			}
			break;

		case ERootParameterType::Constants32Bit:
			rootConstants(inParam.constants, outParam.Constants);
			break;

		case ERootParameterType::CBV:
		case ERootParameterType::SRV:
		case ERootParameterType::UAV:
			rootDescriptor(inParam.descriptor, outParam.Descriptor);
			break;

		default:
			CHECK_NO_ENTRY();
			break;
		}
		outParam.ShaderVisibility = shaderVisibility(inParam.shaderVisibility);
	}

	inline void rootSignatureDesc(
		const RootSignatureDesc& inDesc,
		D3D12_ROOT_SIGNATURE_DESC& outDesc,
		TempAlloc& tempAlloc)
	{
		D3D12_ROOT_PARAMETER* tempParameters = tempAlloc.allocRootParameters(inDesc.numParameters);
		D3D12_STATIC_SAMPLER_DESC* tempStaticSamplers = tempAlloc.allocStaticSamplers(inDesc.numStaticSamplers);
		for (uint32 i = 0; i < inDesc.numParameters; ++i)
		{
			rootParameter(inDesc.parameters[i], tempParameters[i], tempAlloc);
		}
		for (uint32 i = 0; i < inDesc.numStaticSamplers; ++i)
		{
			staticSamplerDesc(inDesc.staticSamplers[i], tempStaticSamplers[i]);
		}

		outDesc.NumParameters     = inDesc.numParameters;
		outDesc.pParameters       = tempParameters;
		outDesc.NumStaticSamplers = inDesc.numStaticSamplers;
		outDesc.pStaticSamplers   = tempStaticSamplers;
		outDesc.Flags             = rootSignatureFlags(inDesc.flags);
	}

	inline D3D12_FILL_MODE fillMode(EFillMode inMode)
	{
		return static_cast<D3D12_FILL_MODE>(inMode);
	}
	inline D3D12_CULL_MODE cullMode(ECullMode inMode)
	{
		return static_cast<D3D12_CULL_MODE>(inMode);
	}
	inline D3D12_CONSERVATIVE_RASTERIZATION_MODE conservativeRasterMode(EConservativeRasterizationMode inMode)
	{
		return static_cast<D3D12_CONSERVATIVE_RASTERIZATION_MODE>(inMode);
	}

	inline void rasterizerDesc(const RasterizerDesc& inDesc, D3D12_RASTERIZER_DESC& outDesc)
	{
		outDesc.FillMode              = fillMode(inDesc.fillMode);
		outDesc.CullMode              = cullMode(inDesc.cullMode);
		outDesc.FrontCounterClockwise = inDesc.frontCCW;
		outDesc.DepthBias             = inDesc.depthBias;
		outDesc.DepthBiasClamp        = inDesc.depthBiasClamp;
		outDesc.SlopeScaledDepthBias  = inDesc.slopeScaledDepthBias;
		outDesc.DepthClipEnable       = inDesc.depthClipEnable;
		outDesc.MultisampleEnable     = inDesc.multisampleEnable;
		outDesc.AntialiasedLineEnable = inDesc.antialisedLineEnable;
		outDesc.ForcedSampleCount     = inDesc.forcedSampleCount;
		outDesc.ConservativeRaster    = conservativeRasterMode(inDesc.conservativeRaster);
	}

	inline D3D12_DEPTH_WRITE_MASK depthWriteMask(EDepthWriteMask inMask)
	{
		return static_cast<D3D12_DEPTH_WRITE_MASK>(inMask);
	}

	inline D3D12_STENCIL_OP stencilOp(EStencilOp inOP)
	{
		return static_cast<D3D12_STENCIL_OP>(inOP);
	}

	inline void depthstencilOpDesc(const DepthstencilOpDesc& inDesc, D3D12_DEPTH_STENCILOP_DESC& outDesc)
	{
		outDesc.StencilFailOp      = stencilOp(inDesc.stencilFailOp);
		outDesc.StencilDepthFailOp = stencilOp(inDesc.stencilDepthFailOp);
		outDesc.StencilPassOp      = stencilOp(inDesc.stencilPassOp);
		outDesc.StencilFunc        = comparisonFunc(inDesc.stencilFunc);
	}

	inline void depthstencilDesc(const DepthstencilDesc& inDesc, D3D12_DEPTH_STENCIL_DESC& outDesc)
	{
		outDesc.DepthEnable      = inDesc.depthEnable;
		outDesc.DepthWriteMask   = depthWriteMask(inDesc.depthWriteMask);
		outDesc.DepthFunc        = comparisonFunc(inDesc.depthFunc);
		outDesc.StencilEnable    = inDesc.stencilEnable;
		outDesc.StencilReadMask  = inDesc.stencilReadMask;
		outDesc.StencilWriteMask = inDesc.stencilWriteMask;
		depthstencilOpDesc(inDesc.frontFace, outDesc.FrontFace);
		depthstencilOpDesc(inDesc.backFace, outDesc.BackFace);
	}

	inline D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType(EPrimitiveTopologyType inType)
	{
		return static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(inType);
	}

	inline DXGI_FORMAT pixelFormat(EPixelFormat inFormat)
	{
		switch (inFormat)
		{
		case EPixelFormat::UNKNOWN:            return DXGI_FORMAT_UNKNOWN;
		case EPixelFormat::R32_TYPELESS:       return DXGI_FORMAT_R32_TYPELESS;
		case EPixelFormat::R8G8B8A8_UNORM:     return DXGI_FORMAT_R8G8B8A8_UNORM;
		case EPixelFormat::R32G32B32_FLOAT:    return DXGI_FORMAT_R32G32B32_FLOAT;
		case EPixelFormat::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case EPixelFormat::R32_UINT:           return DXGI_FORMAT_R32_UINT;
		case EPixelFormat::R16_UINT:           return DXGI_FORMAT_R16_UINT;
		case EPixelFormat::D24_UNORM_S8_UINT:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
		default:
			// #todo: Unknown pixel format
			CHECK_NO_ENTRY();
		}

		return DXGI_FORMAT_UNKNOWN;
	}

	inline void sampleDesc(const SampleDesc& inDesc, DXGI_SAMPLE_DESC& outDesc)
	{
		outDesc.Count   = inDesc.count;
		outDesc.Quality = inDesc.quality;
	}

	inline D3D12_INPUT_CLASSIFICATION inputClassification(EVertexInputClassification inValue)
	{
		return static_cast<D3D12_INPUT_CLASSIFICATION>(inValue);
	}

	void graphicsPipelineDesc(const GraphicsPipelineDesc& inDesc, D3D12_GRAPHICS_PIPELINE_STATE_DESC& outDesc, TempAlloc& tempAlloc);

	inline void inputElement(const VertexInputElement& inDesc, D3D12_INPUT_ELEMENT_DESC& outDesc)
	{
		outDesc.SemanticName         = inDesc.semantic;
		outDesc.SemanticIndex        = inDesc.semanticIndex;
		outDesc.Format               = pixelFormat(inDesc.format);
		outDesc.InputSlot            = inDesc.inputSlot;
		outDesc.AlignedByteOffset    = inDesc.alignedByteOffset;
		outDesc.InputSlotClass       = inputClassification(inDesc.inputSlotClass);
		outDesc.InstanceDataStepRate = inDesc.instanceDataStepRate;
	}

	inline void inputLayout(const VertexInputLayout& inDesc, D3D12_INPUT_LAYOUT_DESC& outDesc, TempAlloc& tempAlloc)
	{
		uint32 num = static_cast<uint32>(inDesc.elements.size());
		D3D12_INPUT_ELEMENT_DESC* tempElements = tempAlloc.allocInputElements(num);
		for (uint32 i = 0; i < num; ++i)
		{
			inputElement(inDesc.elements[i], tempElements[i]);
		}

		outDesc.NumElements        = static_cast<UINT>(num);
		outDesc.pInputElementDescs = tempElements;
	}

	inline D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType(EDescriptorHeapType inType)
	{
		switch (inType)
		{
			case EDescriptorHeapType::CBV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::SRV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::UAV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::CBV_SRV_UAV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::SAMPLER:  return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			case EDescriptorHeapType::RTV:  return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			case EDescriptorHeapType::DSV:  return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			case EDescriptorHeapType::NUM_TYPES: CHECK_NO_ENTRY();
		}
		return D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	}

	inline D3D12_DESCRIPTOR_HEAP_FLAGS descriptorHeapFlags(EDescriptorHeapFlags inFlags)
	{
		return static_cast<D3D12_DESCRIPTOR_HEAP_FLAGS>(inFlags);
	}

	inline void descriptorHeapDesc(const DescriptorHeapDesc& inDesc, D3D12_DESCRIPTOR_HEAP_DESC& outDesc)
	{
		outDesc.Type           = descriptorHeapType(inDesc.type);
		outDesc.NumDescriptors = inDesc.numDescriptors;
		outDesc.Flags          = descriptorHeapFlags(inDesc.flags);
		outDesc.NodeMask       = inDesc.nodeMask;
	}
}

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
