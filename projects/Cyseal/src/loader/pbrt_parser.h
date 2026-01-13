#pragma once

#include "pbrt_scanner.h"
#include "core/matrix.h"
#include "core/vec2.h"
#include "core/smart_pointer.h"
#include "core/assertion.h"

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
		InsideObject     = 3,
	};

	enum class PBRT4ParameterType
	{
		String, Texture, Spectrum, Bool,
		Float3, Float, FloatArray, Float2Array, Float3Array,
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
		std::vector<float> asFloatArray;             // FloatArray, Float2Array, Float3Array
		int32              asInt;                    // Int
		std::vector<int32> asIntArray;               // IntArray
	};

	struct PBRT4MaterialRef
	{
		static const uint32 INVALID_UNNAMED_MATERIAL_ID = 0xffffffff;

		uint32      unnamedId; // Used if unnamed material. INVALID_UNNAMED_MATERIAL_ID otherwise.
		std::string name;      // Used if named material. Empty string otherwise.

		inline bool isUnnamed() const { return unnamedId != INVALID_UNNAMED_MATERIAL_ID; }
	};

	struct PBRT4ParserOutput
	{
		struct TextureFileDesc
		{
			std::string    textureName;
			std::string    textureFilter;
			std::wstring   filename;
			int32          numChannels; // 1 or 3
		};
		struct MaterialDesc
		{
			PBRT4MaterialRef materialName;
			std::string      materialType;
			bool             bUseRgbReflectance       = false;
			vec3             rgbReflectance           = vec3(1.0f);
			std::string      textureReflectance;
			bool             bUseAnisotropicRoughness = false;
			bool             bRemapRoughness          = false;
			float            roughness                = 1.0f;
			float            vroughness               = 1.0f;
			float            uroughness               = 1.0f;
			bool             bTransmissive            = false;
			vec3             rgbTransmittance         = vec3(0.0f);
			std::string      textureTransmittance;
			bool             bUseRgbEtaAndK           = false;
			vec3             rgbEta;
			vec3             rgbK;
			std::string      spectrumEta;
			std::string      spectrumK;
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
			std::wstring     filename;
			PBRT4MaterialRef materialName;
			Matrix           transform;
			bool             bIdentityTransform;
		};
		struct ObjectDeclDesc
		{
			std::string                   name;
			std::vector<TriangleMeshDesc> triangleShapeDescs;
			std::vector<PLYShapeDesc>     plyShapeDescs;
		};
		struct ObjectInstanceDesc
		{
			std::string      name;
			Matrix           instanceTransform;
		};

	public:
		bool                            bValid = true;
		std::vector<std::wstring>       errorMessages;

		Matrix                          sceneTransform;
		std::vector<TextureFileDesc>    textureFileDescs;
		std::vector<MaterialDesc>       namedMaterialDescs;
		std::vector<MaterialDesc>       unnamedMaterialDescs;
		std::vector<TriangleMeshDesc>   triangleShapeDescs;
		std::vector<PLYShapeDesc>       plyShapeDescs;
		std::vector<ObjectDeclDesc>     objectDeclDescs;
		std::vector<ObjectInstanceDesc> objectInstanceDescs;
	};

	// Parses tokens and produces model data suitable for renderer.
	class PBRT4Parser
	{
	public:
		PBRT4Parser();
		PBRT4ParserOutput parse(PBRT4Scanner* scanner);

	// Parser part.
	private:
		using TokenIter = std::vector<Token>::const_iterator;

		void initStates();
		void parserError(TokenIter& it, const wchar_t* msg, ...);
		bool parserWrongToken(TokenIter& it, TokenType tokType);

		void directive(TokenIter& it, PBRT4ParserOutput& output);

		void worldBegin(TokenIter& it, PBRT4ParserOutput& output);
		void transformBegin(TokenIter& it, PBRT4ParserOutput& output);
		void transformEnd(TokenIter& it, PBRT4ParserOutput& output);
		void attributeBegin(TokenIter& it, PBRT4ParserOutput& output);
		void attributeEnd(TokenIter& it, PBRT4ParserOutput& output);
		void objectBegin(TokenIter& it, PBRT4ParserOutput& output);
		void objectEnd(TokenIter& it, PBRT4ParserOutput& output);
		void objectInstance(TokenIter& it, PBRT4ParserOutput& output);

		void integrator(TokenIter& it, PBRT4ParserOutput& output);
		void sampler(TokenIter& it, PBRT4ParserOutput& output);
		void pixelFilter(TokenIter& it, PBRT4ParserOutput& output);
		void film(TokenIter& it, PBRT4ParserOutput& output);
		void camera(TokenIter& it, PBRT4ParserOutput& output);

		void transform(TokenIter& it, PBRT4ParserOutput& output);
		void rotate(TokenIter& it, PBRT4ParserOutput& output);
		void scale(TokenIter& it, PBRT4ParserOutput& output);
		void lookAt(TokenIter& it, PBRT4ParserOutput& output);
		void concatTransform(TokenIter& it, PBRT4ParserOutput& output);

		void texture(TokenIter& it, PBRT4ParserOutput& output);

		void material(TokenIter& it, PBRT4ParserOutput& output);
		void namedMaterial(TokenIter& it, PBRT4ParserOutput& output);
		void makeNamedMaterial(TokenIter& it, PBRT4ParserOutput& output);

		void shape(TokenIter& it, PBRT4ParserOutput& output);

		void lightSource(TokenIter& it, PBRT4ParserOutput& output);
		void areaLightSource(TokenIter& it, PBRT4ParserOutput& output);

		std::vector<PBRT4Parameter> parameters(TokenIter& it);

	private:
		using DirectiveTable = std::map<std::string, std::function<void(TokenIter& it, PBRT4ParserOutput& output)>>;
		DirectiveTable directiveTable;
		TokenIter      eofTokenIt;

		// Parser states
		bool                      bValid = true;
		std::vector<std::wstring> errorMessages;
		PBRT4ParsePhase           parsePhase = PBRT4ParsePhase::RenderingOptions;
		uint32                    nextUnnamedMaterialId = 0; // This should keep increasing regardless of graphics state save/restore.

		std::vector<PBRT4ParsePhase> parsePhaseStack;
		void setParsePhase(PBRT4ParsePhase newPhase) { parsePhase = newPhase; }
		void pushParsePhase() { parsePhaseStack.push_back(parsePhase); }
		void popParsePhase()
		{
			const size_t n = parsePhaseStack.size();
			CHECK(n > 0);
			parsePhase = parsePhaseStack[n - 1];
			parsePhaseStack.pop_back();
		}

		// Graphics states
		struct GraphicsState
		{
			Matrix                transform;
			bool                  bTransformIsIdentity; // #todo-pbrt-object: Do I need this?
			bool                  bMaterialIsUnnamed;
			uint32                unnamedMaterialId;
			std::string           namedMaterial;
			vec3                  emission;

			void initStates()
			{
				transform.identity();
				bTransformIsIdentity = true;
				bMaterialIsUnnamed = true;
				unnamedMaterialId = PBRT4MaterialRef::INVALID_UNNAMED_MATERIAL_ID;
				namedMaterial = "";
				emission = vec3(0.0f);
			}

			uint32 setUnnamedMaterial(uint32 nextId)
			{
				bMaterialIsUnnamed = true;
				unnamedMaterialId = nextId;
				return unnamedMaterialId;
			}

			void setNamedMaterial(const std::string& name)
			{
				bMaterialIsUnnamed = false;
				namedMaterial = name;
			}

			PBRT4MaterialRef getActiveMaterialName()
			{
				if (bMaterialIsUnnamed)
				{
					return PBRT4MaterialRef{ unnamedMaterialId, "" };
				}
				return PBRT4MaterialRef{ PBRT4MaterialRef::INVALID_UNNAMED_MATERIAL_ID, namedMaterial };
			}

			void copyTransformFrom(const GraphicsState& other) // For TransformBegin and TransformEnd directives.
			{
				transform = other.transform;
				bTransformIsIdentity = other.bTransformIsIdentity;
			}
		};
		GraphicsState graphicsState;

		std::vector<GraphicsState> graphicsStateStack;
		void pushGraphicsState()
		{
			graphicsStateStack.push_back(graphicsState);
		}
		void popGraphicsState(bool restoreOnlyTransform = false)
		{
			const size_t n = graphicsStateStack.size();
			CHECK(n > 0);
			const GraphicsState& backup = graphicsStateStack[n - 1];
			if (restoreOnlyTransform)
			{
				graphicsState = backup;
			}
			else
			{
				graphicsState.copyTransformFrom(backup);
			}
			graphicsStateStack.pop_back();
		}

		struct ObjectState
		{
			Matrix                                           transform;
			std::vector<PBRT4ParserOutput::TriangleMeshDesc> triangleShapeDescs;
			std::vector<PBRT4ParserOutput::PLYShapeDesc>     plyShapeDescs;

			void initStates(const Matrix& currentTransform)
			{
				transform = currentTransform;
				triangleShapeDescs = {};
				plyShapeDescs = {};
			}

			bool isEmpty() const
			{
				return triangleShapeDescs.size() == 0
					&& plyShapeDescs.size() == 0;
			}
		};
		ObjectState objectState;
		std::vector<std::string> objectNames;
		std::string activeObjectName; // empty = no active object
		bool anyActiveObject() const {
			// Not (parsePhase == PBRT4ParsePhase::InsideObject)
			// because AttributeBegin/End can be nested within ObjectBegin/End.
			return activeObjectName.empty() == false;
		}

		void setCurrentTransform(const Matrix& M);
		void appendCurrentTransform(const Matrix& M);

	// Compiler part. Wanna separate parser and compiler but the file format is kinda state machine.
	private:
		using ParameterList = std::vector<PBRT4Parameter>;
		struct ShapeDesc
		{
			std::string      name;
			PBRT4MaterialRef materialName;
			Matrix           transform;
			bool             bIdentityTransform;
			ParameterList    parameters;
		};
		struct MaterialDesc
		{
			PBRT4MaterialRef name;
			ParameterList    parameters;
		};
		struct TextureDesc
		{
			std::string      name;
			std::string      textureType;
			std::string      textureClass;
			ParameterList    parameters;
		};
		struct ObjectDesc
		{
			std::string            name;
			std::vector<ShapeDesc> shapes;
		};

		PBRT4Parameter* findParameter(ParameterList& params, const char* pname) const;
		void compileShape(ShapeDesc& inDesc, PBRT4ParserOutput& output);
		void compileMaterial(MaterialDesc& inDesc, PBRT4ParserOutput& output);
		void compileTexture(TextureDesc& inDesc, PBRT4ParserOutput& output);
	};
}
