#pragma once

#if COMPILE_BACKEND_VULKAN

#include "core/assertion.h"
#include "render/pipeline_state.h"
#include <vulkan/vulkan_core.h>

// Convert API-agnostic structs into Vulkan structs
namespace into_vk
{
	class TempAlloc
	{
	public:
		~TempAlloc()
		{
			//
		}

	private:

	};

	inline VkViewport viewport(const Viewport& inViewport)
	{
		VkViewport vkViewport{};
		vkViewport.x = inViewport.topLeftX;
		vkViewport.y = inViewport.topLeftY;
		vkViewport.width = inViewport.width;
		vkViewport.height = inViewport.height;
		vkViewport.minDepth = inViewport.minDepth;
		vkViewport.maxDepth = inViewport.maxDepth;
		return vkViewport;
	}

	inline VkRect2D scissorRect(const ScissorRect& scissorRect)
	{
		VkRect2D vkScissor{};
		vkScissor.extent.width = scissorRect.right - scissorRect.left;
		vkScissor.extent.height = scissorRect.bottom - scissorRect.top;
		vkScissor.offset.x = scissorRect.left;
		vkScissor.offset.y = scissorRect.top;
		return vkScissor;
	}

	inline VkPrimitiveTopology primitiveTopology(EPrimitiveTopology inTopology)
	{
		switch (inTopology)
		{
			case EPrimitiveTopology::UNDEFINED: return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
			case EPrimitiveTopology::POINTLIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case EPrimitiveTopology::LINELIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case EPrimitiveTopology::LINESTRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case EPrimitiveTopology::TRIANGLELIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case EPrimitiveTopology::TRIANGLESTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case EPrimitiveTopology::LINELIST_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
			case EPrimitiveTopology::LINESTRIP_ADJ: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
			case EPrimitiveTopology::TRIANGLELIST_ADJ: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
			case EPrimitiveTopology::TRIANGLESTRIP_ADJ: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
		}
		CHECK_NO_ENTRY();
		return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	}

	// NOTE: DX12 differentiates
	//   D3D12_PRIMITIVE_TOPOLOGY_TYPE for D3D12_GRAPHICS_PIPELINE_STATE_DESC
	//   and D3D12_PRIMITIVE_TOPOLOGY for IASetPrimitiveTopology(),
	//   but Vulkan uses VkPrimitiveTopology for both.
	// #todo-vulkan: But I can't specify 'strip' variants in this way.
	//   Maybe the type of GraphicsPipelineDesc::primitiveTopologyType
	//   should be EPrimitiveTopology, not EPrimitiveTopologyType?
	inline VkPrimitiveTopology primitiveTopologyType(EPrimitiveTopologyType inTopologyType)
	{
		switch (inTopologyType)
		{
			case EPrimitiveTopologyType::Point: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case EPrimitiveTopologyType::Line: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case EPrimitiveTopologyType::Triangle: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			//case EPrimitiveTopologyType::Patch:
			default:
				// #todo-vulkan: PATCHLIST
				CHECK_NO_ENTRY();
		}
		CHECK_NO_ENTRY();
		return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	}

	inline VkShaderStageFlagBits shaderStage(EShaderStage inStage)
	{
		switch (inStage)
		{
			case EShaderStage::VERTEX_SHADER: return VK_SHADER_STAGE_VERTEX_BIT;
			case EShaderStage::DOMAIN_SHADER: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			case EShaderStage::HULL_SHADER: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			case EShaderStage::GEOMETRY_SHADER: return VK_SHADER_STAGE_GEOMETRY_BIT;
			case EShaderStage::PIXEL_SHADER: return VK_SHADER_STAGE_FRAGMENT_BIT;
			case EShaderStage::COMPUTE_SHADER: return VK_SHADER_STAGE_COMPUTE_BIT;
			case EShaderStage::MESH_SHADER: return VK_SHADER_STAGE_MESH_BIT_NV;
			case EShaderStage::AMPLICATION_SHADER: return VK_SHADER_STAGE_TASK_BIT_NV;
			case EShaderStage::RT_INTERSECTION_SHADER: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
			case EShaderStage::RT_ANYHIT_SHADER: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
			case EShaderStage::RT_CLOSESTHIT_SHADER: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
			case EShaderStage::RT_MISS_SHADER: return VK_SHADER_STAGE_MISS_BIT_KHR;
			case EShaderStage::NUM_TYPES: return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}
		CHECK_NO_ENTRY();
		return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	}

	inline VkImageType textureDimension(ETextureDimension dimension)
	{
		switch (dimension)
		{
			case ETextureDimension::UNKNOWN:
			{
				CHECK_NO_ENTRY(); // #todo-vulkan: There is no 'typeless' in Vulkan?
				return VK_IMAGE_TYPE_MAX_ENUM;
			}
			case ETextureDimension::TEXTURE1D: return VkImageType::VK_IMAGE_TYPE_1D;
			case ETextureDimension::TEXTURE2D: return VkImageType::VK_IMAGE_TYPE_2D;
			case ETextureDimension::TEXTURE3D: return VkImageType::VK_IMAGE_TYPE_3D;
		}
		CHECK_NO_ENTRY();
		return VkImageType::VK_IMAGE_TYPE_MAX_ENUM;
	}

	inline VkFormat pixelFormat(EPixelFormat inFormat)
	{
		switch (inFormat)
		{
			case EPixelFormat::UNKNOWN:            return VkFormat::VK_FORMAT_UNDEFINED;
			// #todo-vulkan: R32_TYPLESS in Vulkan?
			case EPixelFormat::R32_TYPELESS:       return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R8G8B8A8_UNORM:     return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
			case EPixelFormat::R32G32B32_FLOAT:    return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
			case EPixelFormat::R32G32B32A32_FLOAT: return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
			case EPixelFormat::R32_UINT:           return VkFormat::VK_FORMAT_R32_UINT;
			case EPixelFormat::R16_UINT:           return VkFormat::VK_FORMAT_R16_UINT;
			case EPixelFormat::D24_UNORM_S8_UINT:  return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
		}
		CHECK_NO_ENTRY();
		return VkFormat::VK_FORMAT_UNDEFINED;
	}

	inline VkSampleCountFlagBits sampleCount(uint32 count)
	{
		switch (count)
		{
			case 1: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
			case 2: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_2_BIT;
			case 4: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_4_BIT;
			case 8: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_8_BIT;
			case 16: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_16_BIT;
			case 32: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_32_BIT;
			case 64: return VkSampleCountFlagBits::VK_SAMPLE_COUNT_64_BIT;
			default: CHECK_NO_ENTRY();
		}
		CHECK_NO_ENTRY();
		return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
	}

	inline VkImageCreateInfo textureDesc(const TextureCreateParams& params)
	{
		VkImageCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.imageType = textureDimension(params.dimension);
		desc.extent.width = params.width;
		desc.extent.height = params.height;
		desc.extent.depth = params.depth;
		desc.mipLevels = params.mipLevels;
		desc.arrayLayers = params.numLayers;
		desc.format = pixelFormat(params.format);
		desc.tiling = VK_IMAGE_TILING_OPTIMAL; // #todo-vulkan: Texture tiling param
		// [VUID-VkImageCreateInfo-initialLayout-00993]
		// initialLayout must be VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_PREINITIALIZED
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		desc.samples = sampleCount(params.sampleCount);
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// #todo-vulkan: VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT for textureCube
		desc.flags = (VkImageCreateFlagBits)0;

		// #todo-vulkan: Other allow flags
		desc.usage = (VkImageUsageFlagBits)0;
		if (0 != (params.accessFlags & ETextureAccessFlags::SRV))
		{
			desc.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::RTV))
		{
			desc.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::UAV))
		{
			desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::DSV))
		{
			desc.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}

		return desc;
	}

	inline VkCompareOp compareOp(EComparisonFunc inComp)
	{
		switch (inComp)
		{
			case EComparisonFunc::Never: return VK_COMPARE_OP_NEVER;
			case EComparisonFunc::Less: return VK_COMPARE_OP_LESS;
			case EComparisonFunc::Equal: return VK_COMPARE_OP_EQUAL;
			case EComparisonFunc::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
			case EComparisonFunc::Greater: return VK_COMPARE_OP_GREATER;
			case EComparisonFunc::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
			case EComparisonFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case EComparisonFunc::Always: return VK_COMPARE_OP_ALWAYS;
			default: CHECK_NO_ENTRY();
		}
		return VK_COMPARE_OP_MAX_ENUM;
	}

	inline VkLogicOp logicOp(ELogicOp inOp)
	{
		switch (inOp)
		{
			case ELogicOp::Clear: return VK_LOGIC_OP_CLEAR;
			case ELogicOp::Set: return VK_LOGIC_OP_SET;
			case ELogicOp::Copy: return VK_LOGIC_OP_COPY;
			case ELogicOp::CopyInverted: return VK_LOGIC_OP_COPY_INVERTED;
			case ELogicOp::Noop: return VK_LOGIC_OP_NO_OP;
			case ELogicOp::Invert: return VK_LOGIC_OP_INVERT;
			case ELogicOp::And: return VK_LOGIC_OP_AND;
			case ELogicOp::Nand: return VK_LOGIC_OP_NAND;
			case ELogicOp::Or: return VK_LOGIC_OP_OR;
			case ELogicOp::Nor: return VK_LOGIC_OP_NOR;
			case ELogicOp::Xor: return VK_LOGIC_OP_XOR;
			case ELogicOp::Equivalent: return VK_LOGIC_OP_EQUIVALENT;
			case ELogicOp::AndReverse: return VK_LOGIC_OP_AND_REVERSE;
			case ELogicOp::AndInverted: return VK_LOGIC_OP_AND_INVERTED;
			case ELogicOp::OrReverse: return VK_LOGIC_OP_OR_REVERSE;
			case ELogicOp::OrInverted: return VK_LOGIC_OP_OR_INVERTED;
			default: CHECK_NO_ENTRY();
		}
		return VK_LOGIC_OP_MAX_ENUM;
	}

	inline VkPipelineColorBlendStateCreateInfo colorBlendDesc(const BlendDesc& inDesc, uint32 numAttachments)
	{
		// #todo-vulkan: Independent blend state is an extension in Vulkan
		VkPipelineColorBlendStateCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		desc.logicOpEnable = inDesc.renderTarget[0].logicOpEnable;
		desc.logicOp = into_vk::logicOp(inDesc.renderTarget[0].logicOp);
		desc.attachmentCount = numAttachments;
		desc.pAttachments = 0; // #todo-vulkan-wip: I need TempAlloc again?
		desc.blendConstants[0] = 0.0f;
		desc.blendConstants[1] = 0.0f;
		desc.blendConstants[2] = 0.0f;
		desc.blendConstants[3] = 0.0f;
	}

	inline VkPipelineDepthStencilStateCreateInfo depthstencilDesc(const DepthstencilDesc& inDesc)
	{
		VkPipelineDepthStencilStateCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		desc.depthTestEnable = inDesc.depthEnable;
		desc.depthWriteEnable = (inDesc.depthWriteMask == EDepthWriteMask::All);
		desc.depthCompareOp = into_vk::compareOp(inDesc.depthFunc);
		desc.depthBoundsTestEnable = VK_FALSE; // #todo-vulkan: depthBoundsTestEnable
		desc.minDepthBounds = 0.0f; // Optional
		desc.maxDepthBounds = 1.0f; // Optional
		desc.stencilTestEnable = inDesc.stencilEnable;
		desc.front = {}; // #todo-vulkan: VkStencilOpState
		desc.back = {};
		return desc;
	}
}

#endif // COMPILE_BACKEND_VULKAN
