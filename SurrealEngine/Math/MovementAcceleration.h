#pragma once
#include "vec.h"
#include <algorithm>

// Shared pure velocity update. It changes only its arguments; prediction never
// runs actor physics callbacks, scripts, pickups or feedback during replay.
inline void ApplyMovementVelocity(vec3& velocity,vec3& acceleration,float elapsed,
    float accelRate,float friction,float maxSpeed)
{
	if (dot(acceleration, acceleration) > 0.0001f)
	{
		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(acceleration);
		vec3 accelDir = acceleration * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			acceleration = accelDir * accelRate;

		float speed = length(velocity);
		velocity = velocity - (velocity - accelDir * speed) * (friction * elapsed);
	}
	else
	{
		float speed = length(velocity);
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * friction * 2.0f * elapsed, 0.0f);
			velocity = velocity * (newSpeed / speed);
		}
	}

	velocity = velocity + acceleration * elapsed;

	float speed = length(velocity);
	if (speed > 0.0f && speed > maxSpeed)
		velocity = velocity * (maxSpeed / speed);
}
