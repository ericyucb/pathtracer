#pragma once
#include <simd/simd.h>
#include <vector>

struct Sphere {
    simd::float4 positionAndRadius;  // xyz = center, w = radius
    //use material index
    simd::float4 color;              // xyz = rgb, w unused
    float roughness; 
    float metallic = 0.0f;
    simd::float2 padding;
};

struct Scene {
    std::vector<Sphere> objects;

    //implement materaials and tie it to a sphere
};
