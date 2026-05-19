#include "clear_resource_pass.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"

// #todo-renderer: Support arbitrary number of requests.
static const uint32 maxTextureClearDescriptorsPerFrame = 1024;

struct ClearResourcePushConstants
{
	uint32 width;
	uint32 height;
	uint32 depth;
	uint32 _pad0;
	uint32 clearValue[4];
};

static EClearTextureDimension getClearTextureDim(const TextureCreateParams& desc)
{
	switch (desc.dimension)
	{
		case ETextureDimension::UNKNOWN: CHECK_NO_ENTRY(); break;
		case ETextureDimension::TEXTURE1D: return EClearTextureDimension::TEXTURE_DIMENSION_1D;
		case ETextureDimension::TEXTURE2D: return EClearTextureDimension::TEXTURE_DIMENSION_2D;
		case ETextureDimension::TEXTURE3D: return EClearTextureDimension::TEXTURE_DIMENSION_3D;
		default: CHECK_NO_ENTRY(); break;
	}
	return EClearTextureDimension::Count;
}

static EClearTextureFormat getClearTextureFormat(const TextureCreateParams& desc)
{
	switch (desc.format)
	{
		case EPixelFormat::R8G8B8A8_UNORM:           return EClearTextureFormat::TEXTURE_FORMAT_FLOAT4;
		case EPixelFormat::B8G8R8A8_UNORM:           return EClearTextureFormat::TEXTURE_FORMAT_FLOAT4;
		case EPixelFormat::R32_FLOAT:                return EClearTextureFormat::TEXTURE_FORMAT_FLOAT1;
		case EPixelFormat::R32G32_FLOAT:             return EClearTextureFormat::TEXTURE_FORMAT_FLOAT2;
		case EPixelFormat::R32G32B32A32_FLOAT:       return EClearTextureFormat::TEXTURE_FORMAT_FLOAT4;
		case EPixelFormat::R16G16B16A16_FLOAT:       return EClearTextureFormat::TEXTURE_FORMAT_FLOAT4;
		case EPixelFormat::R16G16_FLOAT:             return EClearTextureFormat::TEXTURE_FORMAT_FLOAT2;
		case EPixelFormat::R16_FLOAT:                return EClearTextureFormat::TEXTURE_FORMAT_FLOAT1;
		case EPixelFormat::R32_UINT:                 return EClearTextureFormat::TEXTURE_FORMAT_UINT1;
		case EPixelFormat::R16_UINT:                 return EClearTextureFormat::TEXTURE_FORMAT_UINT1;
		case EPixelFormat::R8_UINT:                  return EClearTextureFormat::TEXTURE_FORMAT_UINT1;
		case EPixelFormat::R32G32B32A32_UINT:        return EClearTextureFormat::TEXTURE_FORMAT_UINT4;
		case EPixelFormat::R16G16_SINT:              return EClearTextureFormat::TEXTURE_FORMAT_INT2;
	}
	CHECK_NO_ENTRY();
	return EClearTextureFormat::Count;
}

void ClearResourcePass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;

	passDescriptor.initialize(L"ClearResource", device->maxFramesInFlight(), 0);

	auto createPipeline = [device = this->device]
		(const char* debugName, const wchar_t* filepath, const char* entryName, uint32 dimension, uint32 format, UniquePtr<ComputePipelineState>& pipeline)
	{
		std::wstring wDimension = L"TEXTURE_DIMENSION_ENUM=" + std::to_wstring(dimension);
		std::wstring wFormat = L"TEXTURE_FORMAT_ENUM=" + std::to_wstring(format);

		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, debugName);
		shader->declarePushConstants({ { "pushConstants", (int32)(sizeof(ClearResourcePushConstants) / 4) } });
		shader->loadFromFile(filepath, entryName, { wDimension, wFormat });

		pipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	};

	const wchar_t* wFilepath = L"util/clear_texture.hlsl";
	const char* entryNames[] = { "clear1D", "clear2D", "clear3D" };
	for (uint32 i = 0; i < (uint32)EClearTextureDimension::Count; ++i)
	{
		for (uint32 j = 0; j < (uint32)EClearTextureFormat::Count; ++j)
		{
			char debugName[256];
			sprintf_s(debugName, "ClearTexture_%u_%u", i, j);

			createPipeline(debugName, wFilepath, entryNames[i], i, j, pipelines[i][j]);
		}
	}
}

void ClearResourcePass::prepareForFrame(const FrameInfo& frameInfo)
{
	passDescriptor.resizeDescriptorHeap(frameInfo, maxTextureClearDescriptorsPerFrame);
	tracker = DescriptorIndexTracker{};
}

void ClearResourcePass::enqueueClear(Texture* texture, UnorderedAccessView* uav, ClearValue clearValue)
{
	texturesToClear.push_back(texture);
	UAVsToClear.push_back(uav);
	clearValues.push_back(clearValue);
}

void ClearResourcePass::executeClears(RenderCommandList* commandList, const FrameInfo& frameInfo)
{
	std::vector<TextureBarrierAuto> textureBarriers;
	textureBarriers.reserve(texturesToClear.size());
	for (size_t i = 0; i < texturesToClear.size(); ++i)
	{
		TextureBarrierAuto texBarrier{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			texturesToClear[i], BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None,
		};
		textureBarriers.emplace_back(texBarrier);
	}
	commandList->barrierAuto(0, nullptr, (uint32)textureBarriers.size(), textureBarriers.data(), 0, nullptr);

	DescriptorHeap* descriptorHeap = passDescriptor.getDescriptorHeap(frameInfo);

	for (size_t i = 0; i < texturesToClear.size(); ++i)
	{
		const auto& desc = texturesToClear[i]->getCreateParams();
		const EClearTextureDimension dim = getClearTextureDim(desc);
		const EClearTextureFormat format = getClearTextureFormat(desc);
		const auto& clearValue = clearValues[i];

		ClearResourcePushConstants pushConstants{};
		pushConstants.width = desc.width;
		pushConstants.height = desc.height;
		pushConstants.depth = desc.depth;
		if (clearValue.valueType == EClearValueType::Float)
		{
			std::memcpy(pushConstants.clearValue, clearValue.asFloat, sizeof(clearValue.asFloat));
		}
		else if (clearValue.valueType == EClearValueType::UInt)
		{
			std::memcpy(pushConstants.clearValue, clearValue.asUInt, sizeof(clearValue.asUInt));
		}
		else if (clearValue.valueType == EClearValueType::Int)
		{
			std::memcpy(pushConstants.clearValue, clearValue.asInt, sizeof(clearValue.asInt));
		}

		ShaderParameterTable SPT{};
		SPT.pushConstants("pushConstants", &pushConstants, sizeof(pushConstants));
		SPT.rwTexture("rwTexture", UAVsToClear[i]);

		// #todo-renderer: Sort requests by dimension and format to minimize pipeline changes?
		auto pipeline = pipelines[(uint32)dim][(uint32)format].get();
		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap, &tracker);

		if (dim == EClearTextureDimension::TEXTURE_DIMENSION_1D)
		{
			uint32 dispatchX = (desc.width + 63) / 64;
			commandList->dispatchCompute(dispatchX, 1, 1);
		}
		else if (dim == EClearTextureDimension::TEXTURE_DIMENSION_2D)
		{
			uint32 dispatchX = (desc.width + 7) / 8;
			uint32 dispatchY = (desc.height + 7) / 8;
			commandList->dispatchCompute(dispatchX, dispatchY, 1);
		}
		else if (dim == EClearTextureDimension::TEXTURE_DIMENSION_3D)
		{
			uint32 dispatchX = (desc.width + 3) / 4;
			uint32 dispatchY = (desc.height + 3) / 4;
			uint32 dispatchZ = (desc.depth + 3) / 4;
			commandList->dispatchCompute(dispatchX, dispatchY, dispatchZ);
		}
	}

	texturesToClear.clear();
	UAVsToClear.clear();
	clearValues.clear();
}
