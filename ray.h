#pragma once
#include <simd/simd.h>

class Ray {
    public:
        simd::float4 position;
        simd::float4 direction = simd::float4{0,0,1,0};
};
