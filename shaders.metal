#include <metal_stdlib>
using namespace metal;


// These are the two inputs the GPU passes into your kernel for each thread.

//   texture2d<float, access::write> outTexture [[texture(0)]]
//   - texture2d<float> — a 2D texture where each channel is a float
//   - access::write — this thread can only write to it, not read
//   - [[texture(0)]] — this is bound to texture slot 0, which is why encoder->setTexture(outputTexture, 0) uses slot 0 on the CPU side

//   uint2 pos [[thread_position_in_grid]]
//   - uint2 — a pair of unsigned integers (x, y)
//   - [[thread_position_in_grid]] — Metal automatically fills this in with the pixel coordinate of the current thread. So for the thread handling pixel (42, 100), pos.x = 42 and pos.y = 100

//   The [[ ]] syntax is Metal's way of marking special GPU-provided values. You don't pass these in yourself — Metal injects them automatically based on how the threads were dispatched.
kernel void render_kernel(
    texture2d<float, access::write> outTexture [[texture(0)]],
    uint2 pos [[thread_position_in_grid]])
{
    if (pos.x >= outTexture.get_width() || pos.y >= outTexture.get_height()) return;


    // scale coordinates to between -1, 1
    float2 uv = float2(pos) / float2(outTexture.get_width(), outTexture.get_height());
    uv *= 2.0f;
    uv -= 1.0f;
    uv.x *= (float)outTexture.get_width() / (float)outTexture.get_height(); // aspect ratio fix


    //Predefine variables for now (drawing a sphere)

    float3 center = {0, 0, 0}; // center of sphere
    float r = 0.8f; /// radius

    float3 a = {0, 0, 2.0f}; // ray start
    float3 b = normalize(float3(uv, -1.0f)); // ray direction -> this points the ray at the pixel

    //Quadratic fomrula dot(b, b) t^2 + 2(dot(a, b) - dot(b, center)) t + c = 0
    // c = dot(a, a) - 2 dot(a, center) + dot(center, center) - dot(r, r)

    float squared_coefficient = dot(b, b);
    float linear_coefficient = 2*(dot(a, b) - dot(b, center));
    float constant_coefficient = dot(a, a) - 2 * dot(a, center) + dot(center, center) - r * r;

    //solve for the discriminant to determine how many solutions there are
    //discriminant = linear_coefficient^2 - 4 squared coefficient * constant coefficient

    float discriminant = linear_coefficient * linear_coefficient - 4.0f * squared_coefficient * constant_coefficient;

    // solve for t
    
    // float red =  0;
    // float green =  204.0f/255.0f;
    // float blue =  204.0f/255.0f;

    float t1;
    float t2;
    float3 h1;
    float3 h2;

    float3 lightDirection = normalize(float3(-1.0f, -1.0f, -1.0f));

    if (discriminant >= 0) { // at least one solution -> fill in the pixel because that indicates an intersection
        t1 = (-linear_coefficient - sqrt(discriminant))/ 2.0f*squared_coefficient;
        t2 = (-linear_coefficient + sqrt(discriminant))/ 2.0f*squared_coefficient;

        h1 = a + b * t1; // first intersection -> hitpoint
        h2 = a + b * t2;

        float3 h1_normal_vector = normalize(h1 - center);

        float lightIntensity = max(dot(-lightDirection, h1_normal_vector), 0.0f); //scaled between 0 and 1

        float3 sphereColor = float3(0.5f, 0.0f, 1.0f);
        outTexture.write(float4(sphereColor * lightIntensity,  1.0f), pos);

    } else { //the ray doesn't intersect with the sphere -> don't fill in the pixel
        outTexture.write(float4(0.0f, 0.0f, 0.0f, 1.0f), pos);
    }

  
}
