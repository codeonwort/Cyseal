#pragma once

#include "render/forward_renderer.h"

class D3DForwardRenderer : public ForwardRenderer
{
	
protected:
	virtual void createRenderPasses() override;

};
