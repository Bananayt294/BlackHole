#include "Camera.h"

void Camera::processMouse(float deltaX, float deltaY) {
	yaw += deltaX * this->mouseSens;
	pitch += deltaY * this->mouseSens;

	constexpr float limit = 1.55334f;

	pitch = std::clamp(pitch, -limit, limit);
}

void Camera::update(const bool* keys, float deltaTime) {
	float forwardInput = 0.0f;
	float rightInput = 0.0f;
	float verticalInput = 0.0f;

	if (keys[SDL_SCANCODE_W]) forwardInput += 1.0f;
	if (keys[SDL_SCANCODE_S]) forwardInput -= 1.0f;
	if (keys[SDL_SCANCODE_D]) rightInput += 1.0f;
	if (keys[SDL_SCANCODE_A]) rightInput -= 1.0f;

	if (keys[SDL_SCANCODE_SPACE]) verticalInput += 1.0f;
	if (keys[SDL_SCANCODE_LCTRL]) verticalInput -= 1.0f;

	float cosPitch = std::cos(pitch);
	float sinPitch = std::sin(pitch);
	float cosYaw = std::cos(yaw);
	float sinYaw = std::sin(yaw);

	float forwardX = cosPitch * sinYaw;
	float forwardY = sinPitch;
	float forwardZ = -cosPitch * cosYaw;

	float rightX = cosYaw;
	float rightZ = sinYaw;

	float moveX = forwardX * forwardInput + rightX * rightInput;
	float moveY = forwardY * forwardInput + verticalInput;
	float moveZ = forwardZ * forwardInput + rightZ * rightInput;

	float length = std::sqrt(moveX * moveX + moveY * moveY + moveZ * moveZ);

	if (length > 0.0001f)
	{
		moveX /= length;
		moveY /= length;
		moveZ /= length;

		float speed = movementSpeed;

		if (keys[SDL_SCANCODE_LSHIFT])
			speed *= 4.0f;

		x += moveX * speed * deltaTime;
		y += moveY * speed * deltaTime;
		z += moveZ * speed * deltaTime;
	}
}
