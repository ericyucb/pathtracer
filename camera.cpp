#include "camera.h"

Camera::Camera(simd::float4 position, simd::float4 direction, float yaw, float pitch) {
    ray = new Ray();
    ray->position = position;
    ray->direction = direction;
    this->yAngle = yaw;
    this->xAngle = pitch;
}

void Camera::moveX(float dx) {
    ray->position += simd::float4{dx, 0, 0, 0};
}

void Camera::moveY(float dy) {
    ray->position += simd::float4{0, dy, 0, 0};
}

void Camera::moveZ(float dz) {
    ray->position += ray->direction * dz;
}

void Camera::yaw(float angle) {
    yAngle -= (angle * M_PI / 180.0f);
    updateDirection();
}

void Camera::pitch(float angle){
    xAngle += (angle * M_PI / 180.0f);
    updateDirection();
}

void Camera::updateDirection(){
  

    simd::float4x4 rotateY = matrix_identity_float4x4;
    rotateY.columns[0][0] = std::cos(yAngle);
    rotateY.columns[0][2] = -std::sin(yAngle);
    rotateY.columns[1][1] = 1;
    rotateY.columns[2][0] = std::sin(yAngle);
    rotateY.columns[2][2] = std::cos(yAngle);

    ray -> direction = rotateY * simd::float4{0,0,-1,0};

    simd::float4x4 rotateX = matrix_identity_float4x4;
    rotateX.columns[0][0] = 1;
    rotateX.columns[1][1] = std::cos(xAngle);
    rotateX.columns[1][2] = std::sin(xAngle);
    rotateX.columns[2][1] = -std::sin(xAngle);
    rotateX.columns[2][2] = std::cos(xAngle);
    ray -> direction = rotateX * ray->direction;


}

Ray* Camera::getRay() {
    return ray;
}
