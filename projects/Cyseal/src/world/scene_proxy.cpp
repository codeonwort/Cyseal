#include "scene_proxy.h"
#include "render/static_mesh.h"

SceneProxy::~SceneProxy()
{
	for (StaticMeshProxy* sm : staticMeshes)
	{
		delete sm;
	}
}
