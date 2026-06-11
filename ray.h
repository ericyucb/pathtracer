#pragma once
#include <simd/simd.h>
using namespace simd;

struct Ray {
    float4 position;
    float4 direction = float4{0,0,1,0};
};
