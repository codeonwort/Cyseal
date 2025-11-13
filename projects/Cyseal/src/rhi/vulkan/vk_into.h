#pragma once

#if COMPILE_BACKEND_VULKAN

#include "vk_utils.h"
#include "core/assertion.h"
#include "rhi/pipeline_state.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_barrier.h"

#define VK_NO_PROTOTYPES
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

	inline VkPipelineStageFlags2 barrierSync(EBarrierSync sync)
	{
		auto consumeFlag = [](EBarrierSync* flags, EBarrierSync flag) -> bool {
			bool hasFlag = ENUM_HAS_FLAG(*flags, flag);
			*flags = (EBarrierSync)((uint32)(*flags) & (~(uint32)flag));
			return hasFlag;
		};

		VkPipelineStageFlags2 vkFlags = 0;
		if (consumeFlag(&sync, EBarrierSync::ALL))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::DRAW))
		{
			//CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
			vkFlags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::INDEX_INPUT))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::VERTEX_SHADING))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::PIXEL_SHADING))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::DEPTH_STENCIL))
		{
			CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
			//vkFlags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
			//vkFlags |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::RENDER_TARGET))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::COMPUTE_SHADING))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::RAYTRACING))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
		}
		if (consumeFlag(&sync, EBarrierSync::COPY))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_COPY_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::RESOLVE))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::EXECUTE_INDIRECT))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::PREDICATION))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT;
		}
		if (consumeFlag(&sync, EBarrierSync::ALL_SHADING))
		{
			CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
		}
		if (consumeFlag(&sync, EBarrierSync::NON_PIXEL_SHADING))
		{
			CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
		}
		if (consumeFlag(&sync, EBarrierSync::EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO))
		{
			CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
		}
		if (consumeFlag(&sync, EBarrierSync::CLEAR_UNORDERED_ACCESS_VIEW))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_CLEAR_BIT;
		}
		if (consumeFlag(&sync, EBarrierSync::VIDEO_DECODE))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
		}
		if (consumeFlag(&sync, EBarrierSync::VIDEO_PROCESS))
		{
			CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
		}
		if (consumeFlag(&sync, EBarrierSync::VIDEO_ENCODE))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR;
		}
		if (consumeFlag(&sync, EBarrierSync::BUILD_RAYTRACING_ACCELERATION_STRUCTURE))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		}
		if (consumeFlag(&sync, EBarrierSync::COPY_RAYTRACING_ACCELERATION_STRUCTURE))
		{
			vkFlags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR;
		}
		if (consumeFlag(&sync, EBarrierSync::SPLIT))
		{
			CHECK_NO_ENTRY(); // #todo-barrier-vk: Proper flag?
		}

		return vkFlags;
	}

	inline VkAccessFlags2 barrierAccess(EBarrierAccess access)
	{
		auto consumeFlag = [](EBarrierAccess* flags, EBarrierAccess flag) -> bool {
			bool hasFlag = ENUM_HAS_FLAG(*flags, flag);
			*flags = (EBarrierAccess)((uint32)(*flags) & (~(uint32)flag));
			return hasFlag;
		};

		VkAccessFlags2 vkFlags = 0;
		if (consumeFlag(&access, EBarrierAccess::COMMON))
		{
			vkFlags |= VK_ACCESS_2_MEMORY_WRITE_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::VERTEX_BUFFER))
		{
			vkFlags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::CONSTANT_BUFFER))
		{
			vkFlags |= VK_ACCESS_2_UNIFORM_READ_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::INDEX_BUFFER))
		{
			vkFlags |= VK_ACCESS_2_INDEX_READ_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::RENDER_TARGET))
		{
			vkFlags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::UNORDERED_ACCESS))
		{
			// #todo-barrier-vk: D3D12_BARRIER_ACCESS_UNORDERED_ACCESS is a read/write state
			// but looks like Vulkan allows more fine-grained control?
			vkFlags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
			vkFlags |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::DEPTH_STENCIL_WRITE))
		{
			vkFlags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::DEPTH_STENCIL_READ))
		{
			vkFlags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::SHADER_RESOURCE))
		{
			vkFlags |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::STREAM_OUTPUT))
		{
			// #todo-barrier-vk: transform feedback is optional in Vulkan
			// and there are 3 flags... nah I won't ever use it anyway
			vkFlags |= VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
			//vkFlags |= VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT;
			//vkFlags |= VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
		}
		if (consumeFlag(&access, EBarrierAccess::INDIRECT_ARGUMENT))
		{
			vkFlags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
		}
#if 0
		// #todo-barrier-vk: Conditional rendering
		// Think I won't use it and its enum value conflicts with INDIRECT_ARGUMENT.
		if (consumeFlag(&access, EBarrierAccess::PREDICATION))
		{
			vkFlags |= VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT;
		}
#endif
		if (consumeFlag(&access, EBarrierAccess::COPY_DEST))
		{
			vkFlags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::COPY_SOURCE))
		{
			vkFlags |= VK_ACCESS_2_TRANSFER_READ_BIT;
		}
		if (consumeFlag(&access, EBarrierAccess::RESOLVE_DEST))
		{
			// #todo-barrier-vk: What to do here?
			CHECK_NO_ENTRY();
		}
		if (consumeFlag(&access, EBarrierAccess::RESOLVE_SOURCE))
		{
			// #todo-barrier-vk: What to do here?
			CHECK_NO_ENTRY();
		}
		if (consumeFlag(&access, EBarrierAccess::RAYTRACING_ACCELERATION_STRUCTURE_READ))
		{
			vkFlags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		}
		if (consumeFlag(&access, EBarrierAccess::RAYTRACING_ACCELERATION_STRUCTURE_WRITE))
		{
			vkFlags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		}
		if (consumeFlag(&access, EBarrierAccess::SHADING_RATE_SOURCE))
		{
			vkFlags |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
		}
		if (consumeFlag(&access, EBarrierAccess::VIDEO_DECODE_READ))
		{
			vkFlags |= VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
		}
		if (consumeFlag(&access, EBarrierAccess::VIDEO_DECODE_WRITE))
		{
			vkFlags |= VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
		}
		if (consumeFlag(&access, EBarrierAccess::VIDEO_PROCESS_READ))
		{
			// #todo-barrier-vk: What to do here?
			CHECK_NO_ENTRY();
		}
		if (consumeFlag(&access, EBarrierAccess::VIDEO_PROCESS_WRITE))
		{
			// #todo-barrier-vk: What to do here?
			CHECK_NO_ENTRY();
		}
		if (consumeFlag(&access, EBarrierAccess::VIDEO_ENCODE_READ))
		{
			vkFlags |= VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR;
		}
		if (consumeFlag(&access, EBarrierAccess::VIDEO_ENCODE_WRITE))
		{
			vkFlags |= VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR;
		}

		// #todo-barrier-vk: Is this right?
		if (consumeFlag(&access, EBarrierAccess::NO_ACCESS))
		{
			CHECK(vkFlags == 0);
			vkFlags = VK_ACCESS_2_NONE;
		}

		CHECK(access == 0); // If failed, not all flag bits were consumed;
		return vkFlags;
	}

	inline VkImageLayout barrierLayout(EBarrierLayout layout)
	{
		switch (layout)
		{
		case EBarrierLayout::Undefined:                   return VK_IMAGE_LAYOUT_UNDEFINED;
		case EBarrierLayout::Common:                      return VK_IMAGE_LAYOUT_GENERAL;
		case EBarrierLayout::Present:                     return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		case EBarrierLayout::GenericRead:                 CHECK_NO_ENTRY();
		case EBarrierLayout::RenderTarget:                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case EBarrierLayout::UnorderedAccess:             return VK_IMAGE_LAYOUT_GENERAL;
		case EBarrierLayout::DepthStencilWrite:           return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case EBarrierLayout::DepthStencilRead:            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		case EBarrierLayout::ShaderResource:              return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		case EBarrierLayout::CopySource:                  return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case EBarrierLayout::CopyDest:                    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case EBarrierLayout::ResolveSource:               CHECK_NO_ENTRY();
		case EBarrierLayout::ResolveDest:                 CHECK_NO_ENTRY();
		case EBarrierLayout::ShadingRateSource:           return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
		case EBarrierLayout::VideoDecodeRead:             return VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR;
		case EBarrierLayout::VideoDecodeWrite:            return VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
		case EBarrierLayout::VideoProcessRead:            CHECK_NO_ENTRY();
		case EBarrierLayout::VideoProcessWrite:           CHECK_NO_ENTRY();
		case EBarrierLayout::VideoEncodeRead:             return VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
		case EBarrierLayout::VideoEncodeWrite:            return VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR;
		case EBarrierLayout::DirectQueueCommon:           CHECK_NO_ENTRY();
		case EBarrierLayout::DirectQueueGenericRead:      CHECK_NO_ENTRY();
		case EBarrierLayout::DirectQueueUnorderedAccess:  CHECK_NO_ENTRY();
		case EBarrierLayout::DirectQueueShaderResource:   CHECK_NO_ENTRY();
		case EBarrierLayout::DirectQueueCopySource:       CHECK_NO_ENTRY();
		case EBarrierLayout::DirectQueueCopyDest:         CHECK_NO_ENTRY();
		case EBarrierLayout::ComputeQueueCommon:          CHECK_NO_ENTRY();
		case EBarrierLayout::ComputeQueueGenericRead:     CHECK_NO_ENTRY();
		case EBarrierLayout::ComputeQueueUnorderedAccess: CHECK_NO_ENTRY();
		case EBarrierLayout::ComputeQueueShaderResource:  CHECK_NO_ENTRY();
		case EBarrierLayout::ComputeQueueCopySource:      CHECK_NO_ENTRY();
		case EBarrierLayout::ComputeQueueCopyDest:        CHECK_NO_ENTRY();
		case EBarrierLayout::VideoQueueCommon:            CHECK_NO_ENTRY();
		}
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}

	inline VkImageSubresourceRange barrierSubresourceRange(const BarrierSubresourceRange& range, VkImageLayout newLayout)
	{
		VkImageAspectFlags aspectMask;
		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		else
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		// BarrierSubresourceRange{ 0xffffffff, ... } is d3d convention.
		if (range.isHolistic())
		{
			return VkImageSubresourceRange{
				.aspectMask     = aspectMask,
				.baseMipLevel   = 0,
				.levelCount     = VK_REMAINING_MIP_LEVELS,
				.baseArrayLayer = 0,
				.layerCount     = VK_REMAINING_ARRAY_LAYERS,
				// #todo-barrier-vk: firstPlane and numPlanes?
			};
		}

		return VkImageSubresourceRange{
			.aspectMask     = aspectMask,
			.baseMipLevel   = range.indexOrFirstMipLevel,
			.levelCount     = range.numMipLevels,
			.baseArrayLayer = range.firstArraySlice,
			.layerCount     = range.numArraySlices,
			// #todo-barrier-vk: firstPlane and numPlanes?
		};
	}

	inline VkBufferMemoryBarrier2 bufferMemoryBarrier(const BufferBarrier& barrier)
	{
		return VkBufferMemoryBarrier2{
			.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
			.pNext               = nullptr,
			.srcStageMask        = into_vk::barrierSync(barrier.syncBefore),
			.srcAccessMask       = into_vk::barrierAccess(barrier.accessBefore),
			.dstStageMask        = into_vk::barrierSync(barrier.syncAfter),
			.dstAccessMask       = into_vk::barrierAccess(barrier.accessAfter),
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer              = static_cast<VkBuffer>(barrier.buffer->getRawResource()),
			// #todo-barrier-vk: Custom offset and size for buffer barrier?
			.offset              = 0,
			.size                = VK_WHOLE_SIZE,
		};
	}

	inline VkImageMemoryBarrier2 imageMemoryBarrier(const TextureBarrier& barrier)
	{
		VkImageLayout newLayout = into_vk::barrierLayout(barrier.layoutAfter);
		return VkImageMemoryBarrier2{
			.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.pNext               = nullptr,
			.srcStageMask        = into_vk::barrierSync(barrier.syncBefore),
			.srcAccessMask       = into_vk::barrierAccess(barrier.accessBefore),
			.dstStageMask        = into_vk::barrierSync(barrier.syncAfter),
			.dstAccessMask       = into_vk::barrierAccess(barrier.accessAfter),
			.oldLayout           = into_vk::barrierLayout(barrier.layoutBefore),
			.newLayout           = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = static_cast<VkImage>(barrier.texture->getRawResource()),
			.subresourceRange    = barrierSubresourceRange(barrier.subresources, newLayout),
		};
	}

	inline VkMemoryBarrier2 globalMemoryBarrier(const GlobalBarrier& barrier)
	{
		return VkMemoryBarrier2{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			.pNext = nullptr,
			.srcStageMask = into_vk::barrierSync(barrier.syncBefore),
			.srcAccessMask       = into_vk::barrierAccess(barrier.accessBefore),
			.dstStageMask        = into_vk::barrierSync(barrier.syncAfter),
			.dstAccessMask       = into_vk::barrierAccess(barrier.accessAfter),
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
			case EPixelFormat::UNKNOWN                  : return VkFormat::VK_FORMAT_UNDEFINED;
			// #todo-vulkan: TYPELESS formats in Vulkan?
			case EPixelFormat::R32_TYPELESS             : CHECK_NO_ENTRY(); return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R24G8_TYPELESS           : CHECK_NO_ENTRY(); return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R24_UNORM_X8_TYPELESS    : CHECK_NO_ENTRY(); return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R32G8X24_TYPELESS        : CHECK_NO_ENTRY(); return VkFormat::VK_FORMAT_R64_SFLOAT;
			case EPixelFormat::R32_FLOAT_X8X24_TYPELESS : CHECK_NO_ENTRY(); return VkFormat::VK_FORMAT_R64_SFLOAT;
			case EPixelFormat::R8G8B8A8_UNORM           : return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
			case EPixelFormat::B8G8R8A8_UNORM           : return VkFormat::VK_FORMAT_B8G8R8A8_UNORM;
			case EPixelFormat::R32_FLOAT                : return VkFormat::VK_FORMAT_R32_SFLOAT;
			case EPixelFormat::R32G32_FLOAT             : return VkFormat::VK_FORMAT_R32G32_SFLOAT;
			case EPixelFormat::R32G32B32_FLOAT          : return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
			case EPixelFormat::R32G32B32A32_FLOAT       : return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
			case EPixelFormat::R16G16B16A16_FLOAT       : return VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
			case EPixelFormat::R16G16_FLOAT             : return VkFormat::VK_FORMAT_R16G16_SFLOAT;
			case EPixelFormat::R16_FLOAT                : return VkFormat::VK_FORMAT_R16_SFLOAT;
			case EPixelFormat::R32_UINT                 : return VkFormat::VK_FORMAT_R32_UINT;
			case EPixelFormat::R16_UINT                 : return VkFormat::VK_FORMAT_R16_UINT;
			case EPixelFormat::R32G32B32A32_UINT        : return VkFormat::VK_FORMAT_R32G32B32A32_UINT;
			case EPixelFormat::D24_UNORM_S8_UINT        : return VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
			case EPixelFormat::D32_FLOAT_S8_UINT        : return VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT;
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
		if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::SRV))
		{
			usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::RTV))
		{
			usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::UAV))
		{
			usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		}
		if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::DSV))
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
		if (ENUM_HAS_FLAG(inMask, EColorWriteEnable::Red))
		{
			mask |= VK_COLOR_COMPONENT_R_BIT;
		}
		if (ENUM_HAS_FLAG(inMask, EColorWriteEnable::Green))
		{
			mask |= VK_COLOR_COMPONENT_G_BIT;
		}
		if (ENUM_HAS_FLAG(inMask, EColorWriteEnable::Blue))
		{
			mask |= VK_COLOR_COMPONENT_B_BIT;
		}
		if (ENUM_HAS_FLAG(inMask, EColorWriteEnable::Alpha))
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

	inline VkShaderStageFlags shaderStageFlags(EShaderVisibility inFlags)
	{
		// #todo-vulkan: D3D12_SHADER_VISIBILITY is single enum but VkShaderStageFlags is enum flags.
		VkShaderStageFlags vkFlags = 0;
		switch (inFlags)
		{
			case EShaderVisibility::All:      vkFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_ALL;                         break;
			case EShaderVisibility::Vertex:   vkFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;                  break;
			case EShaderVisibility::Hull:     vkFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;    break;
			case EShaderVisibility::Domain:   vkFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; break;
			case EShaderVisibility::Geometry: vkFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_GEOMETRY_BIT;                break;
			case EShaderVisibility::Pixel:    vkFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;                break;
			default: CHECK_NO_ENTRY(); break; // #todo-vulkan: VkShaderStageFlags contains more flags.
		}
		return vkFlags;
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
					bindingDesc.stride = (std::max)(bindingDesc.stride, x);
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
