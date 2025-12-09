#include "decode_vis_buffer_pass.h"
#include "gpu_scene.h"
#include "rhi/render_device.h"
#include "rhi/swap_chain.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/barrier_tracker.h"

void DecodeVisBufferPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;
	const uint32 swapchainCount = device->getSwapChain()->getBufferCount();

	decodePassDescriptor.initialize(L"DecodeVisBufferPass", swapchainCount, 0);

	ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, "DecodeVisBufferCS");
	shader->declarePushConstants({ { "pushConstants", 1 } });
	shader->loadFromFile(L"decode_vis_buffer.hlsl", "mainCS");

	decodePipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
		ComputePipelineDesc{ .cs = shader, .nodeMask = 0, }
	));

	delete shader;
}

void DecodeVisBufferPass::decodeVisBuffer(
	RenderCommandList* commandList,
	uint32 swapchainIndex,
	const DecodeVisBufferPassInput& passInput)
{
	TextureBarrierAuto textureBarriers[] = {
		{
			EBarrierSync::DEPTH_STENCIL, EBarrierAccess::DEPTH_STENCIL_READ, EBarrierLayout::DepthStencilRead,
			passInput.sceneDepthTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, EBarrierLayout::ShaderResource,
			passInput.visBufferTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		},
		{
			EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, EBarrierLayout::UnorderedAccess,
			passInput.barycentricTexture, BarrierSubresourceRange::allMips(), ETextureBarrierFlags::None
		},
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	const uint32 packedSize = Cymath::packUint16x2(passInput.textureWidth, passInput.textureHeight);

	ShaderParameterTable SPT{};
	SPT.pushConstant("pushConstants", packedSize);
	SPT.constantBuffer("sceneUniform", passInput.sceneUniformBuffer);
	SPT.byteAddressBuffer("gIndexBuffer", gIndexBufferPool->getByteAddressBufferView());
	SPT.byteAddressBuffer("gVertexBuffer", gVertexBufferPool->getByteAddressBufferView());
	SPT.structuredBuffer("gpuSceneBuffer", passInput.gpuScene->getGPUSceneBufferSRV());
	SPT.texture("sceneDepthTexture", passInput.sceneDepthSRV);
	SPT.texture("visBufferTexture", passInput.visBufferSRV);
	SPT.rwTexture("rwOutputTexture", passInput.barycentricUAV);

	uint32 requiredVolatiles = SPT.totalDescriptors();
	decodePassDescriptor.resizeDescriptorHeap(swapchainIndex, requiredVolatiles);

	commandList->setComputePipelineState(decodePipeline.get());

	DescriptorHeap* volatileHeap = decodePassDescriptor.getDescriptorHeap(swapchainIndex);
	commandList->bindComputeShaderParameters(decodePipeline.get(), &SPT, volatileHeap);

	uint32 dispatchX = (passInput.textureWidth + 7) / 8;
	uint32 dispatchY = (passInput.textureHeight + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);

	// #wip: Global barrier if needed
	//GlobalBarrier globalBarrier{
	//	EBarrierSync::COMPUTE_SHADING, EBarrierSync::COMPUTE_SHADING,
	//	EBarrierAccess::UNORDERED_ACCESS, EBarrierAccess::UNORDERED_ACCESS
	//};
	//commandList->barrier(0, nullptr, 0, nullptr, 1, &globalBarrier);
}
