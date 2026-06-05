#pragma once
#include <simd/simd.h>
#include <vector>

struct Sphere {
    simd::float4 positionAndRadius;  // xyz = center, w = radius
    simd::float4 color;              // xyz = rgb, w unused
};

struct Scene {
    std::vector<Sphere> objects;
};
