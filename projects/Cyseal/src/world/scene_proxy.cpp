#include "scene_proxy.h"
#include "render/static_mesh.h"
#include "memory/mem_alloc.h"

SceneProxy::~SceneProxy()
{
	delete staticMeshProxyAllocator;
}
