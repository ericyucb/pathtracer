#include "camera.h"
#include <algorithm>
#include <cmath>

Camera::Camera(simd::float4 position, simd::float4 direction, float yaw, float pitch) :
ray(new Ray()), xAngle(pitch), yAngle(yaw) {
    ray->position = position;
    ray->direction = direction;
    updateDirection();
}


void Camera::moveX(float dx) {
    ray->position += simd::float4{right[0] * dx, right[1] * dx, right[2] * dx, 0};
}

void Camera::moveY(float dy) {
    ray->position += simd::float4{up[0] * dy, up[1] * dy, up[2] * dy, 0};    
}

void Camera::moveZ(float dz) {
    ray->position += ray->direction * dz;
}

void Camera::yaw(float angle) {
    yAngle -= (angle * M_PI / 180.0f);
    yAngle = fmod(yAngle, 2 * M_PI);
    updateDirection();
}

void Camera::pitch(float angle){
    xAngle -= (angle * M_PI / 180.0f); // gives the angle in radians
    xAngle = fmod(xAngle, 2* M_PI); 
    updateDirection();
}

void Camera::updateDirection(){


    simd::float4x4 rotateY = matrix_identity_float4x4;
    rotateY.columns[0][0] = std::cos(yAngle);
    rotateY.columns[0][2] = -std::sin(yAngle);
    rotateY.columns[1][1] = 1;
    rotateY.columns[2][0] = std::sin(yAngle);
    rotateY.columns[2][2] = std::cos(yAngle);

    simd::float4x4 rotateX = matrix_identity_float4x4;
    rotateX.columns[0][0] = 1;
    rotateX.columns[1][1] = std::cos(xAngle);
    rotateX.columns[1][2] = std::sin(xAngle);
    rotateX.columns[2][1] = -std::sin(xAngle);
    rotateX.columns[2][2] = std::cos(xAngle);


    ray -> direction = rotateX * simd::float4{0,0,1,0};
    ray -> direction = rotateY * ray->direction;

    simd::float3 dir = simd_make_float3(ray->direction[0], ray->direction[1], ray->direction[2]);; //forward direction (should be normalized)
    simd::float3 proj_up_dir = simd::dot(dir, simd::float3{0,1,0}) * dir; //the projection of up onto the new direction
    up = simd::normalize(simd::float3{0, 1, 0} - proj_up_dir); //removes the foward component
    right = simd::normalize(simd::cross(dir, up));


}

Ray* Camera::getRay() {
    return ray;
}


