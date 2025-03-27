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

	class PBRT4ParserEx
	{
	public:
		PBRT4ParserEx();
		void parse(PBRT4Scanner* scanner);

	private:
		using TokenIter = std::vector<Token>::const_iterator;

		void directive(TokenIter& it);
		
		void integrator(TokenIter& it);
		void transform(TokenIter& it);
		void sampler(TokenIter& it);
		void pixelFilter(TokenIter& it);
		void film(TokenIter& it);
		void camera(TokenIter& it);
		void texture(TokenIter& it);
		void makeNamedMaterial(TokenIter& it);
		void shape(TokenIter& it);
		void namedMaterial(TokenIter& it);

		std::vector<PBRT4ParameterEx> parameters(TokenIter& it);

		// States
		PBRT4ParsePhase parsePhase = PBRT4ParsePhase::RenderingOptions;
		Matrix currentTransform;
		bool bCurrentTransformIsIdentity = true;
		std::string currentNamedMaterial;

		using DirectiveTable = std::map<std::string, std::function<void(TokenIter& it)>>;
		DirectiveTable directiveTable;
	};
}
