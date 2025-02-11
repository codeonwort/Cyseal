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

void BasePass::initialize(EPixelFormat sceneColorFormat, const EPixelFormat gbufferFormats[], uint32 numGBuffers)
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
	GraphicsPipelineDesc pipelineDesc{
		.vs                     = shaderVS,
		.ps                     = shaderPS,
		.blendDesc              = BlendDesc(),
		.sampleMask             = 0xffffffff,
		.rasterizerDesc         = RasterizerDesc(),
		.depthstencilDesc       = getReverseZPolicy() == EReverseZPolicy::Reverse ? DepthstencilDesc::ReverseZSceneDepth() : DepthstencilDesc::StandardSceneDepth(),
		.inputLayout            = inputLayout,
		.primitiveTopologyType  = EPrimitiveTopologyType::Triangle,
		.numRenderTargets       = 1 + numGBuffers,
		.rtvFormats             = { EPixelFormat::UNKNOWN, }, // Fill later
		.dsvFormat              = swapchain->getBackbufferDepthFormat(),
		.sampleDesc = SampleDesc{
			.count              = swapchain->supports4xMSAA() ? 4u : 1u,
			.quality            = swapchain->supports4xMSAA() ? (swapchain->get4xMSAAQuality() - 1) : 0,
		},
		.staticSamplers         = std::move(staticSamplers),
	};
	pipelineDesc.rtvFormats[0] = sceneColorFormat;
	for (uint32 i = 0; i < numGBuffers; ++i) pipelineDesc.rtvFormats[i + 1] = gbufferFormats[i];
	pipelineState = UniquePtr<GraphicsPipelineState>(device->createGraphicsPipelineState(pipelineDesc));

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

void BasePass::renderBasePass(RenderCommandList* commandList, uint32 swapchainIndex, const BasePassInput& passInput)
{
	auto scene              = passInput.scene;
	auto camera             = passInput.camera;
	auto bIndirectDraw      = passInput.bIndirectDraw;
	auto bGPUCulling        = passInput.bGPUCulling;
	auto sceneUniformBuffer = passInput.sceneUniformBuffer;
	auto gpuScene           = passInput.gpuScene;
	auto gpuCulling         = passInput.gpuCulling;

	if (gpuScene->getGPUSceneItemMaxCount() == 0)
	{
		// #todo-zero-size: Release resources if any.
		return;
	}
	GPUScene::MaterialDescriptorsDesc gpuSceneDesc = gpuScene->queryMaterialDescriptors(swapchainIndex);

	// #todo-renderer: Support other topologies
	const EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TRIANGLELIST;

	// Resize volatile heaps if needed.
	{
		uint32 requiredVolatiles = 0;
		requiredVolatiles += 1; // sceneUniform
		requiredVolatiles += 1; // gpuSceneBuffer
		requiredVolatiles += 1; // gpuSceneDesc.constantsBufferSRV
		requiredVolatiles += gpuSceneDesc.srvCount; // albedoTextures[]

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

	// Fill the indirect draw buffer and perform GPU culling.
	uint32 maxIndirectDraws = 0;
	if (bIndirectDraw)
	{
		uint32 indirectCommandID = 0;
		for (const StaticMesh* mesh : scene->staticMeshes)
		{
			uint32 lod = mesh->getActiveLOD();
			for (const StaticMeshSection& section : mesh->getSections(lod))
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

		if (bGPUCulling)
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
		ShaderParameterTable SPT{};
		SPT.constantBuffer("sceneUniform", sceneUniformBuffer);
		SPT.structuredBuffer("gpuSceneBuffer", gpuScene->getGPUSceneBufferSRV());
		SPT.structuredBuffer("materials", gpuSceneDesc.constantsBufferSRV);
		SPT.texture("albedoTextures", gpuSceneDesc.srvHeap, 0, gpuSceneDesc.srvCount);

		commandList->bindGraphicsShaderParameters(pipelineState.get(), &SPT, volatileViewHeap.at(swapchainIndex));
	}
	
	if (bIndirectDraw)
	{
		if (bGPUCulling)
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
			uint32 lod = mesh->getActiveLOD();
			for (const StaticMeshSection& section : mesh->getSections(lod))
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
