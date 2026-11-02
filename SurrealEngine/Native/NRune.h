#pragma once
#include "Math/vec.h"

class NRune
{
public:
	static vec3 ArcVelocity(int trajectory, const vec3& source, const vec3& destination, float gravity);
	static void RegisterFunctions();
	static bool ClipJointBox(const vec4* planes, const vec3& start, const vec3& end, float radius, float& fraction, vec3& normal);
};
