#pragma once
#include <simd/simd.h>
#include "ray.h"
using namespace simd;

struct CameraMetadata{
    float fov;
    float blurCoefficient;
    float focalDistance;
    Ray cameraOrientation; //ray takes up 32 bytes
    int padding[1];
};
class Camera{

    // struct CameraMetadata{
    //     float fov;
    //     float blurCoefficient = 0.0001f;
    //     float focalDistance = 5.0f;
    //     Ray* cameraOrientation; //ray takes up 32 bytes
    //     int padding[1];
    // };

   

    private:
        Ray* cameraOreintation; // forward direction ray
        float yAngle;
        float xAngle;
        float fov;
        float blurCoefficient;
        float focalDistance;

        simd::float3 right = {1, 0, 0};
        simd::float3 up = {0, 1, 0};

    public:
        Camera(simd::float4 position, simd::float4 direction, float yaw, float pitch, float fov, float blurCoefficent, float focalDistance);
        void moveX(float dx);
        void moveY(float dy);
        void moveZ(float dz);
        void yaw(float amount); //y axis rotation
        void pitch(float amount); //x axis rotation
        Ray* getRay();
        void updateDirection();
        CameraMetadata getCameraStruct();
};