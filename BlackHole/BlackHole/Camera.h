#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

class Camera {
public:
	float x = 0.0f;
	float y = 0.7f;
	float z = 3.7f;
	float yaw = 0.0f;
	float pitch = -0.187f;

	float fovDegrees = 70.0f;
	float movementSpeed = 1.5f;
	float mouseSens = 0.0025f;

	void processMouse(float deltaX, float deltaY);
	void update(const bool* keys, float deltaTime);
};