#include <metal_stdlib>
using namespace metal;



uint pcg(thread uint& state) {
      uint prev = state * 747796405u + 2891336453u;
      uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
      state = prev;
      return (word >> 22u) ^ word;
  };

  float randFloat(thread uint& seed) {
      return float(pcg(seed)) / 4294967296.0f; // [0, 1)
  };

  float3 randFloat3(thread uint& seed, float lo, float hi) {
      return float3(
          lo + randFloat(seed) * (hi - lo),
          lo + randFloat(seed) * (hi - lo),
          lo + randFloat(seed) * (hi - lo)
      );
  };

// These are the two inputs the GPU passes into your kernel for each thread.

//   texture2d<float, access::write> outTexture [[texture(0)]]
//   - texture2d<float> — a 2D texture where each channel is a float
//   - access::write — this thread can only write to it, not read
//   - [[texture(0)]] — this is bound to texture slot 0, which is why encoder->setTexture(outputTexture, 0) uses slot 0 on the CPU side

//   uint2 pos [[thread_position_in_grid]]
//   - uint2 — a pair of unsigned integers (x, y)
//   - [[thread_position_in_grid]] — Metal automatically fills this in with the pixel coordinate of the current thread. So for the thread handling pixel (42, 100), pos.x = 42 and pos.y = 100

//   The [[ ]] syntax is Metal's way of marking special GPU-provided values. You don't pass these in yourself — Metal injects them automatically based on how the threads were dispatched.
struct Ray {
    float4 position;
    float4 direction;
};

struct Material {
    float4 color; // xyz = rgb, w unused
    float roughness;
    float metallic;
    float4 emissionColor;
    float emissionIntensity;
    float padding;
};

struct Sphere {
    float4 positionAndRadius;  // xyz = center, w = radius
    int materialIndex;
};



struct metadata {
    float distance;
    int sphereObject;
    float4 worldPosition;
    float4 worldNormal;
};


metadata Hit(thread Ray& ray, float hitDistance, int sphereIndex, constant Sphere* objects){

    metadata meta;
    meta.distance = hitDistance;
    meta.sphereObject = sphereIndex;
    meta.worldPosition = ray.position + ray.direction * hitDistance;
    
    float3 center = objects[sphereIndex].positionAndRadius.xyz;

    meta.worldNormal = float4(normalize(meta.worldPosition.xyz - center), 0.0f);

    return meta;

};

metadata Miss(){

    metadata payload = {};
    payload.distance = -1.0f;

    return payload;


};
metadata TraceRay(thread Ray& ray, constant Sphere* objects, int count){

    float closestDistance = -1;
    int closestSphere = -1;
    float3 a = ray.position.xyz;
    float3 b = ray.direction.xyz;

    for (int i = 0; i < count; i++) {

        Sphere object = objects[i];
        float3 center = object.positionAndRadius.xyz;
        float r = object.positionAndRadius.w;
       
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
        // float3 h1;
        // float3 h2;


        if (discriminant >= 0) { // at least one solution -> fill in the pixel because that indicates an intersection
            t1 = (-linear_coefficient - sqrt(discriminant))/ (2.0f*squared_coefficient);
            t2 = (-linear_coefficient + sqrt(discriminant))/ (2.0f*squared_coefficient);
            if (t1 > 0) {

                if(closestDistance == -1 or t1 < closestDistance) {
                    closestDistance = t1;
                    closestSphere = i;
                }
                
            }else if (t2 > 0) {
                if(closestDistance == -1 or t2 < closestDistance) {
                    closestDistance = t2;
                    closestSphere = i;
                }
            } 

        } 
    }

    if(closestDistance >= 0) { //only render if we intersected some sphere

        metadata payload = Hit(ray, closestDistance, closestSphere, objects);
        return payload;

    } else{

        metadata payload = Miss();
        return payload;

    }

};


//aka per pixel
kernel void render_kernel( 
    texture2d<float, access::write> outTexture [[texture(0)]],
    constant Ray& camera [[buffer(0)]],
    constant Sphere* objects [[buffer(1)]],
    constant Material* materials [[buffer(2)]],
    constant int& sceneCount [[buffer(3)]],
    device float4* accumulatedColor [[buffer(4)]],
    constant int& frameIndex [[buffer(5)]],
    uint2 pos [[thread_position_in_grid]])
{
    if (pos.x >= outTexture.get_width() || pos.y >= outTexture.get_height()) return;

    // scale coordinates to between -1, 1
    float2 uv = float2(pos) / float2(outTexture.get_width(), outTexture.get_height());
    uv *= 2.0f;
    uv -= 1.0f;
    uv.x *= (float)outTexture.get_width() / (float)outTexture.get_height(); // aspect ratio fix
    uv.y = -uv.y; 
    float fov = 60.0f;
    float fovScale = tan(fov * 0.5f * 3.14159f / 180.0f); // 60 degree FOV
    
    float3 a = camera.position.xyz;
    float3 dir = camera.direction.xyz; //forward direction
    float3 sub = float3(0,1,0) * dot(dir, float3(0,1,0)); //gives how much the up vector points in the forward direction
    float3 up = normalize(float3(0, 1, 0) - sub); //removes the foward component
    float3 right = normalize(cross(dir, up));
    float3 b = normalize(dir + uv.x * fovScale * right + uv.y * fovScale * up); // move to the coordinate point in the new basis
    Ray ray;
    ray.position = float4(a, 0);
    ray.direction = float4(b, 0);

    float3 light = float3(0, 0, 0);
    int bounces = 30;
    float3 lightContribution = float3(1.0f, 1.0f, 1.0f);
    uint seed = pos.x + pos.y * outTexture.get_width() + frameIndex * 719393u; //719... is a prime to encourage randomness 

    // float3 lightDirection;
    // float lightIntensity;
    for (int i = 0; i < bounces; i ++) {

        metadata meta = TraceRay(ray, objects, sceneCount);

        if (meta.distance < 0) {

            // float3 skyColor = {0.4f, 0.7f, 1.0f};
            // light += skyColor * lightContribution * 0.2f;
            break;

        } else{
            // lightDirection = normalize(float3(-1.0f, -1.0f, -1.0f));

            // lightIntensity = max(dot(-lightDirection, meta.worldNormal.xyz), 0.0f); //scaled between 0 and 1
            int mat_idx = objects[meta.sphereObject].materialIndex;
            Material sphereMat = materials[mat_idx];

            // light += lightIntensity * sphereMat.color * multiplier;
            light += sphereMat.emissionIntensity * sphereMat.emissionColor.xyz * lightContribution;

            // lightContribution *= 0.75f;
            //
//   The reason simple multiplication works is that we're treating color as a per-channel energy scale factor. Each RGB channel is an
//   independent wavelength band, and the surface's albedo just says "what fraction of incoming energy survives this bounce per channel."
            lightContribution *= sphereMat.color.xyz; // this can be interpreted as the light being absorbed by the sphere


            // perfect mirror reflection
            float3 reflectDir = reflect(ray.direction.xyz, meta.worldNormal.xyz);
            // fully random scatter around the normal (lambertian diffuse)
            float3 randomDir = normalize(meta.worldNormal.xyz + randFloat3(seed, -1.0f, 1.0f));
            // blend between mirror and diffuse based on roughness (0 = mirror, 1 = fully diffuse)
            float3 diffuseDir = normalize(mix(reflectDir, randomDir, sphereMat.roughness));
            ray.position = meta.worldPosition + meta.worldNormal * 0.01;
            ray.direction = float4(diffuseDir, 0);
        }
    }
    uint idx = outTexture.get_width() * pos.y + pos.x;
    if (frameIndex == 1) accumulatedColor[idx] = float4(0);
    accumulatedColor[idx] = float4(accumulatedColor[idx].xyz + light, 0);
    float3 averaged = accumulatedColor[idx].xyz / float(frameIndex);
    float3 tonemapped = averaged / (averaged + 1.0f); // Reinhard tonemapping
    outTexture.write(float4(tonemapped, 1.0f), pos);


    

    
   
   
}
