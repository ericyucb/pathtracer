#pragma once
#include <simd/simd.h>

class Ray {
    public:
        simd::float4 position;
        simd::float4 direction;
};
