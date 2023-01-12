// Create a bunch of static meshes along specific path.
// Separated just to keep app.cpp small.

#pragma once

#include "core/vec3.h"
#include <vector>

class StaticMesh;

class MeshSplatting
{
public:
	// Splat meshes on a circle.
	struct CreateParams
	{
		vec3 center;
		float radius;
		float height;
		uint32 numLoop;
		uint32 numMeshes;
	};

	void createResources(const CreateParams& createParams);
	void destroyResources();

	void tick(float deltaSeconds);

	const std::vector<StaticMesh*>& getStaticMeshes() const { return staticMeshes; }

private:
	std::vector<StaticMesh*> staticMeshes;
	std::vector<vec3> staticMeshesStartPos;
};
