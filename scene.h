#pragma once
#include <simd/simd.h>
#include <vector>

struct Sphere {
    simd::float4 positionAndRadius;  // xyz = center, w = radius
    //use material index
    int materialIndex;
};

struct Material {
    simd::float4 color; // xyz = rgb, w unused
    float roughness;
    float metallic;
    simd::float4 emissionColor;
    float emissionIntensity;
    float padding;
};

struct Scene {
    std::vector<Sphere> objects;
    std::vector<Material> materials;
};
