#pragma once

#include "core/assertion.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_barrier.h"
#include "rhi/buffer.h"
#include "rhi/texture.h"
#include "rhi/hardware_raytracing.h"
#include "d3d_util.h"
#include <vector>

class D3DGraphicsPipelineState;

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
			for (auto ptrArr : indirectArgumentDescs) delete[] ptrArr;
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
		D3D12_INDIRECT_ARGUMENT_DESC* allocIndirectArgumentDescs(uint32 num)
		{
			D3D12_INDIRECT_ARGUMENT_DESC* descs = new D3D12_INDIRECT_ARGUMENT_DESC[num];
			indirectArgumentDescs.push_back(descs);
			return descs;
		}
	private:
		std::vector<D3D12_DESCRIPTOR_RANGE*> descriptorRanges;
		std::vector<D3D12_ROOT_PARAMETER*> rootParameters;
		std::vector<D3D12_STATIC_SAMPLER_DESC*> staticSamplers;
		std::vector<D3D12_INPUT_ELEMENT_DESC*> inputElements;
		std::vector<D3D12_INDIRECT_ARGUMENT_DESC*> indirectArgumentDescs;
	};

	inline ID3D12Resource* id3d12Resource(GPUResource* inResource)
	{
		return reinterpret_cast<ID3D12Resource*>(inResource->getRawResource());
	}

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
		outDesc.BlendEnable = inDesc.blendEnable;
		outDesc.LogicOpEnable = inDesc.logicOpEnable;
		outDesc.SrcBlend = blend(inDesc.srcBlend);
		outDesc.DestBlend = blend(inDesc.destBlend);
		outDesc.BlendOp = blendOp(inDesc.blendOp);
		outDesc.SrcBlendAlpha = blend(inDesc.srcBlendAlpha);
		outDesc.DestBlendAlpha = blend(inDesc.destBlendAlpha);
		outDesc.LogicOp = logicOp(inDesc.logicOp);
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

	inline D3D12_SHADER_VISIBILITY shaderVisibility(EShaderVisibility inSV)
	{
		return static_cast<D3D12_SHADER_VISIBILITY>(inSV);
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
		outDesc.Filter = filter(inDesc.filter);
		outDesc.AddressU = textureAddressMode(inDesc.addressU);
		outDesc.AddressV = textureAddressMode(inDesc.addressV);
		outDesc.AddressW = textureAddressMode(inDesc.addressW);
		outDesc.MipLODBias = inDesc.mipLODBias;
		outDesc.MaxAnisotropy = inDesc.maxAnisotropy;
		outDesc.ComparisonFunc = comparisonFunc(inDesc.comparisonFunc);
		outDesc.BorderColor = staticBorderColor(inDesc.borderColor);
		outDesc.MinLOD = inDesc.minLOD;
		outDesc.MaxLOD = inDesc.maxLOD;
		outDesc.ShaderRegister = inDesc.shaderRegister;
		outDesc.RegisterSpace = inDesc.registerSpace;
		outDesc.ShaderVisibility = shaderVisibility(inDesc.shaderVisibility);
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
		outDesc.FillMode = fillMode(inDesc.fillMode);
		outDesc.CullMode = cullMode(inDesc.cullMode);
		outDesc.FrontCounterClockwise = inDesc.frontCCW;
		outDesc.DepthBias = inDesc.depthBias;
		outDesc.DepthBiasClamp = inDesc.depthBiasClamp;
		outDesc.SlopeScaledDepthBias = inDesc.slopeScaledDepthBias;
		outDesc.DepthClipEnable = inDesc.depthClipEnable;
		outDesc.MultisampleEnable = inDesc.multisampleEnable;
		outDesc.AntialiasedLineEnable = inDesc.antialisedLineEnable;
		outDesc.ForcedSampleCount = inDesc.forcedSampleCount;
		outDesc.ConservativeRaster = conservativeRasterMode(inDesc.conservativeRaster);
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
		outDesc.StencilFailOp = stencilOp(inDesc.stencilFailOp);
		outDesc.StencilDepthFailOp = stencilOp(inDesc.stencilDepthFailOp);
		outDesc.StencilPassOp = stencilOp(inDesc.stencilPassOp);
		outDesc.StencilFunc = comparisonFunc(inDesc.stencilFunc);
	}

	inline void depthstencilDesc(const DepthstencilDesc& inDesc, D3D12_DEPTH_STENCIL_DESC& outDesc)
	{
		outDesc.DepthEnable = inDesc.depthEnable;
		outDesc.DepthWriteMask = depthWriteMask(inDesc.depthWriteMask);
		outDesc.DepthFunc = comparisonFunc(inDesc.depthFunc);
		outDesc.StencilEnable = inDesc.stencilEnable;
		outDesc.StencilReadMask = inDesc.stencilReadMask;
		outDesc.StencilWriteMask = inDesc.stencilWriteMask;
		depthstencilOpDesc(inDesc.frontFace, outDesc.FrontFace);
		depthstencilOpDesc(inDesc.backFace, outDesc.BackFace);
	}

	inline D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType(EPrimitiveTopologyType inType)
	{
		return static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(inType);
	}

	inline D3D12_PRIMITIVE_TOPOLOGY primitiveTopology(EPrimitiveTopology topology)
	{
		switch (topology)
		{
			case EPrimitiveTopology::UNDEFINED         : return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			case EPrimitiveTopology::POINTLIST         : return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			case EPrimitiveTopology::LINELIST          : return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			case EPrimitiveTopology::LINESTRIP         : return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			case EPrimitiveTopology::TRIANGLELIST      : return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case EPrimitiveTopology::TRIANGLESTRIP     : return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			case EPrimitiveTopology::LINELIST_ADJ      : return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
			case EPrimitiveTopology::LINESTRIP_ADJ     : return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
			case EPrimitiveTopology::TRIANGLELIST_ADJ  : return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
			case EPrimitiveTopology::TRIANGLESTRIP_ADJ : return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
		}
		CHECK_NO_ENTRY();
		return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	}

	inline DXGI_FORMAT pixelFormat(EPixelFormat inFormat)
	{
		switch (inFormat)
		{
			case EPixelFormat::UNKNOWN            : return DXGI_FORMAT_UNKNOWN;
			case EPixelFormat::R32_TYPELESS       : return DXGI_FORMAT_R32_TYPELESS;
			case EPixelFormat::R8G8B8A8_UNORM     : return DXGI_FORMAT_R8G8B8A8_UNORM;
			case EPixelFormat::B8G8R8A8_UNORM     : return DXGI_FORMAT_B8G8R8A8_UNORM;
			case EPixelFormat::R32G32_FLOAT       : return DXGI_FORMAT_R32G32_FLOAT;
			case EPixelFormat::R32G32B32_FLOAT    : return DXGI_FORMAT_R32G32B32_FLOAT;
			case EPixelFormat::R32G32B32A32_FLOAT : return DXGI_FORMAT_R32G32B32A32_FLOAT;
			case EPixelFormat::R16G16B16A16_FLOAT : return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case EPixelFormat::R32_UINT           : return DXGI_FORMAT_R32_UINT;
			case EPixelFormat::R16_UINT           : return DXGI_FORMAT_R16_UINT;
			case EPixelFormat::D24_UNORM_S8_UINT  : return DXGI_FORMAT_D24_UNORM_S8_UINT;
		}
		CHECK_NO_ENTRY(); // #todo-dx12: Unknown pixel format
		return DXGI_FORMAT_UNKNOWN;
	}

	inline void sampleDesc(const SampleDesc& inDesc, DXGI_SAMPLE_DESC& outDesc)
	{
		outDesc.Count = inDesc.count;
		outDesc.Quality = inDesc.quality;
	}

	inline D3D12_INPUT_CLASSIFICATION inputClassification(EVertexInputClassification inValue)
	{
		return static_cast<D3D12_INPUT_CLASSIFICATION>(inValue);
	}

	// NOTE: You must assign pRootSignature yourself.
	void graphicsPipelineDesc(
		const GraphicsPipelineDesc& inDesc,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC& outDesc,
		TempAlloc& tempAlloc);

	inline void inputElement(const VertexInputElement& inDesc, D3D12_INPUT_ELEMENT_DESC& outDesc)
	{
		outDesc.SemanticName = inDesc.semantic;
		outDesc.SemanticIndex = inDesc.semanticIndex;
		outDesc.Format = pixelFormat(inDesc.format);
		outDesc.InputSlot = inDesc.inputSlot;
		outDesc.AlignedByteOffset = inDesc.alignedByteOffset;
		outDesc.InputSlotClass = inputClassification(inDesc.inputSlotClass);
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

		outDesc.NumElements = static_cast<UINT>(num);
		outDesc.pInputElementDescs = tempElements;
	}

	inline D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType(EDescriptorHeapType inType)
	{
		switch (inType)
		{
			case EDescriptorHeapType::CBV         : return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::SRV         : return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::UAV         : return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::CBV_SRV_UAV : return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			case EDescriptorHeapType::SAMPLER     : return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			case EDescriptorHeapType::RTV         : return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			case EDescriptorHeapType::DSV         : return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		}
		CHECK_NO_ENTRY();
		return D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	}

	inline D3D12_DESCRIPTOR_HEAP_FLAGS descriptorHeapFlags(EDescriptorHeapFlags inFlags)
	{
		return static_cast<D3D12_DESCRIPTOR_HEAP_FLAGS>(inFlags);
	}

	inline void descriptorHeapDesc(const DescriptorHeapDesc& inDesc, D3D12_DESCRIPTOR_HEAP_DESC& outDesc)
	{
		outDesc.Type = descriptorHeapType(inDesc.type);
		outDesc.NumDescriptors = inDesc.numDescriptors;
		outDesc.Flags = descriptorHeapFlags(inDesc.flags);
		outDesc.NodeMask = inDesc.nodeMask;
	}

	inline D3D12_RESOURCE_DIMENSION textureDimension(ETextureDimension dimension)
	{
		switch (dimension)
		{
			case ETextureDimension::UNKNOWN   : return D3D12_RESOURCE_DIMENSION_UNKNOWN;
			case ETextureDimension::TEXTURE1D : return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
			case ETextureDimension::TEXTURE2D : return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			case ETextureDimension::TEXTURE3D : return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		}
		CHECK_NO_ENTRY();
		return D3D12_RESOURCE_DIMENSION_UNKNOWN;
	}

	inline D3D12_RESOURCE_DESC textureDesc(const TextureCreateParams& params)
	{
		D3D12_RESOURCE_DESC desc;
		ZeroMemory(&desc, sizeof(desc));

		if (params.dimension == ETextureDimension::TEXTURE1D
			|| params.dimension == ETextureDimension::TEXTURE2D)
		{
			CHECK(params.depth == 1);
		}
		else if (params.dimension == ETextureDimension::TEXTURE3D)
		{
			CHECK(params.numLayers == 1);
		}

		desc.Dimension = textureDimension(params.dimension);
		desc.Alignment = 0; // #todo-dx12: Always default alignment
		desc.Width = params.width;
		desc.Height = params.height;
		if (params.dimension == ETextureDimension::TEXTURE3D)
		{
			desc.DepthOrArraySize = params.depth;
		}
		else
		{
			desc.DepthOrArraySize = params.numLayers;
		}
		desc.MipLevels = params.mipLevels;
		desc.Format = into_d3d::pixelFormat(params.format);
		desc.SampleDesc.Count = params.sampleCount;
		desc.SampleDesc.Quality = params.sampleQuality;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // #todo-dx12: Always default layout
		
		// #todo-dx12: Other allow flags
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		if (0 != (params.accessFlags & ETextureAccessFlags::RTV))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::UAV))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::DSV))
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		}

		return desc;
	}

	D3D12_RESOURCE_STATES bufferMemoryLayout(EBufferMemoryLayout layout);
	D3D12_RESOURCE_STATES textureMemoryLayout(ETextureMemoryLayout layout);

	D3D12_RESOURCE_BARRIER resourceBarrier(const BufferMemoryBarrier& barrier);
	D3D12_RESOURCE_BARRIER resourceBarrier(const TextureMemoryBarrier& barrier);

	inline D3D12_RAYTRACING_GEOMETRY_TYPE raytracingGeometryType(ERaytracingGeometryType inType)
	{
		switch (inType)
		{
			case ERaytracingGeometryType::Triangles: return D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			case ERaytracingGeometryType::ProceduralPrimitiveAABB: return D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
		}
		CHECK_NO_ENTRY();
		return D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	}

	inline D3D12_RAYTRACING_GEOMETRY_FLAGS raytracingGeometryFlags(ERaytracingGeometryFlags inFlags)
	{
		D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		if (0 != (inFlags & ERaytracingGeometryFlags::Opaque))
		{
			flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		}
		if (0 != (inFlags & ERaytracingGeometryFlags::NoDuplicateAnyhitInvocation))
		{
			flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
		}
		return flags;
	}

	inline D3D12_HIT_GROUP_TYPE hitGroupType(ERaytracingHitGroupType inType)
	{
		switch (inType)
		{
			case ERaytracingHitGroupType::Triangles:           return D3D12_HIT_GROUP_TYPE_TRIANGLES;
			case ERaytracingHitGroupType::ProceduralPrimitive: return D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
		}
		CHECK_NO_ENTRY();
		return D3D12_HIT_GROUP_TYPE_TRIANGLES;
	}

	void raytracingGeometryDesc(
		const RaytracingGeometryDesc& inDesc,
		D3D12_RAYTRACING_GEOMETRY_DESC& outDesc);

	inline D3D12_SRV_DIMENSION srvDimension(ESRVDimension inDimension)
	{
		switch (inDimension)
		{
			case ESRVDimension::Unknown:                         return D3D12_SRV_DIMENSION_UNKNOWN;
			case ESRVDimension::Buffer:                          return D3D12_SRV_DIMENSION_BUFFER;
			case ESRVDimension::Texture1D:						 return D3D12_SRV_DIMENSION_TEXTURE1D;
			case ESRVDimension::Texture1DArray:					 return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			case ESRVDimension::Texture2D:						 return D3D12_SRV_DIMENSION_TEXTURE2D;
			case ESRVDimension::Texture2DArray:					 return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			case ESRVDimension::Texture2DMultiSampled:			 return D3D12_SRV_DIMENSION_TEXTURE2DMS;
			case ESRVDimension::Texture2DMultiSampledArray:		 return D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
			case ESRVDimension::Texture3D:						 return D3D12_SRV_DIMENSION_TEXTURE3D;
			case ESRVDimension::TextureCube:					 return D3D12_SRV_DIMENSION_TEXTURECUBE;
			case ESRVDimension::TextureCubeArray:				 return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			case ESRVDimension::RaytracingAccelerationStructure: return D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		}
		CHECK_NO_ENTRY();
		return D3D12_SRV_DIMENSION_UNKNOWN;
	}

	inline D3D12_BUFFER_SRV_FLAGS bufferSRVFlags(EBufferSRVFlags inFlags)
	{
		D3D12_BUFFER_SRV_FLAGS flags = D3D12_BUFFER_SRV_FLAG_NONE;
		if (0 != (inFlags & EBufferSRVFlags::Raw))
		{
			flags |= D3D12_BUFFER_SRV_FLAG_RAW;
		}
		return flags;
	}

	inline D3D12_BUFFER_SRV bufferSRVDesc(const BufferSRVDesc& inDesc)
	{
		D3D12_BUFFER_SRV desc{};
		desc.FirstElement        = inDesc.firstElement;
		desc.NumElements         = inDesc.numElements;
		desc.StructureByteStride = inDesc.structureByteStride;
		desc.Flags               = into_d3d::bufferSRVFlags(inDesc.flags);
		return desc;
	}

	inline D3D12_TEX2D_SRV texture2DSRVDesc(const Texture2DSRVDesc& inDesc)
	{
		D3D12_TEX2D_SRV desc{};
		desc.MostDetailedMip     = inDesc.mostDetailedMip;
		desc.MipLevels           = inDesc.mipLevels;
		desc.PlaneSlice          = inDesc.planeSlice;
		desc.ResourceMinLODClamp = inDesc.minLODClamp;
		return desc;
	}

	inline D3D12_TEXCUBE_SRV textureCubeSRVDesc(const TextureCubeSRVDesc& inDesc)
	{
		return D3D12_TEXCUBE_SRV{
			.MostDetailedMip     = inDesc.mostDetailedMip,
			.MipLevels           = inDesc.mipLevels,
			.ResourceMinLODClamp = inDesc.minLODClamp,
		};
	}

	inline D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc(const ShaderResourceViewDesc& inDesc)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
		desc.Format                  = into_d3d::pixelFormat(inDesc.format);
		desc.ViewDimension           = into_d3d::srvDimension(inDesc.viewDimension);
		// NOTE: Shader4ComponentMapping must be D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING (0x1688) for structured buffers.
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		switch (inDesc.viewDimension)
		{
			case ESRVDimension::Unknown:                           CHECK_NO_ENTRY(); break;
			case ESRVDimension::Buffer:                            desc.Buffer = into_d3d::bufferSRVDesc(inDesc.buffer); break;
			case ESRVDimension::Texture1D:                         CHECK_NO_ENTRY(); break;
			case ESRVDimension::Texture1DArray:                    CHECK_NO_ENTRY(); break;
			case ESRVDimension::Texture2D:                         desc.Texture2D = into_d3d::texture2DSRVDesc(inDesc.texture2D); break;
			case ESRVDimension::Texture2DArray:                    CHECK_NO_ENTRY(); break;
			case ESRVDimension::Texture2DMultiSampled:             CHECK_NO_ENTRY(); break;
			case ESRVDimension::Texture2DMultiSampledArray:        CHECK_NO_ENTRY(); break;
			case ESRVDimension::Texture3D:                         CHECK_NO_ENTRY(); break;
			case ESRVDimension::TextureCube:                       desc.TextureCube = into_d3d::textureCubeSRVDesc(inDesc.textureCube); break;
			case ESRVDimension::TextureCubeArray:                  CHECK_NO_ENTRY(); break;
			case ESRVDimension::RaytracingAccelerationStructure:   CHECK_NO_ENTRY(); break;
			default:                                               CHECK_NO_ENTRY(); break;
		}
		return desc;
	}

	inline D3D12_UAV_DIMENSION uavDimension(EUAVDimension inDimension)
	{
		switch (inDimension)
		{
			case EUAVDimension::Unknown:        return D3D12_UAV_DIMENSION_UNKNOWN;
			case EUAVDimension::Buffer:         return D3D12_UAV_DIMENSION_BUFFER;
			case EUAVDimension::Texture1D:      return D3D12_UAV_DIMENSION_TEXTURE1D;
			case EUAVDimension::Texture1DArray: return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
			case EUAVDimension::Texture2D:      return D3D12_UAV_DIMENSION_TEXTURE2D;
			case EUAVDimension::Texture2DArray: return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			case EUAVDimension::Texture3D:      return D3D12_UAV_DIMENSION_TEXTURE3D;
		}
		CHECK_NO_ENTRY();
		return D3D12_UAV_DIMENSION_UNKNOWN;
	}

	inline D3D12_BUFFER_UAV_FLAGS bufferUAVFlags(EBufferUAVFlags inFlags)
	{
		D3D12_BUFFER_UAV_FLAGS flags = D3D12_BUFFER_UAV_FLAG_NONE;
		if (0 != (inFlags & EBufferUAVFlags::Raw))
		{
			flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		}
		return flags;
	}

	inline D3D12_BUFFER_UAV bufferUAVDesc(const BufferUAVDesc& inDesc)
	{
		D3D12_BUFFER_UAV desc{};
		desc.FirstElement         = inDesc.firstElement;
		desc.NumElements          = inDesc.numElements;
		desc.StructureByteStride  = inDesc.structureByteStride;
		desc.CounterOffsetInBytes = inDesc.counterOffsetInBytes;
		desc.Flags                = into_d3d::bufferUAVFlags(inDesc.flags);
		return desc;
	}

	inline D3D12_TEX2D_UAV texture2DUAVDesc(const Texture2DUAVDesc& inDesc)
	{
		D3D12_TEX2D_UAV desc{};
		desc.MipSlice = inDesc.mipSlice;
		desc.PlaneSlice = inDesc.planeSlice;
		return desc;
	}

	inline D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc(const UnorderedAccessViewDesc& inDesc)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
		desc.Format        = into_d3d::pixelFormat(inDesc.format);
		desc.ViewDimension = into_d3d::uavDimension(inDesc.viewDimension);
		switch (inDesc.viewDimension)
		{
			case EUAVDimension::Unknown:        CHECK_NO_ENTRY(); break;
			case EUAVDimension::Buffer:         desc.Buffer = into_d3d::bufferUAVDesc(inDesc.buffer); break;
			case EUAVDimension::Texture1D:      CHECK_NO_ENTRY(); break;
			case EUAVDimension::Texture2D:      desc.Texture2D = into_d3d::texture2DUAVDesc(inDesc.texture2D); break;
			case EUAVDimension::Texture2DArray: CHECK_NO_ENTRY(); break;
			case EUAVDimension::Texture3D:      CHECK_NO_ENTRY(); break;
			default:                            CHECK_NO_ENTRY(); break;
		}
		return desc;
	}

	inline D3D12_RTV_DIMENSION rtvDimension(ERTVDimension inDimension)
	{
		switch (inDimension)
		{
			case ERTVDimension::Unknown          : return D3D12_RTV_DIMENSION_UNKNOWN;
			case ERTVDimension::Buffer           : return D3D12_RTV_DIMENSION_BUFFER;
			case ERTVDimension::Texture1D        : return D3D12_RTV_DIMENSION_TEXTURE1D;
			case ERTVDimension::Texture1DArray   : return D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
			case ERTVDimension::Texture2D        : return D3D12_RTV_DIMENSION_TEXTURE2D;
			case ERTVDimension::Texture2DArray   : return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			case ERTVDimension::Texture2DMS      : return D3D12_RTV_DIMENSION_TEXTURE2DMS;
			case ERTVDimension::Texture2DMSArray : return D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			case ERTVDimension::Texture3D        : return D3D12_RTV_DIMENSION_TEXTURE3D;
		}
		CHECK_NO_ENTRY();
		return D3D12_RTV_DIMENSION_UNKNOWN;
	}

	inline D3D12_TEX2D_RTV texture2DRTVDesc(const Texture2DRTVDesc& inDesc)
	{
		return D3D12_TEX2D_RTV{
			.MipSlice   = inDesc.mipSlice,
			.PlaneSlice = inDesc.planeSlice,
		};
	}

	inline D3D12_RENDER_TARGET_VIEW_DESC rtvDesc(const RenderTargetViewDesc& inDesc)
	{
		D3D12_RENDER_TARGET_VIEW_DESC desc{};
		desc.Format        = into_d3d::pixelFormat(inDesc.format);
		desc.ViewDimension = into_d3d::rtvDimension(inDesc.viewDimension);
		switch (inDesc.viewDimension)
		{
			case ERTVDimension::Unknown          : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Buffer           : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Texture1D        : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Texture1DArray   : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Texture2D        : desc.Texture2D = into_d3d::texture2DRTVDesc(inDesc.texture2D); break;
			case ERTVDimension::Texture2DArray   : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Texture2DMS      : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Texture2DMSArray : CHECK_NO_ENTRY(); break;
			case ERTVDimension::Texture3D        : CHECK_NO_ENTRY(); break;
		}
		return desc;
	}

	inline D3D12_DSV_DIMENSION dsvDimension(EDSVDimension inDimension)
	{
		switch (inDimension)
		{
			case EDSVDimension::Unknown:          return D3D12_DSV_DIMENSION_UNKNOWN;
			case EDSVDimension::Texture1D:        return D3D12_DSV_DIMENSION_TEXTURE1D;
			case EDSVDimension::Texture1DArray:   return D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
			case EDSVDimension::Texture2D:        return D3D12_DSV_DIMENSION_TEXTURE2D;
			case EDSVDimension::Texture2DArray:   return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
			case EDSVDimension::Texture2DMS:      return D3D12_DSV_DIMENSION_TEXTURE2DMS;
			case EDSVDimension::Texture2DMSArray: return D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
		}
		CHECK_NO_ENTRY(); return D3D12_DSV_DIMENSION_UNKNOWN;
	}

	inline D3D12_DSV_FLAGS dsvFlags(EDSVFlags inFlags)
	{
		D3D12_DSV_FLAGS flags = D3D12_DSV_FLAG_NONE;
		if (ENUM_HAS_FLAG(inFlags, EDSVFlags::OnlyDepth))   flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		if (ENUM_HAS_FLAG(inFlags, EDSVFlags::OnlyStencil)) flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		return flags;
	}

	inline D3D12_TEX2D_DSV texture2DDSVDesc(const Texture2DDSVDesc& inDesc)
	{
		return D3D12_TEX2D_DSV{
			.MipSlice = inDesc.mipSlice,
		};
	}

	inline D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc(const DepthStencilViewDesc& inDesc)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
		desc.Format        = into_d3d::pixelFormat(inDesc.format);
		desc.ViewDimension = into_d3d::dsvDimension(inDesc.viewDimension);
		desc.Flags         = into_d3d::dsvFlags(inDesc.flags);
		switch (inDesc.viewDimension)
		{
			case EDSVDimension::Unknown:          CHECK_NO_ENTRY(); break;
			case EDSVDimension::Texture1D:        CHECK_NO_ENTRY(); break;
			case EDSVDimension::Texture1DArray:   CHECK_NO_ENTRY(); break;
			case EDSVDimension::Texture2D:        desc.Texture2D = into_d3d::texture2DDSVDesc(inDesc.texture2D); break;
			case EDSVDimension::Texture2DArray:   CHECK_NO_ENTRY(); break;
			case EDSVDimension::Texture2DMS:      CHECK_NO_ENTRY(); break;
			case EDSVDimension::Texture2DMSArray: CHECK_NO_ENTRY(); break;
			default:                              CHECK_NO_ENTRY(); break;
		}
		return desc;
	}

	inline D3D12_RESOURCE_FLAGS bufferResourceFlags(EBufferAccessFlags inFlags)
	{
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
		if (0 != (inFlags & EBufferAccessFlags::UAV))
		{
			flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		}
		return flags;
	}

	inline D3D12_INDIRECT_ARGUMENT_TYPE indirectArgumentType(EIndirectArgumentType inType)
	{
		return static_cast<D3D12_INDIRECT_ARGUMENT_TYPE>(inType);
	}

	void indirectArgument(const IndirectArgumentDesc& inDesc, D3D12_INDIRECT_ARGUMENT_DESC& outDesc, D3DGraphicsPipelineState* pipelineState);

	uint32 calcIndirectArgumentByteStride(const IndirectArgumentDesc& inDesc);
	uint32 calcCommandSignatureByteStride(const CommandSignatureDesc& inDesc, uint32& outPaddingBytes);

	void commandSignature(
		const CommandSignatureDesc& inDesc,
		D3D12_COMMAND_SIGNATURE_DESC& outDesc,
		D3DGraphicsPipelineState* pipelineState,
		TempAlloc& tempAlloc);

}
