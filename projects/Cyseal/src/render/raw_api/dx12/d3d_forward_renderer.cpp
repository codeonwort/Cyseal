#include "d3d_forward_renderer.h"
#include "d3d_base_pass.h"

void D3DForwardRenderer::createRenderPasses()
{
	basePass = new D3DBasePass;

	static_cast<D3DBasePass*>(basePass)->initialize();
}
