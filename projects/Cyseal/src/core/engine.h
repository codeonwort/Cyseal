#pragma once

#include "render/render_device.h"
#include <stdint.h>

enum class EEngineState : uint8_t
{
	UNINITIALIZED,
	RUNNING,
	SHUTDOWN
};

struct CysealEngineCreateParams
{
	RenderDeviceCreateParams renderDevice;
};

class CysealEngine final
{

public:
	explicit CysealEngine();
	~CysealEngine();

	CysealEngine(const CysealEngine& rhs) = delete;
	CysealEngine& operator=(const CysealEngine& rhs) = delete;

	void startup(const CysealEngineCreateParams& createParams);
	void shutdown();

	inline RenderDevice* getRenderDevice() const { return renderDevice; }

private:
	void createRenderDevice(const RenderDeviceCreateParams& createParams);

private:
	EEngineState state;

	class RenderDevice* renderDevice;

};
