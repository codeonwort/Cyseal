#include "pbrt_loader.h"
#include "ply_loader.h"
#include "image_loader.h"

#include "core/assertion.h"
#include "core/smart_pointer.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "rhi/gpu_resource.h"
#include "rhi/texture_manager.h"
#include "render/material.h"
#include "world/gpu_resource_asset.h"
#include "util/resource_finder.h"
#include "util/string_conversion.h"
#include "util/logging.h"

#include <vector>
#include <fstream>
#include <filesystem>

DEFINE_LOG_CATEGORY_STATIC(LogPBRT);

// -------------------------------------
// PBRT4Scene

PBRT4Scene::~PBRT4Scene()
{
	deallocate();
}

void PBRT4Scene::deallocate()
{
	for (PLYMesh* mesh : plyMeshes)
	{
		delete mesh;
	}
	plyMeshes.clear();
}

// #todo-pbrt: Parser impl roadmap
// 1. Parse geometries and render something.
// 2. Parse light sources and lit the geometries.
// 3. Parse materials and apply to geometries.
PBRT4Scene* PBRT4Loader::loadFromFile(const std::wstring& filepath)
{
	std::wstring wFilepath = ResourceFinder::get().find(filepath);
	if (wFilepath.size() == 0)
	{
		CYLOG(LogPBRT, Error, L"Can't find file: %s", filepath.c_str());
		return nullptr;
	}
	std::filesystem::path baseDirPath = filepath;
	std::wstring baseDir = baseDirPath.parent_path().wstring() + L"/";
	std::fstream fs(wFilepath);
	if (!fs)
	{
		CYLOG(LogPBRT, Error, L"Can't open file: %s", filepath.c_str());
		return nullptr;
	}

	pbrt::PBRT4Scanner scanner;
	scanner.scanTokens(fs);

	pbrt::PBRT4Parser pbrtParser;
	pbrt::PBRT4ParserOutput parserOutput = pbrtParser.parse(&scanner);

	PBRT4Scene* pbrtScene = new PBRT4Scene;

	std::map<std::string, SharedPtr<TextureAsset>> textureAssetDatabase;
	std::map<std::string, SharedPtr<MaterialAsset>> materialDatabase;

	ImageLoader imageLoader;
	for (const auto& desc : parserOutput.textureFileDescs)
	{
		ImageLoadData* imageBlob = nullptr;

		std::wstring textureFilepath = baseDir + desc.filename;
		textureFilepath = ResourceFinder::get().find(textureFilepath);
		if (textureFilepath.size() > 0)
		{
			imageBlob = imageLoader.load(textureFilepath);
		}

		SharedPtr<TextureAsset> textureAsset;
		if (imageBlob == nullptr)
		{
			textureAsset = gTextureManager->getSystemTextureGrey2D();
		}
		else
		{
			textureAsset = makeShared<TextureAsset>();

			std::wstring wTextureName;
			str_to_wstr(desc.textureName, wTextureName);

			ENQUEUE_RENDER_COMMAND(CreateTextureAsset)(
				[textureAsset, imageBlob, wTextureName](RenderCommandList& commandList)
				{
					TextureCreateParams createParams = TextureCreateParams::texture2D(
						EPixelFormat::R8G8B8A8_UNORM,
						ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
						imageBlob->width, imageBlob->height);
					
					Texture* texture = gRenderDevice->createTexture(createParams);
					texture->uploadData(commandList,
						imageBlob->buffer,
						imageBlob->getRowPitch(),
						imageBlob->getSlicePitch());
					texture->setDebugName(wTextureName.c_str());

					textureAsset->setGPUResource(SharedPtr<Texture>(texture));

					commandList.enqueueDeferredDealloc(imageBlob);
				}
			);
		}

		//imageDatabase.insert(std::make_pair(desc.textureName, imageBlob));
		textureAssetDatabase.insert(std::make_pair(desc.textureName, textureAsset));
	}

	for (const auto& desc : parserOutput.namedMaterialDescs)
	{
		auto material = makeShared<MaterialAsset>();

		material->albedoMultiplier.x = desc.rgbReflectance.x;
		material->albedoMultiplier.y = desc.rgbReflectance.y;
		material->albedoMultiplier.z = desc.rgbReflectance.z;
		if (desc.textureReflectance.size() > 0)
		{
			auto it = textureAssetDatabase.find(desc.textureReflectance);
			if (it != textureAssetDatabase.end())
			{
				material->albedoTexture = it->second;
			}
			else
			{
				CYLOG(LogPBRT, Error, L"Material '%S' uses textureReflectance '%S' but couldn't find it",
					desc.materialName.c_str(), desc.textureReflectance.c_str());
			}
		}
		if (material->albedoTexture == nullptr)
		{
			material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
		}
		
		if (desc.bUseAnisotropicRoughness)
		{
			material->roughness = 0.5f * (desc.uroughness + desc.vroughness);
			CYLOG(LogPBRT, Error, L"Material '%S' uses anisotropic roughness but not supported", desc.materialName.c_str());
		}
		else
		{
			material->roughness = desc.roughness;
		}

		// #todo-material: Parse metallic

		// #todo-pbrt: Other NamedMaterialDesc properties

		materialDatabase.insert(std::make_pair(desc.materialName, material));
	}

	pbrtScene->triangleMeshes = std::move(parserOutput.triangleShapeDescs);

	PLYLoader plyLoader;
	for (const auto& desc : parserOutput.plyShapeDescs)
	{
		std::wstring plyFilepath = baseDir + desc.filename;
		std::wstring plyFullpath = ResourceFinder::get().find(plyFilepath);
		if (plyFullpath.size() == 0)
		{
			CYLOG(LogPBRT, Error, L"Can't find file: %s", plyFilepath.c_str());
		}
		else
		{
			PLYMesh* plyMesh = plyLoader.loadFromFile(plyFullpath);
			if (plyMesh == nullptr)
			{
				CYLOG(LogPBRT, Error, L"Can't parse PLY file: %s", plyFullpath.c_str());
			}
			else
			{
				auto it = materialDatabase.find(desc.namedMaterial);
				if (it != materialDatabase.end())
				{
					plyMesh->material = it->second;
				}
				if (!desc.bIdentityTransform)
				{
					plyMesh->applyTransform(desc.transform);
				}
				pbrtScene->plyMeshes.push_back(plyMesh);
			}
		}
	}
	
	return pbrtScene;
}
