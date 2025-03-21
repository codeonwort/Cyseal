#pragma once

#include "pbrt_scanner.h"
#include <string>

namespace pbrt
{
	class PBRT4Scanner;

	enum class PBRT4ParsePhase
	{
		RenderingOptions = 0,
		SceneElements    = 1,
		InsideAttribute  = 2,
	};

	class PBRT4ParserEx
	{
	public:
		void parse(PBRT4Scanner* scanner);

	private:
		using TokenIter = std::vector<Token>::const_iterator;
		void directive(TokenIter& it);
		void integrator(TokenIter& it);
		void parameters(TokenIter& it);
		void transform(TokenIter& it);

		// States
		PBRT4ParsePhase parsePhase;
	};
}
