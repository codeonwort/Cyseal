#include "base_pass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "gpu_culling.h"
#include "util/logging.h"

#include "rhi/render_device.h"
#include "rhi/rhi_policy.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

DEFINE_LOG_CATEGORY_STATIC(LogBasePass);

// #wip: gather what to do...
/*
- Need to share GPU culling result with depth prepass.
- Need to create separate graphics pipelines for depth prepass and base pass.
  - Need vs/ps for pipeline creation.
*/

#define kMaxIndirectDrawCommandCount 256
// #todo-renderer: Support other topologies
#define kPrimitiveTopology           EPrimitiveTopology::TRIANGLELIST

BasePass::~BasePass()
{
	delete shaderVS;
	delete shaderPS;
}

void BasePass::initialize(RenderDevice* inRenderDevice, EPixelFormat inSceneColorFormat, const EPixelFormat inGbufferFormats[], uint32 numGBuffers, EPixelFormat inVelocityMapFormat)
{
	device = inRenderDevice;
	sceneColorFormat = inSceneColorFormat;
	gbufferFormats.assign(inGbufferFormats, inGbufferFormats + numGBuffers);
	velocityMapFormat = inVelocityMapFormat;

	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	passDescriptor.initialize(L"BasePass", swapchainCount, 0);

	// Shader stages
	shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BasePassVS");
	shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BasePassPS");
	shaderVS->declarePushConstants({ { "pushConstants", 1} });
	shaderPS->declarePushConstants({ { "pushConstants", 1} });
	shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS");
	shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS");

	for (size_t i = 0; i < GraphicsPipelineKeyDesc::numPipelineKeyDescs(); ++i)
	{
		auto pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		auto pipelineState = createPipeline(GraphicsPipelineKeyDesc::kPipelineKeyDescs[i]);
		auto indirectDrawHelper = createIndirectDrawHelper(pipelineState, pipelineKey);
		pipelinePermutation.insertPipeline(pipelineKey, GraphicsPipelineItem{ pipelineState, indirectDrawHelper });
	}
}

void BasePass::renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput)
{
	auto scene    = passInput.scene;
	auto gpuScene = passInput.gpuScene;

	if (gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}
	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

	// #todo-basepass: Need smarter way to generate drawlists per pipeline if permutation blows up.
	BasePassDrawList drawsForDefaultPipelines;
	BasePassDrawList drawsForNoCullPipelines;
	drawsForDefaultPipelines.reserve(scene->totalMeshSectionsLOD0);
	drawsForNoCullPipelines.reserve(scene->totalMeshSectionsLOD0);
	{
		uint32 objectID = 0;
		for (const StaticMeshProxy* mesh : scene->staticMeshes)
		{
			for (const StaticMeshSection& section : mesh->getSections())
			{
				bool bDoubleSided = section.material->bDoubleSided;
				if (bDoubleSided)
				{
					drawsForNoCullPipelines.meshes.push_back(&section);
					drawsForNoCullPipelines.objectIDs.push_back(objectID);
				}
				else
				{
					drawsForDefaultPipelines.meshes.push_back(&section);
					drawsForDefaultPipelines.objectIDs.push_back(objectID);
				}
				++objectID;
			}
		}
	}

	if (passInput.bGPUCulling)
	{
		passInput.gpuCulling->resetCullingResources();
	}

	// Bind shader parameters except for root constants.
	// #note: Assumes all permutation share the same root signature.
	{
		auto key = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc);
		auto defaultPipeline = pipelinePermutation.findPipeline(key).pipelineState;

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("shadowMask", passInput.shadowMaskSRV);
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		uint32 requiredVolatiles = SPT.totalDescriptors();
		passDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

		DescriptorHeap* volatileHeap = passDescriptor.getDescriptorHeap(swapchainIndex);
		commandList->bindGraphicsShaderParameters(defaultPipeline, &SPT, volatileHeap);
	}

	const GraphicsPipelineKey defaultPipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kDefaultPipelineKeyDesc);
	const GraphicsPipelineKey noCullPipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(GraphicsPipelineKeyDesc::kNoCullPipelineKeyDesc);
	renderForPipeline(commandList, swapchainIndex, passInput, defaultPipelineKey, drawsForDefaultPipelines);
	renderForPipeline(commandList, swapchainIndex, passInput, noCullPipelineKey, drawsForNoCullPipelines);
}

void BasePass::renderForPipeline(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput, GraphicsPipelineKey pipelineKey, const BasePassDrawList& drawList)
{
	auto scene              = passInput.scene;
	auto camera             = passInput.camera;
	auto bIndirectDraw      = passInput.bIndirectDraw;
	auto bGPUCulling        = passInput.bGPUCulling;
	auto sceneUniformBuffer = passInput.sceneUniformBuffer;
	auto gpuScene           = passInput.gpuScene;
	auto gpuCulling         = passInput.gpuCulling;
	auto shadowMaskSRV      = passInput.shadowMaskSRV;

	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

	auto pipelineItem = pipelinePermutation.findPipeline(pipelineKey);
	GraphicsPipelineState* pipelineState = pipelineItem.pipelineState;
	IndirectDrawHelper* indirectDrawHelper = pipelineItem.indirectDrawHelper;

	indirectDrawHelper->resizeResources(swapchainIndex, gpuScene->getGPUSceneItemMaxCount());
	auto argumentBufferGenerator = indirectDrawHelper->argumentBufferGenerator.get();

	// Fill the indirect draw buffer and perform GPU culling.
	uint32 maxIndirectDraws = 0;
	if (bIndirectDraw)
	{
		uint32 indirectCommandID = 0;
		for (size_t i = 0; i < drawList.meshes.size(); ++i)
		{
			const StaticMeshSection* section = drawList.meshes[i];
			const uint32 objectID = drawList.objectIDs[i];

			VertexBuffer* positionBuffer = section->positionBuffer->getGPUResource().get();
			VertexBuffer* nonPositionBuffer = section->nonPositionBuffer->getGPUResource().get();
			IndexBuffer* indexBuffer = section->indexBuffer->getGPUResource().get();

			argumentBufferGenerator->beginCommand(indirectCommandID);

			argumentBufferGenerator->writeConstant32(objectID);
			argumentBufferGenerator->writeVertexBufferView(positionBuffer);
			argumentBufferGenerator->writeVertexBufferView(nonPositionBuffer);
			argumentBufferGenerator->writeIndexBufferView(indexBuffer);
			argumentBufferGenerator->writeDrawIndexedArguments(indexBuffer->getIndexCount(), 1, 0, 0, 0);

			argumentBufferGenerator->endCommand();

			++indirectCommandID;
		}

		maxIndirectDraws = indirectCommandID;
		Buffer* currentArgumentBuffer = indirectDrawHelper->argumentBuffer.at(swapchainIndex);
		argumentBufferGenerator->copyToBuffer(commandList, maxIndirectDraws, currentArgumentBuffer, 0);

		if (bGPUCulling)
		{
			GPUCullingInput cullingPassInput{
				.camera                      = camera,
				.gpuScene                    = gpuScene,
				.maxDrawCommands             = maxIndirectDraws,
				.indirectDrawBuffer          = currentArgumentBuffer,
				.culledIndirectDrawBuffer    = indirectDrawHelper->culledArgumentBuffer.at(swapchainIndex),
				.drawCounterBuffer           = indirectDrawHelper->drawCounterBuffer.at(swapchainIndex),
				.indirectDrawBufferSRV       = indirectDrawHelper->argumentBufferSRV.at(swapchainIndex),
				.culledIndirectDrawBufferUAV = indirectDrawHelper->culledArgumentBufferUAV.at(swapchainIndex),
				.drawCounterBufferUAV        = indirectDrawHelper->drawCounterBufferUAV.at(swapchainIndex),
			};
			gpuCulling->cullDrawCommands(commandList, swapchainIndex, cullingPassInput);
		}
	}

	commandList->setGraphicsPipelineState(pipelineState);
	commandList->iaSetPrimitiveTopology(kPrimitiveTopology);
	
	if (bIndirectDraw)
	{
		if (bGPUCulling)
		{
			Buffer* argBuffer = indirectDrawHelper->culledArgumentBuffer.at(swapchainIndex);
			Buffer* counterBuffer = indirectDrawHelper->drawCounterBuffer.at(swapchainIndex);
			commandList->executeIndirect(indirectDrawHelper->commandSignature.get(), maxIndirectDraws, argBuffer, 0, counterBuffer, 0);
		}
		else
		{
			Buffer* argBuffer = indirectDrawHelper->argumentBuffer.at(swapchainIndex);
			commandList->executeIndirect(indirectDrawHelper->commandSignature.get(), maxIndirectDraws, argBuffer, 0, nullptr, 0);
		}
	}
	else
	{
		commandList->beginRenderPass();

		for (size_t i = 0; i < drawList.meshes.size(); ++i)
		{
			const StaticMeshSection* section = drawList.meshes[i];
			const uint32 objectID = drawList.objectIDs[i];

			ShaderParameterTable SPT{};
			SPT.pushConstant("pushConstants", objectID);
			commandList->updateGraphicsRootConstants(pipelineState, &SPT);

			VertexBuffer* vertexBuffers[] = {
				section->positionBuffer->getGPUResource().get(),
				section->nonPositionBuffer->getGPUResource().get()
			};
			auto indexBuffer = section->indexBuffer->getGPUResource().get();

			commandList->iaSetVertexBuffers(0, _countof(vertexBuffers), vertexBuffers);
			commandList->iaSetIndexBuffer(indexBuffer);
			commandList->drawIndexedInstanced(indexBuffer->getIndexCount(), 1, 0, 0, 0);
		}

		commandList->endRenderPass();
	}
}

GraphicsPipelineState* BasePass::createPipeline(const GraphicsPipelineKeyDesc& pipelineKeyDesc)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	RasterizerDesc rasterizerDesc = RasterizerDesc();
	rasterizerDesc.cullMode = pipelineKeyDesc.cullMode;

	// Input layout
	// #todo-basepass: Should be variant per vertex factory
	VertexInputLayout inputLayout = {
			{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
			{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
			{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};

	std::vector<StaticSamplerDesc> staticSamplers = {
		StaticSamplerDesc{
			.name             = "albedoSampler",
			.filter           = ETextureFilter::MIN_MAG_MIP_LINEAR,
			.addressU         = ETextureAddressMode::Wrap,
			.addressV         = ETextureAddressMode::Wrap,
			.addressW         = ETextureAddressMode::Wrap,
			.mipLODBias       = 0.0f,
			.maxAnisotropy    = 0,
			.comparisonFunc   = EComparisonFunc::Always,
			.borderColor      = EStaticBorderColor::TransparentBlack,
			.minLOD           = 0.0f,
			.maxLOD           = FLT_MAX,
			.shaderVisibility = EShaderVisibility::All,
		},
	};

	const uint32 numRTVs = (uint32)(1 + gbufferFormats.size() + 1); // sceneColor + gbuffers + velocityMap
	GraphicsPipelineDesc pipelineDesc{
		.vs                     = shaderVS,
		.ps                     = shaderPS,
		.blendDesc              = BlendDesc(),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = std::move(rasterizerDesc),
		.depthstencilDesc       = getReverseZPolicy() == EReverseZPolicy::Reverse ? DepthstencilDesc::ReverseZSceneDepth() : DepthstencilDesc::StandardSceneDepth(),
		.inputLayout            = inputLayout,
		.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
		.numRenderTargets       = numRTVs,
		.rtvFormats             = { EPixelFormat::UNKNOWN, }, // Fill later
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc = SampleDesc{
			.count              = swapchain->supports4xMSAA() ? 4u : 1u,
			.quality            = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0,
		},
		.staticSamplers         = std::move(staticSamplers),
	};
	uint32 rtvIndex = 0;
	pipelineDesc.rtvFormats[rtvIndex++] = sceneColorFormat;
	for (size_t i = 0; i < gbufferFormats.size(); ++i)
	{
		pipelineDesc.rtvFormats[rtvIndex++] = gbufferFormats[i];
	}
	pipelineDesc.rtvFormats[rtvIndex++] = velocityMapFormat;
	CHECK(rtvIndex == numRTVs);

	GraphicsPipelineKey pipelineKey = GraphicsPipelineKeyDesc::assemblePipelineKey(pipelineKeyDesc);
	GraphicsPipelineState* pipelineState = device->createGraphicsPipelineState(pipelineDesc);

	return pipelineState;
}

IndirectDrawHelper* BasePass::createIndirectDrawHelper(GraphicsPipelineState* pipelineState, GraphicsPipelineKey pipelineKey)
{
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	IndirectDrawHelper* helper = new(EMemoryTag::Renderer) IndirectDrawHelper(device, pipelineKey);
	helper->argumentBuffer.initialize(swapchainCount);
	helper->argumentBufferSRV.initialize(swapchainCount);
	helper->culledArgumentBuffer.initialize(swapchainCount);
	helper->culledArgumentBufferUAV.initialize(swapchainCount);
	helper->drawCounterBuffer.initialize(swapchainCount);
	helper->drawCounterBufferUAV.initialize(swapchainCount);

	// Hmm... C++20 designated initializers looks ugly in this case :(
	CommandSignatureDesc commandSignatureDesc{
		.argumentDescs = {
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::CONSTANT,
				.name = "pushConstants",
				.constant = {
					.destOffsetIn32BitValues = 0,
					.num32BitValuesToSet = 1,
				},
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::VERTEX_BUFFER_VIEW,
				.vertexBuffer = {
					.slot = 0, // position buffer slot
				},
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::VERTEX_BUFFER_VIEW,
				.vertexBuffer = {
					.slot = 1, // non-position buffer slot
				},
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::INDEX_BUFFER_VIEW,
			},
			IndirectArgumentDesc{
				.type = EIndirectArgumentType::DRAW_INDEXED,
			},
		},
		.nodeMask = 0,
	};
	helper->commandSignature = UniquePtr<CommandSignature>(
		device->createCommandSignature(commandSignatureDesc, pipelineState));

	helper->argumentBufferGenerator = UniquePtr<IndirectCommandGenerator>(
		device->createIndirectCommandGenerator(commandSignatureDesc, kMaxIndirectDrawCommandCount));

	// Fixed size. Create here.
	for (uint32 i = 0; i < swapchainCount; ++i)
	{
		helper->drawCounterBuffer[i] = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = sizeof(uint32),
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::UAV,
			}
		));

		wchar_t debugName[256];
		swprintf_s(debugName, L"Buffer_IndirectDrawCounterBuffer_%u_%u", pipelineKey, i);
		helper->drawCounterBuffer[i]->setDebugName(debugName);

		UnorderedAccessViewDesc uavDesc{};
		uavDesc.format                      = EPixelFormat::UNKNOWN;
		uavDesc.viewDimension               = EUAVDimension::Buffer;
		uavDesc.buffer.firstElement         = 0;
		uavDesc.buffer.numElements          = 1;
		uavDesc.buffer.structureByteStride  = sizeof(uint32);
		uavDesc.buffer.counterOffsetInBytes = 0;
		uavDesc.buffer.flags                = EBufferUAVFlags::None;

		helper->drawCounterBufferUAV[i] = UniquePtr<UnorderedAccessView>(
			device->createUAV(helper->drawCounterBuffer[i].get(), uavDesc));
	}

	return helper;
}
