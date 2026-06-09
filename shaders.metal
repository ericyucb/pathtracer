#include <metal_stdlib>
using namespace metal;



uint pcg(thread uint& state) {
    uint prev = state * 747796405u + 2891336453u;
    uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    state = prev;
    return (word >> 22u) ^ word;
};

float randFloat(thread uint& seed) {
    return float(pcg(seed)) / 4294967296.0f;
};

float randFloat(thread uint& seed, float lo, float hi){
    return lo + randFloat(seed) * (hi - lo);
}

float3 randFloat3(thread uint& seed, float lo, float hi) {
    return float3(
        randFloat(seed, lo, hi),
        randFloat(seed, lo, hi),
        randFloat(seed, lo, hi)
    );
};

float2 randomPointInCircle(thread uint& seed) {
    float angle = randFloat(seed) * 2 * 3.1415;
    float2 pointOnCircle = float2(cos(angle), sin(angle));
    return pointOnCircle * sqrt(randFloat(seed, 0, 1));
}

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
      float specularProbability;
      float indexOfRefraction;
      int isDielectric;
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
    Ray incomingRay;
};


metadata Hit(thread Ray& ray, float hitDistance, int sphereIndex, constant Sphere* objects){

    metadata meta;
    meta.distance = hitDistance;
    meta.sphereObject = sphereIndex;
    meta.worldPosition = ray.position + ray.direction * hitDistance;
    meta.incomingRay = ray;
    
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


float3 computeRefract(float n1, float n2, Ray i, float3 worldNormal);
float schlick_approximation(float n1, float n2, Ray i, float3 worldNormal, float3 refractedRay);

Ray hitGlass(float n1, float n2, thread uint& seed, metadata meta){ // this sends back either the reflected or refracted direction
    Ray i = meta.incomingRay;
    bool isOutside = dot(meta.incomingRay.direction.xyz, meta.worldNormal.xyz) < 0;
    float3 normal = isOutside ? meta.worldNormal.xyz : -meta.worldNormal.xyz;
    float n1_ = isOutside ? n1 : n2;
    float n2_ = isOutside ? n2 : n1;
    float3 refractedRay = computeRefract(n1_, n2_, i, normal);
    float p = schlick_approximation(n1_, n2_, i, normal, refractedRay);
    bool isReflection = p >= randFloat(seed) ? true : false;
    
    if (isReflection) {
        i.direction = float4(reflect(i.direction.xyz, normal), 0);
        i.position = meta.worldPosition + float4(normal * 0.001, 0);
    } else {
        i.direction = float4(refractedRay, 0);
        // always use -normal: when entering normal is outward so -normal pushes inside,
        // when exiting normal is inward so -normal pushes outside
        i.position = meta.worldPosition - float4(normal * 0.001, 0);
    }
    return i;
}

//calculate the refract vector direction using snells law
float3 computeRefract(float n1, float n2, Ray i, float3 worldNormal){
    float refractionRatio = n1/n2;
    float3 incident_i = i.direction.xyz;
    float incident_i_y = dot(-incident_i, worldNormal); // this ensures we get a positive dot product since cos (theta) is always positive
    float sin_square_term = pow(refractionRatio, 2) * (1- pow(incident_i_y, 2));
    if (sin_square_term > 1){
        return reflect(i.direction.xyz, worldNormal);
    }
    return refractionRatio * incident_i + worldNormal * (refractionRatio * incident_i_y - sqrt(1- sin_square_term));
}



// this tells you the fraction of light that gets reflected
float schlick_approximation(float n1, float n2, Ray i, float3 worldNormal, float3 refractedRay){ //ray is the incident ray
    float r0 = pow((n1-n2)/(n1+n2), 2);
    float cosine_theta = dot(-i.direction.xyz, worldNormal);
    if (n1 > n2 ){
        float sinT2 = pow(n1 / n2, 2) * (1.0 - pow(cosine_theta, 2));
        if (sinT2 > 1.0) return 1;  
        cosine_theta = sqrt(1-sinT2);
    }
    return r0 + (1-r0) * pow(1 - cosine_theta, 5);
}





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
    uint seed = pos.x + pos.y * outTexture.get_width() + frameIndex * 719393u; //719... is a prime to encourage randomness 

    // scale coordinates to between -1, 1
    float2 uv = float2(pos) / float2(outTexture.get_width(), outTexture.get_height());
    uv *= 2.0f;
    uv -= 1.0f;
    uv.x *= (float)outTexture.get_width() / (float)outTexture.get_height(); // aspect ratio fix
    uv.y = -uv.y; 
    // uv += float2(randFloat(seed) - 0.5f, randFloat(seed) - 0.5f) / float2(outTexture.get_width(), outTexture.get_height()); //anti aliasing
    //TODO move these hyperparameters into sphere class
    float fov = 45.0f;
    float fovScale = tan(fov * 0.5f * 3.14159f / 180.0f); // 60 degree FOV
    float blur_coefficient = 0.0001f;
    float focalDistance = 5.0f;
    float3 focalPoint = camera.position.xyz + camera.direction.xyz * focalDistance;
    float2 jitter_depth = randomPointInCircle(seed) * blur_coefficient;

    float3 a = float3(camera.position.x + jitter_depth.x, camera.position.y + jitter_depth.y, camera.position.z); // depth of view
    float3 dir = normalize(focalPoint- a); //forward direction (should be normalized)
    float3 up = float3(0,1,0) - dot(float3(0,1,0), normalize(dir))* normalize(dir); //the projection of up onto the new direction
    float3 right = normalize(cross(dir, up));
    float3 b = normalize(dir + uv.x * fovScale * right + uv.y * fovScale * up); // move to the coordinate point in the new basis
    Ray ray;
    ray.position = float4(a, 0);
    ray.direction = float4(b, 0);

    float3 light = float3(0, 0, 0);
    int bounces = 10;
    float3 lightContribution = float3(1.0f, 1.0f, 1.0f);

    // float3 lightDirection;
    // float lightIntensity;
    for (int i = 0; i < bounces; i ++) {

        metadata meta = TraceRay(ray, objects, sceneCount);

        if (meta.distance < 0) {

            // float3 skyColor = {0.4f, 0.7f, 1.0f};
            // light += skyColor * lightContribution * 0.3f;
            break;

        }
        
        int mat_idx = objects[meta.sphereObject].materialIndex;
        Material sphereMat = materials[mat_idx];

        if (sphereMat.isDielectric) {
            ray = hitGlass(1.0, sphereMat.indexOfRefraction, seed, meta);

        } else {

            // light += lightIntensity * sphereMat.color * multiplier;
            light += sphereMat.emissionIntensity * sphereMat.emissionColor.xyz * lightContribution;

            bool isDiffuse = sphereMat.specularProbability < randFloat(seed); // materials has probability p of reflecting specular and 1-p of diffuse

    //   The reason simple multiplication works is that we're treating color as a per-channel energy scale factor. Each RGB channel is an
    //   independent wavelength band, and the surface's albedo just says "what fraction of incoming energy survives this bounce per channel."
            lightContribution *= sphereMat.color.xyz; // this can be interpreted as the light being absorbed by the sphere
            // perfect mirror reflection
            float3 specularDir = reflect(ray.direction.xyz, meta.worldNormal.xyz);

            // fully random scatter around the normal (cosine weighted ditribution)
            float3 diffuseDir = normalize(meta.worldNormal.xyz + randFloat3(seed, -1.0f, 1.0f));
            // blend between mirror and diffuse based on roughness (0 = mirror, 1 = fully diffuse)
            float3 dir; 

            if (isDiffuse) {
                dir = mix(specularDir, diffuseDir, sphereMat.roughness); // this is the general direction defined by roughness the more rough the less specular
            } else{
                dir = specularDir; // this represents a full specular reflection with a probability p
            }

            // calculate the new position and direction
            ray.position = meta.worldPosition + meta.worldNormal * 0.01; 
            ray.direction = float4(dir, 0);

        }
    }
    uint idx = outTexture.get_width() * pos.y + pos.x;
    if (frameIndex == 1) accumulatedColor[idx] = float4(0);
    accumulatedColor[idx] = float4(accumulatedColor[idx].xyz + light, 0);
    float3 averaged = accumulatedColor[idx].xyz / float(frameIndex);
    float3 tonemapped = averaged / (averaged + 1.0f); // Reinhard tonemapping
    outTexture.write(float4(tonemapped, 1.0f), pos);
}
