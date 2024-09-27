#include "base_pass.h"
#include "material.h"
#include "static_mesh.h"
#include "gpu_scene.h"
#include "gpu_culling.h"
#include "util/logging.h"

#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/gpu_resource_binding.h"
#include "rhi/gpu_resource_view.h"
#include "rhi/shader.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/vertex_buffer_pool.h"

// Force MRT formats for now.
#define PF_sceneColor            EPixelFormat::R32G32B32A32_FLOAT
#define PF_thinGBufferA          EPixelFormat::R16G16B16A16_FLOAT

DEFINE_LOG_CATEGORY_STATIC(LogBasePass);

void BasePass::initialize()
{
	RenderDevice* device = gRenderDevice;
	SwapChain* swapchain = device->getSwapChain();
	const uint32 swapchainCount = swapchain->getBufferCount();

	argumentBuffer.initialize(swapchainCount);
	argumentBufferSRV.initialize(swapchainCount);
	culledArgumentBuffer.initialize(swapchainCount);
	culledArgumentBufferUAV.initialize(swapchainCount);
	drawCounterBuffer.initialize(swapchainCount);
	drawCounterBufferUAV.initialize(swapchainCount);

	totalVolatileDescriptor.resize(swapchainCount, 0);
	volatileViewHeap.initialize(swapchainCount);

	// Input layout
	// #todo: Should be variant per vertex factory
	VertexInputLayout inputLayout = {
			{"POSITION", 0, EPixelFormat::R32G32B32_FLOAT, 0, 0, EVertexInputClassification::PerVertex, 0},
			{"NORMAL", 0, EPixelFormat::R32G32B32_FLOAT, 1, 0, EVertexInputClassification::PerVertex, 0},
			{"TEXCOORD", 0, EPixelFormat::R32G32_FLOAT, 1, sizeof(float) * 3, EVertexInputClassification::PerVertex, 0}
	};

	// Shader stages
	ShaderStage* shaderVS = device->createShader(EShaderStage::VERTEX_SHADER, "BasePassVS");
	ShaderStage* shaderPS = device->createShader(EShaderStage::PIXEL_SHADER, "BasePassPS");
	shaderVS->declarePushConstants({ "pushConstants" });
	shaderPS->declarePushConstants({ "pushConstants" });
	shaderVS->loadFromFile(L"base_pass.hlsl", "mainVS");
	shaderPS->loadFromFile(L"base_pass.hlsl", "mainPS");

	// PSO
	{
		GraphicsPipelineDesc desc;
		desc.inputLayout            = inputLayout;
		desc.vs                     = shaderVS;
		desc.ps                     = shaderPS;
		desc.rasterizerDesc         = RasterizerDesc();
		desc.blendDesc              = BlendDesc();
		desc.depthstencilDesc       = DepthstencilDesc::StandardSceneDepth();
		desc.sampleMask             = 0xffffffff;
		desc.primitiveTopologyType  = EPrimitiveTopologyType::Triangle;
		desc.numRenderTargets       = 2;
		desc.rtvFormats[0]          = PF_sceneColor;
		desc.rtvFormats[1]          = PF_thinGBufferA;
		desc.sampleDesc.count       = swapchain->supports4xMSAA() ? 4 : 1;
		desc.sampleDesc.quality     = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0;
		desc.dsvFormat              = swapchain->getBackbufferDepthFormat();

		pipelineState = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(desc));
	}

	// Cleanup
	{
		delete shaderVS;
		delete shaderPS;
	}

	// Indirect draw
	{
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
		commandSignature = UniquePtr<CommandSignature>(
			device->createCommandSignature(commandSignatureDesc, pipelineState.get()));

		argumentBufferGenerator = UniquePtr<IndirectCommandGenerator>(
			device->createIndirectCommandGenerator(commandSignatureDesc, 256));

		// Fixed size. Create here.
		for (uint32 i = 0; i < swapchainCount; ++i)
		{
			drawCounterBuffer[i] = UniquePtr<Buffer>(gRenderDevice->createBuffer(
				BufferCreateParams{
					.sizeInBytes = sizeof(uint32),
					.alignment   = 0,
					.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::UAV,
				}
			));

			wchar_t debugName[256];
			swprintf_s(debugName, L"Buffer_IndirectDrawCounterBuffer_%u", i);
			drawCounterBuffer[i]->setDebugName(debugName);

			UnorderedAccessViewDesc uavDesc{};
			uavDesc.format                      = EPixelFormat::UNKNOWN;
			uavDesc.viewDimension               = EUAVDimension::Buffer;
			uavDesc.buffer.firstElement         = 0;
			uavDesc.buffer.numElements          = 1;
			uavDesc.buffer.structureByteStride  = sizeof(uint32);
			uavDesc.buffer.counterOffsetInBytes = 0;
			uavDesc.buffer.flags                = EBufferUAVFlags::None;

			drawCounterBufferUAV[i] = UniquePtr<UnorderedAccessView>(
				gRenderDevice->createUAV(drawCounterBuffer[i].get(), uavDesc));
		}
	}
}

void BasePass::renderBasePass(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const SceneProxy* scene,
	const Camera* camera,
	const RendererOptions& rendererOptions,
	ConstantBufferView* sceneUniformBuffer,
	GPUScene* gpuScene,
	GPUCulling* gpuCulling,
	Texture* RT_sceneColor,
	Texture* RT_thinGBufferA)
{
	// #todo-renderer: Support other topologies
	const EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TRIANGLELIST;

	CHECK(RT_sceneColor->getCreateParams().format == PF_sceneColor);
	CHECK(RT_thinGBufferA->getCreateParams().format == PF_thinGBufferA);

	// Resize volatile heaps if needed.
	{
		uint32 materialCBVCount, materialSRVCount;
		gpuScene->queryMaterialDescriptorsCount(swapchainIndex, materialCBVCount, materialSRVCount);

		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += materialCBVCount;
		requiredVolatiles += materialSRVCount;

		if (requiredVolatiles > totalVolatileDescriptor[swapchainIndex])
		{
			resizeVolatileHeaps(swapchainIndex, requiredVolatiles);
		}
	}

	// Resize indirect argument buffers and their generator.
	{
		const uint32 maxElements = gpuScene->getGPUSceneItemMaxCount();

		if (argumentBufferGenerator->getMaxCommandCount() < maxElements)
		{
			argumentBufferGenerator->resizeMaxCommandCount(maxElements);
		}

		uint32 requiredCapacity = argumentBufferGenerator->getCommandByteStride() * maxElements;
		Buffer* argBuffer = argumentBuffer.at(swapchainIndex);
		Buffer* culledArgBuffer = culledArgumentBuffer.at(swapchainIndex);

		if (argBuffer == nullptr || argBuffer->getCreateParams().sizeInBytes < requiredCapacity)
		{
			argumentBuffer[swapchainIndex] = UniquePtr<Buffer>(gRenderDevice->createBuffer(
				BufferCreateParams{
					.sizeInBytes = requiredCapacity,
					.alignment   = 0,
					.accessFlags = EBufferAccessFlags::COPY_SRC | EBufferAccessFlags::UAV,
				}
			));

			wchar_t debugName[256];
			swprintf_s(debugName, L"Buffer_IndirectDrawBuffer_%u", swapchainIndex);
			argumentBuffer[swapchainIndex]->setDebugName(debugName);

			ShaderResourceViewDesc srvDesc{};
			srvDesc.format                     = EPixelFormat::UNKNOWN;
			srvDesc.viewDimension              = ESRVDimension::Buffer;
			srvDesc.buffer.firstElement        = 0;
			srvDesc.buffer.numElements         = maxElements;
			srvDesc.buffer.structureByteStride = argumentBufferGenerator->getCommandByteStride();
			srvDesc.buffer.flags               = EBufferSRVFlags::None;

			argumentBufferSRV[swapchainIndex] = UniquePtr<ShaderResourceView>(
				gRenderDevice->createSRV(argumentBuffer.at(swapchainIndex), srvDesc));
		}
		if (culledArgBuffer == nullptr || culledArgBuffer->getCreateParams().sizeInBytes < requiredCapacity)
		{
			culledArgumentBuffer[swapchainIndex] = UniquePtr<Buffer>(gRenderDevice->createBuffer(
				BufferCreateParams{
					.sizeInBytes = requiredCapacity,
					.alignment   = 0,
					.accessFlags = EBufferAccessFlags::UAV
				}
			));

			wchar_t debugName[256];
			swprintf_s(debugName, L"Buffer_CulledIndirectDrawBuffer_%u", swapchainIndex);
			culledArgumentBuffer[swapchainIndex]->setDebugName(debugName);

			UnorderedAccessViewDesc uavDesc{};
			uavDesc.format                      = EPixelFormat::UNKNOWN;
			uavDesc.viewDimension               = EUAVDimension::Buffer;
			uavDesc.buffer.firstElement         = 0;
			uavDesc.buffer.numElements          = maxElements;
			uavDesc.buffer.structureByteStride  = argumentBufferGenerator->getCommandByteStride();
			uavDesc.buffer.counterOffsetInBytes = 0;
			uavDesc.buffer.flags                = EBufferUAVFlags::None;

			culledArgumentBufferUAV[swapchainIndex] = UniquePtr<UnorderedAccessView>(
				gRenderDevice->createUAV(culledArgumentBuffer.at(swapchainIndex), uavDesc));
		}
	}

	// #todo-lod: LOD selection
	const uint32 LOD = 0;

	// Fill the indirect draw buffer and perform GPU culling.
	uint32 maxIndirectDraws = 0;
	if (rendererOptions.bEnableIndirectDraw)
	{
		uint32 indirectCommandID = 0;
		for (const StaticMesh* mesh : scene->staticMeshes)
		{
			for (const StaticMeshSection& section : mesh->getSections(LOD))
			{
				VertexBuffer* positionBuffer = section.positionBuffer->getGPUResource().get();
				VertexBuffer* nonPositionBuffer = section.nonPositionBuffer->getGPUResource().get();
				IndexBuffer* indexBuffer = section.indexBuffer->getGPUResource().get();

				argumentBufferGenerator->beginCommand(indirectCommandID);

				argumentBufferGenerator->writeConstant32(indirectCommandID);
				argumentBufferGenerator->writeVertexBufferView(positionBuffer);
				argumentBufferGenerator->writeVertexBufferView(nonPositionBuffer);
				argumentBufferGenerator->writeIndexBufferView(indexBuffer);
				argumentBufferGenerator->writeDrawIndexedArguments(indexBuffer->getIndexCount(), 1, 0, 0, 0);

				argumentBufferGenerator->endCommand();

				++indirectCommandID;
			}
		}

		maxIndirectDraws = indirectCommandID;
		Buffer* currentArgumentBuffer = argumentBuffer.at(swapchainIndex);
		argumentBufferGenerator->copyToBuffer(commandList, maxIndirectDraws, currentArgumentBuffer, 0);

		if (rendererOptions.bEnableGPUCulling)
		{
			gpuCulling->cullDrawCommands(
				commandList, swapchainIndex, sceneUniformBuffer, camera, gpuScene,
				maxIndirectDraws, currentArgumentBuffer, argumentBufferSRV.at(swapchainIndex),
				culledArgumentBuffer.at(swapchainIndex), culledArgumentBufferUAV.at(swapchainIndex),
				drawCounterBuffer.at(swapchainIndex), drawCounterBufferUAV.at(swapchainIndex));
		}
	}

	
	commandList->setGraphicsPipelineState(pipelineState.get());
	commandList->iaSetPrimitiveTopology(primitiveTopology);

	// Bind shader parameters except for root constants.
	{
		GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.constantBuffer("materials", gpuSceneDesc.cbvHeap, 0, gpuSceneDesc.cbvCount);
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
	}
	
	if (rendererOptions.bEnableIndirectDraw)
	{
		if (rendererOptions.bEnableGPUCulling)
		{
			Buffer* argBuffer = culledArgumentBuffer.at(swapchainIndex);
			Buffer* counterBuffer = drawCounterBuffer.at(swapchainIndex);
			commandList->executeIndirect(commandSignature.get(), maxIndirectDraws, argBuffer, 0, counterBuffer, 0);
		}
		else
		{
			Buffer* argBuffer = argumentBuffer.at(swapchainIndex);
			commandList->executeIndirect(commandSignature.get(), maxIndirectDraws, argBuffer, 0, nullptr, 0);
		}
	}
	else
	{
		uint32 payloadID = 0;
		for (const StaticMesh* mesh : scene->staticMeshes)
		{
			for (const StaticMeshSection& section : mesh->getSections(LOD))
			{
				ShaderParameterTable SPT{};
				SPT.pushConstant("pushConstants", payloadID);
				commandList->updateGraphicsRootConstants(pipelineState.get(), &SPT);

				VertexBuffer* vertexBuffers[] = {
					section.positionBuffer->getGPUResource().get(),
					section.nonPositionBuffer->getGPUResource().get()
				};
				auto indexBuffer = section.indexBuffer->getGPUResource().get();

				commandList->iaSetVertexBuffers(0, 2, vertexBuffers);
				commandList->iaSetIndexBuffer(indexBuffer);
				commandList->drawIndexedInstanced(indexBuffer->getIndexCount(), 1, 0, 0, 0);

				++payloadID;
			}
		}
	}
}

void BasePass::resizeVolatileHeaps(uint32 swapchainIndex, uint32 maxDescriptors)
{
	totalVolatileDescriptor[swapchainIndex] = maxDescriptors;

	volatileViewHeap[swapchainIndex] = UniquePtr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(
		DescriptorHeapDesc{
			.type           = EDescriptorHeapType::CBV_SRV_UAV,
			.numDescriptors = maxDescriptors,
			.flags          = EDescriptorHeapFlags::ShaderVisible,
			.nodeMask       = 0,
		}
	));

	wchar_t debugName[256];
	swprintf_s(debugName, L"BasePass_VolatileViewHeap_%u", swapchainIndex);
	volatileViewHeap[swapchainIndex]->setDebugName(debugName);

	CYLOG(LogBasePass, Log, L"Resize volatile heap [%u]: %u descriptors", swapchainIndex, maxDescriptors);
}
