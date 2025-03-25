#pragma once

#include "pbrt_scanner.h"
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

	enum class PBRT4ParameterTypeEx { String, Float3, Float, Int, Bool };
	struct PBRT4ParameterEx
	{
		PBRT4ParameterTypeEx datatype = PBRT4ParameterTypeEx::String;
		std::string name;

		std::string asString;
		float asFloat3[3] = { 0.0f, 0.0f, 0.0f };
		float asFloat = 0.0f;
		int32 asInt = 0;
		bool asBool = false;
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

		std::vector<PBRT4ParameterEx> parameters(TokenIter& it);

		// States
		PBRT4ParsePhase parsePhase = PBRT4ParsePhase::RenderingOptions;

		using DirectiveTable = std::map<std::string, std::function<void(TokenIter& it)>>;
		DirectiveTable directiveTable;
	};
}
