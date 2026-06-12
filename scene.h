#pragma once
#include <simd/simd.h>
#include <vector>
#include "ray.h"
using namespace simd;
using namespace std;

struct Mesh{
    int firstTriangleIndex;
    int triangleCount;
    int materialIndex;
    float3 boundsMin;
    float3 boundsMax;
};

struct Sphere{
    float4 positionAndRadius;  // xyz = center, w = radius
    //use material index
    int materialIndex;
    int padding[3];
};

struct Triangle{
    float3 p1, p2, p3; //each point (x, y, z), initialized in counterclockwise fashion, this takes up 16 * 3 bytes due to byte allignment

    int materialIndex;
    int padding[3];

};



struct Material {
    float4 albetoAndRoughness; // xyz = rgb, w roughness
    float4 emissionColor; //xyz = emission color, w = emission Intensity
    float4 params; //metallic, specularProbability, indexOfRefraction, unused
    int type; //0 = lambertian, 1 = dielectric, 2 = metallic
    int padding[3];
    
};


struct Scene {
    vector<Triangle> triangles;
    vector<Sphere> spheres;
    vector<Mesh> meshes;
    vector<Material> materials;
};
