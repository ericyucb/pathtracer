#include <simd/simd.h>

#include <vector>
struct Sphere {
    simd::float3 position; 
    float radius;
    simd::float3 color;
};

struct Scene {
    std::vector<Sphere> objects;
    //TODO: add textures
};