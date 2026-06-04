#include <simd/simd.h>
#include "ray.h"


class Camera{

    private:
        Ray* ray;
        float yAngle;
        float xAngle;

    public:
        Camera(simd::float4 position, simd::float4 direction, float yaw, float pitch);
        void moveX(float dx);
        void moveY(float dy);
        void moveZ(float dz);
        void yaw(float amount); //y axis rotation
        void pitch(float amount); //x axis rotation
        Ray* getRay();
        void updateDirection();
};