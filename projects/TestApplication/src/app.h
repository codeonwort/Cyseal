#pragma once

#include "app_base.h"

class Application : public AppBase
{

protected:
	virtual bool onInitialize() override;
	virtual bool onUpdate(float dt) override;
	virtual bool onTerminate() override;

};
