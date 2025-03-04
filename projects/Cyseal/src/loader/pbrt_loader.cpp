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

#define TOKEN_LOOKAT "LookAt"
#define TOKEN_CAMERA "Camera"
#define TOKEN_SAMPLER "Sampler"
#define TOKEN_INTEGRATOR "Integrator"
#define TOKEN_FILM "Film"

#define TOKEN_WORLDBEGIN "WorldBegin"

#define TOKEN_LIGHTSOURCE "LightSource"
#define TOKEN_ATTRIBUTEBEGIN "AttributeBegin"
#define TOKEN_ATTRIBUTEEND "AttributeEnd"
#define TOKEN_MATERIAL "Material"
#define TOKEN_NAMEDMATERIAL "NamedMaterial"
#define TOKEN_MAKENAMEDMATERIAL "MakeNamedMaterial"
#define TOKEN_SHAPE "Shape"
#define TOKEN_TEXTURE "Texture"
#define TOKEN_TRANSLATE "Translate"
#define TOKEN_TRANSFORM "Transform"
#define TOKEN_AREALIGHTSOURCE "AreaLightSource"

DEFINE_LOG_CATEGORY_STATIC(LogPBRT);

enum class PBRT4ParsePhase
{
	RenderingOptions = 0,
	SceneElements = 1,
	InsideAttribute = 2,
};

struct TextureFileDesc
{
	std::string textureName;
	std::string textureFilter; // #todo-pbrt
	std::wstring filename;
};

struct NamedMaterialDesc
{
	std::string materialName;
	std::string materialType;

	bool bUseRgbReflectance = false;
	vec3 rgbReflectance = vec3(1.0f);
	std::string textureReflectance;

	bool bUseAnisotropicRoughness = false;
	bool bRemapRoughness = false;
	float roughness = 1.0f;
	float vroughness = 1.0f;
	float uroughness = 1.0f;

	bool bUseRgbEtaAndK = false;
	vec3 rgbEta, rgbK;
	std::string spectrumEta, spectrumK;
};

struct PLYShapeDesc
{
	std::wstring filename;
	std::string namedMaterial;
	Matrix transform;
	bool bIdentityTransform;
};

std::istream& operator>>(std::istream& stream, vec3& v)
{
	stream >> v.x >> v.y >> v.z;
	return stream;
}

std::string readQuoteWord(std::istream& stream)
{
	std::string str;
	stream >> str;
	return str.substr(1, str.size() - 2);
}
std::string readQuoteTwoWords(std::istream& stream)
{
	std::string word1, word2;
	stream >> word1 >> word2;
	return word1.substr(1) + " " + word2.substr(0, word2.size() - 1);
}
std::string readBracketQuoteWord(std::istream& stream)
{
	char ch;
	stream >> ch;
	std::string str = readQuoteWord(stream);
	stream >> ch;
	return str;
}
float readBracketFloat(std::istream& stream)
{
	char ch;
	float x;
	stream >> ch >> x >> ch;
	return x;
}
vec3 readBracketVec3(std::istream& stream)
{
	char ch;
	vec3 v;
	stream >> ch >> v.x >> v.y >> v.z >> ch;
	return v;
}
// If next input starts with ", parse the words enclosed.
// Otherwise, parse an ordinary string.
//     MakeNamedMaterial => MakeNamedMaterial
//     "string type"     => string type
std::string readMaybeQuoteWords(std::istream& stream)
{
	std::string str;
	char ch;
	stream >> ch;
	stream.unsetf(std::ios::skipws);
	if (ch == '"')
	{
		while (true)
		{
			stream >> ch;
			if (ch == '"') break;
			str += ch;
		}
	}
	else
	{
		stream >> str;
		str = ch + str;
	}
	stream.setf(std::ios::skipws);
	return str;
}
Matrix readBracketMatrix(std::istream& stream)
{
	char ch;
	Matrix mat;
	stream >> ch;
	for (uint32 i = 0; i < 16; ++i)
	{
		stream >> mat.m[i / 4][i % 4];
	}
	stream >> ch;
	return mat;
}
// There might be some smart way but I'm too lazy
void readBracketFloatArray(std::istream& stream, std::vector<float>& outArray)
{
	char ch;
	stream >> ch;

	std::string str;
	while (true)
	{
		stream >> str;
		if (str == "]")
		{
			break;
		}
		std::stringstream ss(str);
		float x;
		ss >> x;
		outArray.push_back(x);
	}
}
void readBracketVec2Array(std::istream& stream, std::vector<vec2>& outArray)
{
	char ch;
	stream >> ch;

	std::string str;
	while (true)
	{
		stream >> str;
		if (str == "]")
		{
			break;
		}
		std::stringstream ss(str);
		vec2 v;
		ss >> v.x;
		stream >> v.y;
		outArray.push_back(v);
	}
}
void readBracketVec3Array(std::istream& stream, std::vector<vec3>& outArray)
{
	char ch;
	stream >> ch;

	std::string str;
	while (true)
	{
		stream >> str;
		if (str == "]")
		{
			break;
		}
		std::stringstream ss(str);
		vec3 v;
		ss >> v.x;
		stream >> v.y >> v.z;
		outArray.push_back(v);
	}
}
void readBracketUint32Array(std::istream& stream, std::vector<uint32>& outArray)
{
	char ch;
	stream >> ch;

	std::string str;
	while (true)
	{
		stream >> str;
		if (str == "]")
		{
			break;
		}
		std::stringstream ss(str);
		uint32 i;
		ss >> i;
		outArray.push_back(i);
	}
}

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

	PBRT4Scene* pbrtScene = new PBRT4Scene;
	PBRT4ParsePhase parsePhase = PBRT4ParsePhase::RenderingOptions;

	// ------------------------------------------------
	// Steps
	// 1. Rendering options
	// 2. WorldBegin
	// 3. Lights, geometries, and volumes

	Matrix sceneTransform;
	std::vector<TextureFileDesc> textureFileDescs;
	std::vector<NamedMaterialDesc> namedMaterialDescs;
	std::vector<PBRT4TriangleMesh> triangleShapeDescs;
	std::vector<PLYShapeDesc> plyShapeDescs;

	// State machine
	std::string queuedToken;
	std::string currentNamedMaterial;
	Matrix currentTransform;
	vec3 currentEmission = vec3(0.0f);
	bool bCurrentTransformIdentity = true;
	bool bValidFormat = true;

	auto enqueueToken = [&queuedToken](const std::string& tok)
	{
		CHECK(queuedToken.size() == 0);
		queuedToken = tok;
	};

	while (!fs.eof())
	{
		std::string token;
		if (queuedToken.size() > 0)
		{
			token = queuedToken;
			queuedToken = "";
		}
		else
		{
			fs >> token;
		}
		
		if (token == TOKEN_LOOKAT)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				fs >> pbrtScene->eyePosition;
				fs >> pbrtScene->lookAtPosition;
				fs >> pbrtScene->upVector;
			}
			else
			{
				bValidFormat = false;
				break;
			}
		}
		else if (token == TOKEN_CAMERA)
		{
			// #todo-pbrt: Parse token Camera
			int z = 0;
		}
		else if (token == TOKEN_SAMPLER)
		{
			// #todo-pbrt: Parse token Sampler
		}
		else if (token == TOKEN_INTEGRATOR)
		{
			// #todo-pbrt: Parse token Integrator
		}
		else if (token == TOKEN_FILM)
		{
			// #todo-pbrt: Parse token Film
		}
		else if (token == TOKEN_WORLDBEGIN)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				parsePhase = PBRT4ParsePhase::SceneElements;
			}
			else
			{
				bValidFormat = false;
				break;
			}
		}
		else if (token == TOKEN_TEXTURE)
		{
			std::string textureName = readQuoteWord(fs);
			std::string textureType = readQuoteWord(fs);
			if (textureType == "spectrum")
			{
				std::string textureClass = readQuoteWord(fs);
				if (textureClass == "imagemap")
				{
					std::string textureFilter, textureFilename;
					for (uint32 i = 0; i < 2; ++i)
					{
						std::string paramName = readQuoteTwoWords(fs);
						if (paramName == "string filter")
						{
							// #todo-pbrt: texture filter
							textureFilter = readQuoteWord(fs);
						}
						else if (paramName == "string filename")
						{
							textureFilename = readBracketQuoteWord(fs);
						}
					}
					std::wstring wTextureFilename;
					str_to_wstr(textureFilename, wTextureFilename);
					TextureFileDesc desc{
						.textureName = textureName,
						.textureFilter = textureFilter,
						.filename = wTextureFilename
					};
					textureFileDescs.emplace_back(desc);
				}
				else
				{
					bValidFormat = false;
					CYLOG(LogPBRT, Error, L"Unknown texture class: %S", textureClass.c_str());
				}
			}
			else if (textureType == "float")
			{
				// #todo-pbrt
				bValidFormat = false;
				CYLOG(LogPBRT, Error, L"Unhandled texture type: %S", textureType.c_str());
			}
			else
			{
				bValidFormat = false;
				CYLOG(LogPBRT, Error, L"Texture type can be only spectrum or float: %S", textureType.c_str());
			}
		}
		else if (token == TOKEN_MAKENAMEDMATERIAL)
		{
			NamedMaterialDesc materialDesc{};
			materialDesc.materialName = readQuoteWord(fs);
			while (true)
			{
				std::string maybeParam = readMaybeQuoteWords(fs);
				if (maybeParam == "string type")
				{
					materialDesc.materialType = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "rgb reflectance")
				{
					materialDesc.rgbReflectance = readBracketVec3(fs);
					materialDesc.bUseRgbReflectance = true;
				}
				else if (maybeParam == "texture reflectance")
				{
					materialDesc.textureReflectance = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "bool remaproughness")
				{
					materialDesc.bRemapRoughness = readBracketQuoteWord(fs) == "true";
				}
				else if (maybeParam == "float roughness")
				{
					materialDesc.roughness = readBracketFloat(fs);
					materialDesc.bUseAnisotropicRoughness = false;
				}
				else if (maybeParam == "float vroughness")
				{
					materialDesc.vroughness = readBracketFloat(fs);
					materialDesc.bUseAnisotropicRoughness = true;
				}
				else if (maybeParam == "float uroughness")
				{
					materialDesc.uroughness = readBracketFloat(fs);
					materialDesc.bUseAnisotropicRoughness = true;
				}
				else if (maybeParam == "spectrum eta")
				{
					materialDesc.spectrumEta = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "spectrum k")
				{
					materialDesc.spectrumK = readBracketQuoteWord(fs);
				}
				else if (maybeParam == "rgb eta")
				{
					materialDesc.rgbEta = readBracketVec3(fs);
					materialDesc.bUseRgbEtaAndK = true;
				}
				else if (maybeParam == "rgb k")
				{
					materialDesc.rgbK = readBracketVec3(fs);
					materialDesc.bUseRgbEtaAndK = true;
				}
				else
				{
					enqueueToken(maybeParam);
					break;
				}
			}
			namedMaterialDescs.emplace_back(materialDesc);
		}
		else if (token == TOKEN_NAMEDMATERIAL)
		{
			currentNamedMaterial = readQuoteWord(fs);
		}
		else if (token == TOKEN_SHAPE)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				bValidFormat = false;
				break;
			}
			else if (parsePhase == PBRT4ParsePhase::SceneElements || parsePhase == PBRT4ParsePhase::InsideAttribute)
			{
				std::string shapeType = readQuoteWord(fs);
				if (shapeType == "plymesh")
				{
					// "string filename" [ "models/somefile.ply" ]
					std::string plyFilename, dummyS;
					char dummyC;
					fs >> dummyS >> dummyS >> dummyC >> plyFilename >> dummyC;
					plyFilename = plyFilename.substr(1, plyFilename.size() - 2);

					std::wstring wPlyFilename;
					str_to_wstr(plyFilename, wPlyFilename);

					PLYShapeDesc desc{
						.filename = wPlyFilename,
						.namedMaterial = currentNamedMaterial,
						.transform = currentTransform,
						.bIdentityTransform = bCurrentTransformIdentity,
					};
					plyShapeDescs.emplace_back(desc);
				}
				else if (shapeType == "trianglemesh")
				{
					std::vector<vec2> texcoordBuffer;
					std::vector<vec3> normalBuffer;
					std::vector<vec3> positionBuffer;
					std::vector<uint32> indexBuffer;
					while (true)
					{
						std::string maybeParam = readMaybeQuoteWords(fs);
						if (maybeParam == "point2 uv")
						{
							readBracketVec2Array(fs, texcoordBuffer);
						}
						else if (maybeParam == "normal N")
						{
							readBracketVec3Array(fs, normalBuffer);
						}
						else if (maybeParam == "point3 P")
						{
							readBracketVec3Array(fs, positionBuffer);
						}
						else if (maybeParam == "integer indices")
						{
							readBracketUint32Array(fs, indexBuffer);
						}
						else
						{
							enqueueToken(maybeParam);
							break;
						}
					}
					SharedPtr<Material> material = makeShared<Material>();
					material->emission = currentEmission;

					PBRT4TriangleMesh desc{
						.positionBuffer = std::move(positionBuffer),
						.normalBuffer = std::move(normalBuffer),
						.texcoordBuffer = std::move(texcoordBuffer),
						.indexBuffer = std::move(indexBuffer),
						.material = material,
					};
					triangleShapeDescs.emplace_back(desc);
				}
			}
		}
		else if (token == TOKEN_LIGHTSOURCE)
		{
			// #todo-pbrt: Parse token LightSource
			int z = 0;
		}
		else if (token == TOKEN_TRANSFORM)
		{
			if (parsePhase == PBRT4ParsePhase::RenderingOptions)
			{
				sceneTransform = readBracketMatrix(fs);
			}
			else if (parsePhase == PBRT4ParsePhase::InsideAttribute)
			{
				currentTransform = readBracketMatrix(fs);
				bCurrentTransformIdentity = false;
			}
		}
		else if (token == TOKEN_AREALIGHTSOURCE)
		{
			std::string lightType = readMaybeQuoteWords(fs);
			if (lightType == "diffuse")
			{
				std::string lightParam = readMaybeQuoteWords(fs);
				if (lightParam == "rgb L")
				{
					currentEmission = readBracketVec3(fs);
				}
				else
				{
					bValidFormat = false;
					CYLOG(LogPBRT, Error, L"Invalid area light param: %S", lightParam.c_str());
				}
			}
			else
			{
				bValidFormat = false;
				CYLOG(LogPBRT, Error, L"Invalid area light type: %S", lightType.c_str());
			}
		}
		else if (token == TOKEN_ATTRIBUTEBEGIN)
		{
			CHECK(parsePhase == PBRT4ParsePhase::SceneElements);
			// #todo-pbrt: Parse token AttributeBegin
			parsePhase = PBRT4ParsePhase::InsideAttribute;
		}
		else if (token == TOKEN_ATTRIBUTEEND)
		{
			CHECK(parsePhase == PBRT4ParsePhase::InsideAttribute);
			// #todo-pbrt: Parse token AttributeEnd
			parsePhase = PBRT4ParsePhase::SceneElements;
			currentTransform.identity();
			bCurrentTransformIdentity = true;
			currentEmission = vec3(0.0f);
		}
	}

	if (!bValidFormat)
	{
		CYLOG(LogPBRT, Error, L"Failed to parse: %s", filepath.c_str());
		delete pbrtScene;
		return nullptr;
	}

	//std::map<std::string, ImageLoadData*> imageDatabase;
	std::map<std::string, SharedPtr<TextureAsset>> textureAssetDatabase;
	std::map<std::string, SharedPtr<Material>> materialDatabase;

	ImageLoader imageLoader;
	for (const TextureFileDesc& desc : textureFileDescs)
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

	for (const NamedMaterialDesc& desc : namedMaterialDescs)
	{
		auto material = makeShared<Material>();

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

	pbrtScene->triangleMeshes = std::move(triangleShapeDescs);

	PLYLoader plyLoader;
	for (const PLYShapeDesc& desc : plyShapeDescs)
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
