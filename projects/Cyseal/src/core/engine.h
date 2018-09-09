#pragma once

#include <stdint.h>

enum class EEngineState : uint8_t
{
	UNINITIALIZED,
	RUNNING,
	SHUTDOWN
};

class CysealEngine final
{

public:
	explicit CysealEngine();
	~CysealEngine();

	CysealEngine(const CysealEngine& rhs) = delete;
	CysealEngine& operator=(const CysealEngine& rhs) = delete;

	void startup();
	void shutdown();

private:
	EEngineState state;

};
