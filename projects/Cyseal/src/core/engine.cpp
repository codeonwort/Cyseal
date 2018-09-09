#include "engine.h"
#include "assertion.h"
#include "util/unit_test.h"

CysealEngine::CysealEngine()
{
	state = EEngineState::UNINITIALIZED;
}

CysealEngine::~CysealEngine()
{
	CHECK(state == EEngineState::SHUTDOWN);
}

void CysealEngine::startup()
{
	CHECK(state == EEngineState::UNINITIALIZED);

	UnitTestValidator::runAllUnitTests();

	state = EEngineState::RUNNING;
}

void CysealEngine::shutdown()
{
	CHECK(state == EEngineState::RUNNING);

	state = EEngineState::SHUTDOWN;
}
