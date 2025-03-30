#pragma once

#include "pbrt_scanner.h"
#include "core/matrix.h"

#include <string>
#include <map>
#include <functional>

namespace pbrt
{
	class PBRT4Scanner;

	enum class PBRT4ParsePhase
	{
		RenderingOptions = 0,
		SceneElements    = 1,
		InsideAttribute  = 2,
	};

	enum class PBRT4ParameterTypeEx
	{
		String, Texture, Bool,
		Float3, Float, Float2Array, Float3Array,
		Int, IntArray,
	};
	struct PBRT4ParameterEx
	{
		PBRT4ParameterTypeEx datatype = PBRT4ParameterTypeEx::String;
		std::string name;

		// #todo-pbrt-parser: union
		std::string asString; // String
		std::string asTexture; // Texture
		bool asBool = false; // Bool
		float asFloat = 0.0f; // Float
		float asFloat3[3] = { 0.0f, 0.0f, 0.0f }; // Float3
		std::vector<float> asFloatArray; // Float2Array, Float3Array
		int32 asInt; // Int
		std::vector<int32> asIntArray; // IntArray
	};

	struct PBRT4ParserOutput
	{
		using ParameterList = std::vector<PBRT4ParameterEx>;
		struct Shape
		{
			std::string   name;
			std::string   namedMaterial;
			Matrix        transform;
			bool          bIdentityTransform;
			ParameterList parameters;
		};

		std::vector<Shape> shapes;
	};

	class PBRT4ParserEx
	{
	public:
		PBRT4ParserEx();
		PBRT4ParserOutput parse(PBRT4Scanner* scanner);

	private:
		using TokenIter = std::vector<Token>::const_iterator;

		using DirectiveTable = std::map<std::string, std::function<void(TokenIter& it, PBRT4ParserOutput& output)>>;
		DirectiveTable directiveTable;

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

		std::vector<PBRT4ParameterEx> parameters(TokenIter& it);

	private:
		// States
		PBRT4ParsePhase    parsePhase = PBRT4ParsePhase::RenderingOptions;
		Matrix             currentTransform;
		Matrix             currentTransformBackup;
		bool               bCurrentTransformIsIdentity = true;
		std::string        currentNamedMaterial;
	};
}
