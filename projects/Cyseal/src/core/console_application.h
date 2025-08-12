#pragma once

#include "application.h"

class ConsoleApplication : public ApplicationBase
{
public:
	virtual EApplicationReturnCode launch(const ApplicationCreateParams& createParams) override
	{
		onExecute();

		return EApplicationReturnCode::Ok;
	}

protected:
	virtual void onExecute() = 0;
};
