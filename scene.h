#pragma once
#include <simd/simd.h>
#include <vector>
using namespace simd;

struct Sphere {
    float4 positionAndRadius;  // xyz = center, w = radius
    //use material index
    int materialIndex;
};

  struct Material {
      float4 color; // xyz = rgb, w unused
      float roughness;
      float metallic;
      float4 emissionColor;
      float emissionIntensity;
      float padding;
      float specularProbability;
      float indexOfRefraction;
      int isDielectric;
     
};

struct Scene {
    std::vector<Sphere> objects;
    std::vector<Material> materials;
};
