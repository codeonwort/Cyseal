#include "d3d_util.h"
#include "d3d_device.h"

class D3DDevice* getD3DDevice()
{
	return static_cast<D3DDevice*>(gRenderDevice);
}
