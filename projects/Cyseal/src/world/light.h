#pragma once

#include "core/vec3.h"

class DirectionalLight
{
public:
	DirectionalLight()
	{
		direction = normalize(vec3(0.0f, -1.0f, -1.0f));
		illuminance = 64000.0f * vec3(1.0f, 1.0f, 1.0f); // Daylight
	}

	// World space direction
	vec3 direction;

	// https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf
	// https://www.realtimerendering.com/blog/physical-units-for-lights/
	// https://en.wikipedia.org/wiki/Lux#Illuminance
	// Unit: lux (lm / m^2)
	vec3 illuminance;
};
