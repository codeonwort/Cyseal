#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "core/assertion.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_barrier.h"

#include <vulkan/vulkan_core.h>
#include <algorithm>

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

	inline constexpr VkImageLayout imageLayout(ETextureMemoryLayout layout)
	{
		switch (layout)
		{
			case ETextureMemoryLayout::COMMON                : return VK_IMAGE_LAYOUT_UNDEFINED;
			case ETextureMemoryLayout::RENDER_TARGET         : return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			case ETextureMemoryLayout::DEPTH_STENCIL_TARGET  : return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			case ETextureMemoryLayout::PIXEL_SHADER_RESOURCE : return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			case ETextureMemoryLayout::UNORDERED_ACCESS      : return VK_IMAGE_LAYOUT_GENERAL;
			case ETextureMemoryLayout::COPY_SRC              : return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			case ETextureMemoryLayout::COPY_DEST             : return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			case ETextureMemoryLayout::PRESENT               : return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}
		CHECK_NO_ENTRY();
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}

	inline VkBufferMemoryBarrier bufferMemoryBarrier(const BufferMemoryBarrier& barrier)
	{
		// #wip-critical: Access masks for buffer
		VkAccessFlags srcAccessMask = VK_ACCESS_NONE;
		VkAccessFlags dstAccessMask = VK_ACCESS_NONE;

		return VkBufferMemoryBarrier{
			.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext               = nullptr,
			.srcAccessMask       = srcAccessMask,
			.dstAccessMask       = dstAccessMask,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer              = static_cast<VkBuffer>(barrier.buffer->getRawResource()),
			.offset              = (VkDeviceSize)barrier.offset,
			.size                = (VkDeviceSize)barrier.size,
		};
	}

	inline VkImageMemoryBarrier imageMemoryBarrier(const TextureMemoryBarrier& barrier)
	{
		VkImageLayout oldLayout = imageLayout(barrier.stateBefore);
		VkImageLayout newLayout = imageLayout(barrier.stateAfter);
		VkPipelineStageFlags srcStage, dstStage; // #wip-critical: Cant't use them here
		VkAccessFlags srcAccessMask, dstAccessMask;
		VkImageAspectFlags aspectMask;
		findImageBarrierFlags(
			oldLayout, newLayout, VK_FORMAT_UNDEFINED, // #wip-critical: Cant't know format here
			&srcStage, &dstStage,
			&srcAccessMask, &dstAccessMask,
			&aspectMask);

		// #wip-critical: Take subresource as an argument
		VkImageSubresourceRange subresourceRange{
			.aspectMask     = aspectMask,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		};

		return VkImageMemoryBarrier{
			.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext               = nullptr,
			.srcAccessMask       = srcAccessMask,
			.dstAccessMask       = dstAccessMask,
			.oldLayout           = oldLayout,
			.newLayout           = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = static_cast<VkImage>(barrier.texture->getRawResource()),
			.subresourceRange    = subresourceRange,
		};
	}

	inline VkViewport viewport(const Viewport& inViewport)
	{
		return VkViewport{
			.x        = inViewport.topLeftX,
			.y        = inViewport.topLeftY,
			.width    = inViewport.width,
			.height   = inViewport.height,
			.minDepth = inViewport.minDepth,
			.maxDepth = inViewport.maxDepth,
		};
	}

	inline VkRect2D scissorRect(const ScissorRect& scissorRect)
	{
		return VkRect2D{
			.offset = VkOffset2D{
				.x = (int32)scissorRect.left,
				.y = (int32)scissorRect.top,
			},
			.extent = VkExtent2D{
				.width = scissorRect.right - scissorRect.left,
				.height = scissorRect.bottom - scissorRect.top,
			},
		};
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
			case EPrimitiveTopologyType::Point    : return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			case EPrimitiveTopologyType::Line     : return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case EPrimitiveTopologyType::Triangle : return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			//case EPrimitiveTopologyType::Patch: // #todo-vulkan: PATCHLIST
		}
		CHECK_NO_ENTRY();
		return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	}

	inline VkShaderStageFlagBits shaderStage(EShaderStage inStage)
	{
		switch (inStage)
		{
			case EShaderStage::VERTEX_SHADER          : return VK_SHADER_STAGE_VERTEX_BIT;
			case EShaderStage::HULL_SHADER            : return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			case EShaderStage::DOMAIN_SHADER          : return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			case EShaderStage::GEOMETRY_SHADER        : return VK_SHADER_STAGE_GEOMETRY_BIT;
			case EShaderStage::PIXEL_SHADER           : return VK_SHADER_STAGE_FRAGMENT_BIT;
			case EShaderStage::COMPUTE_SHADER         : return VK_SHADER_STAGE_COMPUTE_BIT;
			case EShaderStage::MESH_SHADER            : return VK_SHADER_STAGE_MESH_BIT_NV;
			case EShaderStage::AMPLICATION_SHADER     : return VK_SHADER_STAGE_TASK_BIT_NV;
			case EShaderStage::RT_RAYGEN_SHADER       : return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
			case EShaderStage::RT_ANYHIT_SHADER       : return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
			case EShaderStage::RT_CLOSESTHIT_SHADER   : return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
			case EShaderStage::RT_MISS_SHADER         : return VK_SHADER_STAGE_MISS_BIT_KHR;
			case EShaderStage::RT_INTERSECTION_SHADER : return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
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
			case EPixelFormat::UNKNOWN            : return VkFormat::VK_FORMAT_UNDEFINED;
			// #todo-vulkan: R32_TYPLESS in Vulkan?
			case EPixelFormat::R32_TYPELESS       : return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R8G8B8A8_UNORM     : return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
			case EPixelFormat::B8G8R8A8_UNORM     : return VkFormat::VK_FORMAT_B8G8R8A8_UNORM;
			case EPixelFormat::R32G32_FLOAT       : return VkFormat::VK_FORMAT_R32G32_SFLOAT;
			case EPixelFormat::R32G32B32_FLOAT    : return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
			case EPixelFormat::R32G32B32A32_FLOAT : return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
			case EPixelFormat::R16G16B16A16_FLOAT : return VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
			case EPixelFormat::R32_UINT           : return VkFormat::VK_FORMAT_R32_UINT;
			case EPixelFormat::R16_UINT           : return VkFormat::VK_FORMAT_R16_UINT;
			case EPixelFormat::D24_UNORM_S8_UINT  : return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
		}
		CHECK_NO_ENTRY();
		return VkFormat::VK_FORMAT_UNDEFINED;
	}

	inline VkSampleCountFlagBits sampleCount(uint32 count)
	{
		switch (count)
		{
			case 1  : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
			case 2  : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_2_BIT;
			case 4  : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_4_BIT;
			case 8  : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_8_BIT;
			case 16 : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_16_BIT;
			case 32 : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_32_BIT;
			case 64 : return VkSampleCountFlagBits::VK_SAMPLE_COUNT_64_BIT;
		}
		CHECK_NO_ENTRY();
		return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
	}

	inline VkImageCreateInfo textureDesc(const TextureCreateParams& params)
	{
		// #todo-vulkan: Other allow flags
		VkImageUsageFlags usage = 0;
		if (0 != (params.accessFlags & ETextureAccessFlags::SRV))
		{
			usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::RTV))
		{
			usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::UAV))
		{
			usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		}
		if (0 != (params.accessFlags & ETextureAccessFlags::DSV))
		{
			usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}

		return VkImageCreateInfo{
			.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.pNext                 = nullptr,
			// #todo-vulkan: VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT for textureCube
			.flags                 = (VkImageCreateFlagBits)0,
			.imageType             = textureDimension(params.dimension),
			.format                = pixelFormat(params.format),
			.extent                = { params.width, params.height, params.depth },
			.mipLevels             = params.mipLevels,
			.arrayLayers           = params.numLayers,
			.samples               = sampleCount(params.sampleCount),
			.tiling                = VK_IMAGE_TILING_OPTIMAL, // #todo-vulkan: Texture tiling param
			.usage                 = usage,
			.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
			// Queue family is ignored if sharingMode is not VK_SHARING_MODE_CONCURRENT
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices   = nullptr,
			// [VUID-VkImageCreateInfo-initialLayout-00993]
			// initialLayout must be VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_PREINITIALIZED
			.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
		};
	}

	inline VkCompareOp compareOp(EComparisonFunc inComp)
	{
		switch (inComp)
		{
			case EComparisonFunc::Never        : return VK_COMPARE_OP_NEVER;
			case EComparisonFunc::Less         : return VK_COMPARE_OP_LESS;
			case EComparisonFunc::Equal        : return VK_COMPARE_OP_EQUAL;
			case EComparisonFunc::LessEqual    : return VK_COMPARE_OP_LESS_OR_EQUAL;
			case EComparisonFunc::Greater      : return VK_COMPARE_OP_GREATER;
			case EComparisonFunc::NotEqual     : return VK_COMPARE_OP_NOT_EQUAL;
			case EComparisonFunc::GreaterEqual : return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case EComparisonFunc::Always       : return VK_COMPARE_OP_ALWAYS;
		}
		CHECK_NO_ENTRY();
		return VK_COMPARE_OP_MAX_ENUM;
	}

	inline VkLogicOp logicOp(ELogicOp inOp)
	{
		switch (inOp)
		{
			case ELogicOp::Clear        : return VK_LOGIC_OP_CLEAR;
			case ELogicOp::Set          : return VK_LOGIC_OP_SET;
			case ELogicOp::Copy         : return VK_LOGIC_OP_COPY;
			case ELogicOp::CopyInverted : return VK_LOGIC_OP_COPY_INVERTED;
			case ELogicOp::Noop         : return VK_LOGIC_OP_NO_OP;
			case ELogicOp::Invert       : return VK_LOGIC_OP_INVERT;
			case ELogicOp::And          : return VK_LOGIC_OP_AND;
			case ELogicOp::Nand         : return VK_LOGIC_OP_NAND;
			case ELogicOp::Or           : return VK_LOGIC_OP_OR;
			case ELogicOp::Nor          : return VK_LOGIC_OP_NOR;
			case ELogicOp::Xor          : return VK_LOGIC_OP_XOR;
			case ELogicOp::Equivalent   : return VK_LOGIC_OP_EQUIVALENT;
			case ELogicOp::AndReverse   : return VK_LOGIC_OP_AND_REVERSE;
			case ELogicOp::AndInverted  : return VK_LOGIC_OP_AND_INVERTED;
			case ELogicOp::OrReverse    : return VK_LOGIC_OP_OR_REVERSE;
			case ELogicOp::OrInverted   : return VK_LOGIC_OP_OR_INVERTED;
		}
		CHECK_NO_ENTRY();
		return VK_LOGIC_OP_MAX_ENUM;
	}

	inline VkColorComponentFlags colorWriteMask(EColorWriteEnable inMask)
	{
		VkColorComponentFlags mask = 0;
		if (0 != (inMask & EColorWriteEnable::Red))
		{
			mask |= VK_COLOR_COMPONENT_R_BIT;
		}
		if (0 != (inMask & EColorWriteEnable::Green))
		{
			mask |= VK_COLOR_COMPONENT_G_BIT;
		}
		if (0 != (inMask & EColorWriteEnable::Blue))
		{
			mask |= VK_COLOR_COMPONENT_B_BIT;
		}
		if (0 != (inMask & EColorWriteEnable::Alpha))
		{
			mask |= VK_COLOR_COMPONENT_A_BIT;
		}
		return mask;
	}

	inline VkBlendFactor blendFactor(EBlend inBlend)
	{
		switch (inBlend)
		{
			case EBlend::Zero             : return VK_BLEND_FACTOR_ZERO;
			case EBlend::One              : return VK_BLEND_FACTOR_ONE;
			case EBlend::SrcColor         : return VK_BLEND_FACTOR_SRC_COLOR;
			case EBlend::InvSrcColor      : return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
			case EBlend::SrcAlpha         : return VK_BLEND_FACTOR_SRC_ALPHA;
			case EBlend::InvSrcAlpha      : return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case EBlend::DestAlpha        : return VK_BLEND_FACTOR_DST_ALPHA;
			case EBlend::InvDestAlpha     : return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			case EBlend::DestColor        : return VK_BLEND_FACTOR_DST_COLOR;
			case EBlend::InvDestColor     : return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
			case EBlend::SrcAlphaSaturate : return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
			// #todo-vulkan: Equivalent of OMSetBlendFactor()?
			// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCmdSetBlendConstants.html
			case EBlend::BlendFactor      : CHECK_NO_ENTRY();
			case EBlend::InvBlendFactor   : CHECK_NO_ENTRY();
			case EBlend::Src1Color        : return VK_BLEND_FACTOR_SRC1_COLOR;
			case EBlend::InvSrc1Color     : return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
			case EBlend::Src1Alpha        : return VK_BLEND_FACTOR_SRC1_ALPHA;
			case EBlend::InvSrc1Alpha     : return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
		}
		CHECK_NO_ENTRY();
		return VK_BLEND_FACTOR_MAX_ENUM;
	}

	inline VkBlendOp blendOp(EBlendOp inOp)
	{
		// #todo-vulkan: A bunch of EXT blendOps
		switch (inOp)
		{
			case EBlendOp::Add         : return VK_BLEND_OP_ADD;
			case EBlendOp::Subtract    : return VK_BLEND_OP_SUBTRACT;
			case EBlendOp::RevSubtract : return VK_BLEND_OP_REVERSE_SUBTRACT;
			case EBlendOp::Min         : return VK_BLEND_OP_MIN;
			case EBlendOp::Max         : return VK_BLEND_OP_MAX;
		}
		CHECK_NO_ENTRY();
		return VK_BLEND_OP_MAX_ENUM;
	}

	inline VkPipelineDepthStencilStateCreateInfo depthstencilDesc(const DepthstencilDesc& inDesc)
	{
		return VkPipelineDepthStencilStateCreateInfo{
			.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.pNext                 = nullptr,
			.flags                 = (VkPipelineDepthStencilStateCreateFlags)0,
			.depthTestEnable       = inDesc.depthEnable,
			.depthWriteEnable      = (inDesc.depthWriteMask == EDepthWriteMask::All),
			.depthCompareOp        = into_vk::compareOp(inDesc.depthFunc),
			.depthBoundsTestEnable = VK_FALSE, // #todo-vulkan: depthBoundsTestEnable
			.stencilTestEnable     = inDesc.stencilEnable,
			.front                 = VkStencilOpState{}, // #todo-vulkan: VkStencilOpState
			.back                  = VkStencilOpState{},
			.minDepthBounds        = 0.0f, // Optional
			.maxDepthBounds        = 1.0f, // Optional
		};
	}

	inline VkPolygonMode polygonMode(EFillMode inMode)
	{
		// #todo-vulkan: Missing VkPolygonMode (POINT, FILL_RECTANGLE_NV)
		switch (inMode)
		{
			case EFillMode::Line : return VK_POLYGON_MODE_LINE;
			case EFillMode::Fill : return VK_POLYGON_MODE_FILL;
		}
		CHECK_NO_ENTRY();
		return VK_POLYGON_MODE_MAX_ENUM;
	}

	inline VkCullModeFlags cullMode(ECullMode inMode)
	{
		// #todo-vulkan: Missing VkCullModeFlags (FRONT_AND_BACK)
		switch (inMode)
		{
			case ECullMode::None  : return VK_CULL_MODE_NONE;
			case ECullMode::Front : return VK_CULL_MODE_FRONT_BIT;
			case ECullMode::Back  : return VK_CULL_MODE_BACK_BIT;
		}
		CHECK_NO_ENTRY();
		return VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
	}

	inline VkDescriptorType descriptorPoolType(EDescriptorHeapType inType)
	{
		switch (inType)
		{
			case EDescriptorHeapType::CBV         : return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case EDescriptorHeapType::SRV         : return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case EDescriptorHeapType::UAV         : return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case EDescriptorHeapType::SAMPLER     : return VK_DESCRIPTOR_TYPE_SAMPLER;
			case EDescriptorHeapType::RTV         : return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			case EDescriptorHeapType::DSV         : return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			// #todo-vulkan: See VulkanDevice::createDescriptorHeap
			// D3D12 backend needs this type as it can only bind two heaps. (CbvSrvUav heap + Sampler heap)
			// But there is no equivalent field for the type in VkDescriptorType.
			case EDescriptorHeapType::CBV_SRV_UAV: CHECK_NO_ENTRY();
		}
		CHECK_NO_ENTRY();
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}

	inline VkVertexInputRate vertexInputRate(EVertexInputClassification inRate)
	{
		switch (inRate)
		{
			case EVertexInputClassification::PerVertex   : return VK_VERTEX_INPUT_RATE_VERTEX;
			case EVertexInputClassification::PerInstance : return VK_VERTEX_INPUT_RATE_INSTANCE;
		}
		CHECK_NO_ENTRY();
		return VK_VERTEX_INPUT_RATE_MAX_ENUM;
	}

	// #todo-vulkan: Should I redefine VertexInputElement?
	inline void vertexInputBindings(
		const std::vector<VertexInputElement>& inElements,
		std::vector<VkVertexInputBindingDescription>& outBindings)
	{
		std::vector<VertexInputElement> elems = inElements;
		std::sort(elems.begin(), elems.end(),
			[](const VertexInputElement& x, VertexInputElement& y)
			{
				return x.inputSlot < y.inputSlot;
			});
		
		for (size_t i = 0; i < elems.size(); ++i)
		{
			if (i == 0 || elems[i].inputSlot != elems[i - 1].inputSlot)
			{
				VkVertexInputBindingDescription bindingDesc{};
				bindingDesc.binding = elems[i].inputSlot;
				bindingDesc.inputRate = into_vk::vertexInputRate(elems[i].inputSlotClass);
				bindingDesc.stride = 0;
				for (size_t j = i; j < elems.size() && elems[i].inputSlot == elems[j].inputSlot; ++j)
				{
					uint32 x = elems[j].alignedByteOffset + getPixelFormatBytes(elems[j].format);
					bindingDesc.stride = std::max(bindingDesc.stride, x);
				}
				outBindings.emplace_back(bindingDesc);
			}
		}
	}

	inline VkVertexInputAttributeDescription vertexInputAttribute(const VertexInputElement& inElement)
	{
		return VkVertexInputAttributeDescription{
			.location = inElement.semanticIndex,
			.binding  = inElement.inputSlot,
			.format   = into_vk::pixelFormat(inElement.format),
			.offset   = inElement.alignedByteOffset,
		};
	}

	inline VkImageViewType imageViewType(ESRVDimension inSRVDimension)
	{
		switch (inSRVDimension)
		{
			case ESRVDimension::Unknown                         : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case ESRVDimension::Buffer                          : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case ESRVDimension::Texture1D                       : return VK_IMAGE_VIEW_TYPE_1D;
			case ESRVDimension::Texture1DArray                  : return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case ESRVDimension::Texture2D                       : return VK_IMAGE_VIEW_TYPE_2D;
			case ESRVDimension::Texture2DArray                  : return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case ESRVDimension::Texture2DMultiSampled           : return VK_IMAGE_VIEW_TYPE_2D;
			case ESRVDimension::Texture2DMultiSampledArray      : return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case ESRVDimension::Texture3D                       : return VK_IMAGE_VIEW_TYPE_3D;
			case ESRVDimension::TextureCube                     : return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case ESRVDimension::TextureCubeArray                : return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			case ESRVDimension::RaytracingAccelerationStructure : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		}
		CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}

	inline VkImageViewType imageViewType(EUAVDimension inDimension)
	{
		switch (inDimension)
		{
			case EUAVDimension::Unknown                    : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case EUAVDimension::Buffer                     : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case EUAVDimension::Texture1D                  : return VK_IMAGE_VIEW_TYPE_1D;
			case EUAVDimension::Texture1DArray             : return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case EUAVDimension::Texture2D                  : return VK_IMAGE_VIEW_TYPE_2D;
			case EUAVDimension::Texture2DArray             : return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case EUAVDimension::Texture3D                  : return VK_IMAGE_VIEW_TYPE_3D;
		}
		CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}

	inline VkImageViewType imageViewType(ERTVDimension inDimension)
	{
		switch (inDimension)
		{
			case ERTVDimension::Unknown                    : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case ERTVDimension::Buffer                     : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case ERTVDimension::Texture1D                  : return VK_IMAGE_VIEW_TYPE_1D;
			case ERTVDimension::Texture1DArray             : return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case ERTVDimension::Texture2D                  : return VK_IMAGE_VIEW_TYPE_2D;
			case ERTVDimension::Texture2DArray             : return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			case ERTVDimension::Texture2DMS                : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case ERTVDimension::Texture2DMSArray           : CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case ERTVDimension::Texture3D                  : return VK_IMAGE_VIEW_TYPE_3D;
		}
		CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}

	inline VkImageViewType imageViewType(EDSVDimension inDimension)
	{
		switch (inDimension)
		{
			case EDSVDimension::Unknown:          CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
			case EDSVDimension::Texture1D:        return VK_IMAGE_VIEW_TYPE_1D;
			case EDSVDimension::Texture1DArray:   return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			case EDSVDimension::Texture2D:        return VK_IMAGE_VIEW_TYPE_2D;
			case EDSVDimension::Texture2DArray:   return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			// #todo-vulkan: MS variants for vulkan? And what about VK_IMAGE_VIEW_TYPE_CUBE_ARRAY?
			//case EDSVDimension::Texture2DMS:      return VK_IMAGE_VIEW_TYPE_2D;
			//case EDSVDimension::Texture2DMSArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		}
		CHECK_NO_ENTRY(); return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}
}

#endif // COMPILE_BACKEND_VULKAN
