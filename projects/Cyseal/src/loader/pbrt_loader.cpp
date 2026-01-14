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

#if 0
	std::fstream fs(wFilepath);
	if (!fs)
	{
		CYLOG(LogPBRT, Error, L"Can't open file: %s", filepath.c_str());
		return nullptr;
	}

	pbrt::PBRT4Scanner scanner;
	scanner.scanTokens(fs);
#else
	std::vector<std::string> fileContents;
	bool bFilesValid = pbrt::readFileRecursive(wFilepath.c_str(), fileContents);
	if (!bFilesValid)
	{
		CYLOG(LogPBRT, Error, L"Can't open file (or its includes): %s", filepath.c_str());
		return nullptr;
	}
	pbrt::PBRT4Scanner scanner;
	scanner.scanTokens(fileContents);
#endif

	pbrt::PBRT4Parser pbrtParser;
	pbrt::PBRT4ParserOutput parserOutput = pbrtParser.parse(&scanner);
	CHECK(parserOutput.bValid);

	PBRT4Scene* pbrtScene = new PBRT4Scene;

	loadTextureFiles(baseDir, parserOutput);
	loadMaterials(parserOutput);
	pbrtScene->triangleMeshes = std::move(parserOutput.triangleShapeDescs);
	loadPLYMeshes(baseDir, parserOutput.plyShapeDescs, pbrtScene->plyMeshes);
	loadObjects(baseDir, parserOutput, pbrtScene->objectInstances);
	
	return pbrtScene;
}

MaterialAsset* PBRT4Loader::findNamedMaterial(const char* name) const
{
	auto it = namedMaterialDatabase.find(name);
	return it == namedMaterialDatabase.end() ? nullptr : it->second.get();
}

void PBRT4Loader::loadTextureFiles(const std::wstring& baseDir, const pbrt::PBRT4ParserOutput& parserOutput)
{
	ImageLoader imageLoader;

	textureAssetDatabase.clear();
	textureDirectiveDatabase.clear();

	// Load image files used by Texture directives.
	for (const std::wstring& wFilename : parserOutput.textureFileDescSet)
	{
		ImageLoadData* imageBlob = nullptr;

		std::wstring textureFilepath = baseDir + wFilename;
		textureFilepath = ResourceFinder::get().find(textureFilepath);
		if (textureFilepath.size() > 0)
		{
			imageBlob = imageLoader.load(textureFilepath);
		}
		else
		{
			CYLOG(LogPBRT, Error, L"Failed to open: %s", wFilename.c_str());
		}

		SharedPtr<TextureAsset> textureAsset;
		if (imageBlob == nullptr)
		{
			textureAsset = gTextureManager->getSystemTextureGrey2D();
		}
		else
		{
			textureAsset = makeShared<TextureAsset>();

			ENQUEUE_RENDER_COMMAND(CreateTextureAsset)(
				[texWeak = WeakPtr<TextureAsset>(textureAsset), imageBlob, wFilename](RenderCommandList& commandList)
				{
					SharedPtr<TextureAsset> texShared = texWeak.lock();
					if (texShared == nullptr)
					{
						// #todo-pbrt-parser: Maybe don't load unused texture files in the first place?
						// Texture file was declared in a pbrt file, but actually not used by any material,
						// so related TextureAsset was already deallocated.
						CYLOG(LogPBRT, Error, L"TextureAsset is already deallocated for: %s", wFilename.c_str());
						return;
					}

					TextureCreateParams createParams = TextureCreateParams::texture2D(
						EPixelFormat::R8G8B8A8_UNORM,
						ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
						imageBlob->width, imageBlob->height);
					
					Texture* texture = gRenderDevice->createTexture(createParams);
					texture->uploadData(commandList,
						imageBlob->buffer,
						imageBlob->getRowPitch(),
						imageBlob->getSlicePitch());
					texture->setDebugName(wFilename.c_str());

					texShared->setGPUResource(SharedPtr<Texture>(texture));

					commandList.enqueueDeferredDealloc(imageBlob);
				}
			);
		}

		textureAssetDatabase.insert({ wFilename, textureAsset });
	}

	// Map Texture directive to image file.
	// #todo-pbrt-parser: Parse parameters in PBRT4Parser::compileTexture() and somehow apply them.
	for (const auto& desc : parserOutput.textureDescs)
	{
		auto it = textureAssetDatabase.find(desc.filename);
		if (it != textureAssetDatabase.end())
		{
			textureDirectiveDatabase.insert({ desc.textureName, it->second });
		}
		else
		{
			CYLOG(LogPBRT, Error, L"Texture filename not found: %s", desc.filename.c_str());
		}
	}
}

void PBRT4Loader::loadMaterials(const pbrt::PBRT4ParserOutput& parserOutput)
{
	namedMaterialDatabase.clear();
	unnamedMaterialDatabase.clear();

	// Create material assets
	auto createMaterialAsset = [&](const pbrt::PBRT4ParserOutput::MaterialDesc& desc, size_t ix) {
		auto material = makeShared<MaterialAsset>();

		const std::string debugName = desc.materialName.isUnnamed() ? "__unnamed" + std::to_string(ix) : desc.materialName.name;

		material->albedoMultiplier.x = desc.rgbReflectance.x;
		material->albedoMultiplier.y = desc.rgbReflectance.y;
		material->albedoMultiplier.z = desc.rgbReflectance.z;
		if (desc.textureReflectance.size() > 0)
		{
			auto it = textureDirectiveDatabase.find(desc.textureReflectance);
			if (it != textureDirectiveDatabase.end())
			{
				material->albedoTexture = it->second;
			}
			else
			{
				CYLOG(LogPBRT, Error, L"Material '%S' uses textureReflectance '%S' but couldn't find it",
					debugName.c_str(), desc.textureReflectance.c_str());
			}
		}
		if (material->albedoTexture == nullptr)
		{
			material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
		}
		
		if (desc.bUseAnisotropicRoughness)
		{
			material->roughness = 0.5f * (desc.uroughness + desc.vroughness);
			CYLOG(LogPBRT, Error, L"Material '%S' uses anisotropic roughness but not supported", debugName.c_str());
		}
		else
		{
			material->roughness = desc.roughness;
		}

		// #todo-pbrt: diffusetransmission material needs different materialID
		//if (desc.bTransmissive)
		//{
		//	material->materialID = EMaterialId::Transparent;
		//	material->transmittance = desc.rgbTransmittance; (need to support texureTransmittance also)
		//}

		// #todo-pbrt: Other MaterialDesc properties

		if (desc.materialName.isUnnamed())
		{
			unnamedMaterialDatabase.push_back(material);
		}
		else
		{
			namedMaterialDatabase.insert(std::make_pair(desc.materialName.name, material));
		}
	};

	for (size_t i = 0; i < parserOutput.namedMaterialDescs.size(); ++i)
	{
		createMaterialAsset(parserOutput.namedMaterialDescs[i], i);
	}
	for (size_t i = 0; i < parserOutput.unnamedMaterialDescs.size(); ++i)
	{
		createMaterialAsset(parserOutput.unnamedMaterialDescs[i], i);
	}
}

void PBRT4Loader::loadPLYMeshes(const std::wstring& baseDir, const std::vector<pbrt::PBRT4ParserOutput::PLYShapeDesc>& descs, std::vector<PLYMesh*>& outMeshes)
{
	PLYLoader plyLoader;
	for (const pbrt::PBRT4ParserOutput::PLYShapeDesc& desc : descs)
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
				plyMesh->material = findMaterialByRef(desc.materialName);
				if (!desc.bIdentityTransform)
				{
					plyMesh->applyTransform(desc.transform);
				}
				outMeshes.push_back(plyMesh);
			}
		}
	}
}

void PBRT4Loader::loadObjects(const std::wstring& baseDir, pbrt::PBRT4ParserOutput& parserOutput, std::vector<PBRT4ObjectInstances>& outInstances)
{
	std::map<std::string, PBRT4ObjectInstances> objectInstancesMap;
	for (pbrt::PBRT4ParserOutput::ObjectDeclDesc& desc : parserOutput.objectDeclDescs)
	{
		PBRT4ObjectInstances obj;
		obj.objectName = desc.name;
		obj.triangleMeshes = std::move(desc.triangleShapeDescs);
		loadPLYMeshes(baseDir, desc.plyShapeDescs, obj.plyMeshes);

		objectInstancesMap.insert({ desc.name, std::move(obj) });
	}
	for (pbrt::PBRT4ParserOutput::ObjectInstanceDesc& desc : parserOutput.objectInstanceDescs)
	{
		auto it = objectInstancesMap.find(desc.name);
		if (it != objectInstancesMap.end())
		{
			it->second.instanceTransforms.push_back(desc.instanceTransform);
		}
		else
		{
			CYLOG(LogPBRT, Error, L"Can't find object decl: %S", desc.name.data());
		}
	}
	for (const auto& it : objectInstancesMap)
	{
		outInstances.push_back(it.second);
	}
}

SharedPtr<MaterialAsset> PBRT4Loader::findMaterialByRef(const pbrt::PBRT4MaterialRef& ref) const
{
	if (ref.isUnnamed())
	{
		CHECK(0 <= ref.unnamedId && ref.unnamedId < unnamedMaterialDatabase.size());
		return unnamedMaterialDatabase[ref.unnamedId];
	}
	auto it = namedMaterialDatabase.find(ref.name);
	return it == namedMaterialDatabase.end() ? nullptr : it->second;
}
