#pragma once

#include "pbrt_scanner.h"
#include "core/matrix.h"
#include "core/vec2.h"
#include "core/smart_pointer.h"

#include <string>
#include <map>
#include <functional>

class MaterialAsset;

namespace pbrt
{
	class PBRT4Scanner;

	enum class PBRT4ParsePhase
	{
		RenderingOptions = 0,
		SceneElements    = 1,
		InsideAttribute  = 2,
	};

	enum class PBRT4ParameterType
	{
		String, Texture, Spectrum, Bool,
		Float3, Float, Float2Array, Float3Array,
		Int, IntArray,
	};

	struct PBRT4Parameter
	{
		PBRT4ParameterType datatype = PBRT4ParameterType::String;
		std::string name;

		// #todo-pbrt-parser: union
		std::string        asString;                 // String, Texture, Spectrum
		bool               asBool = false;           // Bool
		float              asFloat = 0.0f;           // Float
		vec3               asFloat3 = vec3(0.0f);    // Float3
		std::vector<float> asFloatArray;             // Float2Array, Float3Array
		int32              asInt;                    // Int
		std::vector<int32> asIntArray;               // IntArray
	};

	struct PBRT4ParserOutput
	{
		struct TextureFileDesc
		{
			std::string    textureName;
			std::string    textureFilter;
			std::wstring   filename;
		};
		struct NamedMaterialDesc
		{
			std::string    materialName;
			std::string    materialType;
			bool           bUseRgbReflectance       = false;
			vec3           rgbReflectance           = vec3(1.0f);
			std::string    textureReflectance;
			bool           bUseAnisotropicRoughness = false;
			bool           bRemapRoughness          = false;
			float          roughness                = 1.0f;
			float          vroughness               = 1.0f;
			float          uroughness               = 1.0f;
			bool           bUseRgbEtaAndK           = false;
			vec3           rgbEta;
			vec3           rgbK;
			std::string    spectrumEta;
			std::string    spectrumK;
		};
		struct TriangleMeshDesc
		{
			std::vector<vec3>        positionBuffer;
			std::vector<vec3>        normalBuffer;
			std::vector<vec2>        texcoordBuffer;
			std::vector<uint32>      indexBuffer;
			SharedPtr<MaterialAsset> material;
		};
		struct PLYShapeDesc
		{
			std::wstring   filename;
			std::string    namedMaterial;
			Matrix         transform;
			bool           bIdentityTransform;
		};

	public:
		vec3                           eyePosition;
		vec3                           lookAtPosition;
		vec3                           upVector;
		Matrix                         sceneTransform;
		std::vector<TextureFileDesc>   textureFileDescs;
		std::vector<NamedMaterialDesc> namedMaterialDescs;
		std::vector<TriangleMeshDesc>  triangleShapeDescs;
		std::vector<PLYShapeDesc>      plyShapeDescs;
	};

	// Parse tokens.
	// Wanna separate parser and compiler but the file format is kinda state machine.
	class PBRT4Parser
	{
	public:
		PBRT4Parser();
		PBRT4ParserOutput parse(PBRT4Scanner* scanner);

	private:
		using TokenIter = std::vector<Token>::const_iterator;

		using DirectiveTable = std::map<std::string, std::function<void(TokenIter& it, PBRT4ParserOutput& output)>>;
		DirectiveTable directiveTable;

		void initStates();

		void directive(TokenIter& it, PBRT4ParserOutput& output);
		
		void integrator(TokenIter& it, PBRT4ParserOutput& output);
		void transform(TokenIter& it, PBRT4ParserOutput& output);
		void sampler(TokenIter& it, PBRT4ParserOutput& output);
		void pixelFilter(TokenIter& it, PBRT4ParserOutput& output);
		void film(TokenIter& it, PBRT4ParserOutput& output);
		void camera(TokenIter& it, PBRT4ParserOutput& output);
		void texture(TokenIter& it, PBRT4ParserOutput& output);
		void makeNamedMaterial(TokenIter& it, PBRT4ParserOutput& output);
		void shape(TokenIter& it, PBRT4ParserOutput& output);
		void namedMaterial(TokenIter& it, PBRT4ParserOutput& output);
		void lightSource(TokenIter& it, PBRT4ParserOutput& output);
		void rotate(TokenIter& it, PBRT4ParserOutput& output);
		void concatTransform(TokenIter& it, PBRT4ParserOutput& output);
		void areaLightSource(TokenIter& it, PBRT4ParserOutput& output);
		void material(TokenIter& it, PBRT4ParserOutput& output);

		std::vector<PBRT4Parameter> parameters(TokenIter& it);

	private:
		using ParameterList = std::vector<PBRT4Parameter>;
		struct ShapeDesc
		{
			std::string   name;
			std::string   namedMaterial;
			Matrix        transform;
			bool          bIdentityTransform;
			ParameterList parameters;
		};
		struct MaterialDesc
		{
			std::string   name;
			ParameterList parameters;
		};
		struct TextureDesc
		{
			std::string   name;
			std::string   textureType;
			std::string   textureClass;
			ParameterList parameters;
		};

		PBRT4Parameter* findParameter(ParameterList& params, const char* pname) const;
		void compileShape(ShapeDesc& inDesc, PBRT4ParserOutput& output);
		void compileMaterial(MaterialDesc& inDesc, PBRT4ParserOutput& output);
		void compileTexture(TextureDesc& inDesc, PBRT4ParserOutput& output);

	private:
		// States
		PBRT4ParsePhase    parsePhase = PBRT4ParsePhase::RenderingOptions;
		Matrix             currentTransform;
		Matrix             currentTransformBackup;
		bool               bCurrentTransformIsIdentity = true;
		std::string        currentNamedMaterial;
		vec3               currentEmission;
	};
}
