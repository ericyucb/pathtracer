#include <metal_stdlib>
using namespace metal;


//UTILS

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
};

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
};


//shader_structs
struct Ray {
    float4 position;
    float4 direction = float4{0,0,1,0};
};

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
    int meshIndex; //this references which mesh the triangle is part of
    int padding[2];

};


struct Material {
    float4 albetoAndRoughness; // xyz = rgb, w roughness
    float4 emissionColor; //xyz = emission color, w = emission Intensity
    float4 params; //metallic, specularProbability, indexOfRefraction, unused
    int type; //0 = lambertian, 1 = dielectric, 2 = metallic
    int padding[3];
    
};


struct metadata {
    float3 worldPosition;
    Ray incomingRay;
    float3 worldNormal;
    float distance;
    int objectIndex;
    int objectType; //1 = sphere, 2 = triangle
    int padding[1];
  
};

struct CameraMetadata{
    float fov;
    float blurCoefficient;
    float focalDistance;
    Ray cameraOrientation; //ray takes up 32 bytes
    int padding[1];
};

struct BVHNode { // each node should encode its bounding box as well as all primitives contained within it
    float3 minP;
    float3 maxP;
    int firstPrimitiveIndex;
    int primitiveCount;
    int leftChildIndex; // if leftChildIndex = 0 it is a leaf
};

struct BVHPrimitive {
    float3 centroid;
    int objectType;
    int objectIndex;
    float3 minP;
    float3 maxP;
};

struct PrimitiveRef {
    int objectType;
    int objectIndex;
};

//Helper Functions
metadata Hit(thread Ray& ray, float hitDistance, int objectIndex, int objectType, device const Sphere* spheres, device const Triangle* triangles){

    metadata meta;
    meta.distance = hitDistance;
    meta.objectIndex = objectIndex;
    meta.worldPosition = (ray.position + ray.direction * hitDistance).xyz;
    meta.incomingRay = ray;
    meta.objectType = objectType;

    if (objectType == 1) {
        float3 center = spheres[objectIndex].positionAndRadius.xyz;
        meta.worldNormal = normalize(meta.worldPosition.xyz - center);
    } else if(objectType == 2) {
        Triangle triangle = triangles[objectIndex];
        float3 v1 = triangle.p2 - triangle.p1;
        float3 v2 = triangle.p3 - triangle.p1;
        meta.worldNormal = normalize(cross(v1, v2));
    }
    
    return meta;

};

metadata Miss(){

    metadata meta = {};
    meta.distance = -1.0f;

    return meta;
};

void updateSphereDistance(float3 a, float3 b, int sphereIndex, device const Sphere* spheres, thread float& closestDistance, thread int& closestObject, thread int& closestObjectType) {

    Sphere sphere = spheres[sphereIndex];
    float3 center = sphere.positionAndRadius.xyz;
    float r = sphere.positionAndRadius.w;

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

            if(closestDistance == INFINITY or t1 < closestDistance) {
                closestDistance = t1;
                closestObject = sphereIndex;
                closestObjectType = 1;
            }
            
        }else if (t2 > 0) {
            if(closestDistance == INFINITY or t2 < closestDistance) {
                closestDistance = t2;
                closestObject = sphereIndex;
                closestObjectType = 1;
            }
        } 

    } 
}

void updateTriangleDistance(float3 a, float3 b, int triangleIndex, device const Triangle* triangles, thread float& closestDistance, thread int& closestObject, thread int& closestObjectType){
    Triangle triangle = triangles[triangleIndex];
    //first check for intersection with plane
    float3 v1 = triangle.p2 - triangle.p1;
    float3 v2 = triangle.p3 - triangle.p1;
    float3 planeNormal =  normalize(cross(v1, v2));
    if (dot(planeNormal, b) == 0) {
        return;
    }
    //moller-trumble ray intersection
    //O + tD = A + u(B-A) + v(C-A) where O = ray origin D= ray direction, A, B, C are points on triangle
    //(B-A) = v1, (C-A) = v2
    
    //We are solving the system of equations, where u and v are the barycentric coordinates
    //[-D v1 v2] @ [t u v] = O-A
    //We can solve this using cramers rule leveraging the fact that the determinat of a 3x3 matrix is the triple product dot(x, cross(y, z))
    //first check if it intersects with triangle at all, it doens't intersect if u or v are not within the range [0, 1]
    float3 trianglePointA = triangle.p1;

    float3 rayOriginMinusPointA = a - trianglePointA;

    //find the determinant of [-D v1 v2] which reduces to the triple cross product
    float detM = dot(-b, cross(v1, v2));

    //solve for u first U = detu/detM
    //detU = |[-D, O-A, v2]|
    float detU = dot(-b, cross(rayOriginMinusPointA, v2));
    float u = detU/detM;

    if (u < 0 or u > 1) {
        return;
    }

    //solve for v
    //detV = |[-D, v1, O-A]|
    float detV = dot(-b, cross(v1, rayOriginMinusPointA));
    float v = detV/detM;

    if (v<0 or v > 1){
        return;
    }

    if (u + v > 1) {
        return;
    }

    //solve for t
    //detT = |[O-A, v1, v2]|
    float detT = dot(rayOriginMinusPointA, cross(v1, v2));
    float t = detT/detM;

    //if t<0 the triangle is behind the camera
    if (t < 0){
        return;
    }

    float distance = t;

    if (closestDistance == INFINITY or distance < closestDistance) {
        closestDistance = distance;
        closestObject = triangleIndex;
        closestObjectType = 2;
    }
}

// metadata TraceRay(thread Ray& ray, device const Sphere* spheres, device const Triangle* triangles, device const Mesh* meshes,  int sphereCount, int triangleCount){
    

//     float closestDistance = -1;
//     int closestObject = -1;
//     int closestObjectType;
//     float3 a = ray.position.xyz;
//     float3 b = ray.direction.xyz;

//     for (int sphereIndex = 0; sphereIndex < sphereCount; sphereIndex++) {
//         updateSphereDistance(a, b, sphereIndex, closestDistance, closestObject, closestObjectType);
//     }

//     for (int triangleIndex = 0; triangleIndex < triangleCount; triangleIndex++){
//        updateTriangleDistance(a, b, triangleIndex, closestDistance, closestObject, closestObjectType);
//     }

//     if(closestDistance >= 0) { //only render if we intersected some primitive

//         metadata payload = Hit(ray, closestDistance, closestObject, closestObjectType, spheres, triangles);
//         return payload;

//     } else{

//         metadata payload = Miss();
//         return payload;

//     }

// };


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
        i.position = float4(meta.worldPosition, 0) + float4(normal * 0.001, 0);
    } else {
        i.direction = float4(refractedRay, 0);
        // always use -normal: when entering normal is outward so -normal pushes inside,
        // when exiting normal is inward so -normal pushes outside
        i.position = float4(meta.worldPosition, 0) - float4(normal * 0.001, 0);
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

// float intersectBoxDist(thread Ray* ray, float3 minP, float3 maxP, float3 ray_dir_recipricol){
//     float3 t_low = (minP-ray->position.xyz)*ray_dir_recipricol; // lowx lowy lowz
//     float3 t_high = (maxP-ray->position.xyz)*ray_dir_recipricol;
//     float3 t_close = min(t_low, t_high);
//     float3 t_far =  max(t_low, t_high);

//     float t_last_entry = max(t_close.x, max(t_close.y, t_close.z));
//     float t_first_exit = min(t_far.x, min(t_far.y, t_far.z));

//     if (t_last_entry < t_first_exit){
//         return t_last_entry;
//     }
//     return -1.0f;
    
// }

float intersectBoxDist(thread Ray* ray, float3 minP, float3 maxP, float3 ray_dir_recipricol){
    float3 t_low = (minP - ray->position.xyz) * ray_dir_recipricol;
    float3 t_high = (maxP - ray->position.xyz) * ray_dir_recipricol;
    
    float3 t_close = min(t_low, t_high);
    float3 t_far =  max(t_low, t_high);

    float t_last_entry = max(t_close.x, max(t_close.y, t_close.z));
    float t_first_exit = min(t_far.x, min(t_far.y, t_far.z));

    // Changed to <= to handle perfectly flat bounding boxes
    if (t_last_entry <= t_first_exit && t_first_exit > 0.0f){
        // THE FIX: If the entry point is behind us, but the exit is in front, 
        // we are inside the box. Return 0.0f so it doesn't get culled!
        return max(0.0, t_last_entry);
    }
    return -1.0f;
}

metadata traceBVH(thread Ray* ray, device const PrimitiveRef* primitives, device const BVHNode* bvh, device const Sphere* spheres, device const Triangle* triangles) { //given a ray search through bvh and return the required nodes
    //ray box intersection algorithm (slab method)
    float closestDistance = INFINITY;
    int closestObject = -1;
    int closestObjectType;
    float3 a = ray->position.xyz;
    float3 b = ray->direction.xyz;
    
    float3 ray_dir_recipricol = 1/ray->direction.xyz;

    int stack[64];
    int currIndex = 0;
    stack[currIndex++] = 0; // add the root onto the stack


    while(currIndex != 0) {
        int currBVHNodeIndex = stack[--currIndex];
        BVHNode curr = bvh[currBVHNodeIndex];

        float dist = intersectBoxDist(ray, curr.minP, curr.maxP, ray_dir_recipricol);
        if (dist < 0.0f or dist > closestDistance){ // theres no point in searching further if we found a closer box previously
            continue;
        } else{
            if (curr.leftChildIndex != 0) {
                //if there is children nodes, make sure the closer box is checked first
                BVHNode leftChild = bvh[curr.leftChildIndex];
                float leftChildDist = intersectBoxDist(ray, leftChild.minP, leftChild.maxP, ray_dir_recipricol);
                BVHNode rightChild = bvh[curr.leftChildIndex + 1];
                float rightChildDist = intersectBoxDist(ray, rightChild.minP, rightChild.maxP, ray_dir_recipricol);
                if (leftChildDist < rightChildDist) {
                    stack[currIndex++] = curr.leftChildIndex + 1;
                    stack[currIndex++] = curr.leftChildIndex;
                } else {
                    stack[currIndex++] = curr.leftChildIndex;
                    stack[currIndex++] = curr.leftChildIndex + 1;
                }
                
            }
            if (curr.leftChildIndex == 0) { // leaf node
                //keep track of all primitives 
                for (int i = curr.firstPrimitiveIndex; i< curr.firstPrimitiveIndex + curr.primitiveCount; i++) {
                    PrimitiveRef primitive =  primitives[i];
                    if (primitive.objectType == 1) {
                        int sphereIndex = primitive.objectIndex;
                        updateSphereDistance(a, b, sphereIndex, spheres, closestDistance, closestObject, closestObjectType);
                    } else if (primitive.objectType == 2) {
                        int triangleIndex = primitive.objectIndex;
                        updateTriangleDistance(a, b, triangleIndex, triangles, closestDistance, closestObject, closestObjectType);
                    }
                }
            }
            
        }
    }

    if(closestObject > -1) { //only render if we intersected some primitive

        metadata payload = Hit(*ray, closestDistance, closestObject, closestObjectType, spheres, triangles);
        return payload;

    } else{

        metadata payload = Miss();
        return payload;

    }

}

//per pixel
kernel void render_kernel(
    texture2d<float, access::write> outTexture       [[texture(0)]],
    constant CameraMetadata&        camera           [[buffer(0)]],
    device const Sphere*            spheres          [[buffer(1)]],
    constant int&                   sphereCount      [[buffer(2)]],
    device const Triangle*          triangles        [[buffer(3)]],
    constant int&                   triangleCount    [[buffer(4)]],
    device const Mesh*              meshes           [[buffer(5)]],
    constant Material*              materials        [[buffer(6)]],
    constant int&                   frameIndex       [[buffer(7)]],
    device float4*                  accumulatedColor [[buffer(8)]],
    constant int&                   meshCount        [[buffer(9)]],
    device const BVHNode*           bvhNodes         [[buffer(10)]],
    device const PrimitiveRef*      primitiveRef     [[buffer(11)]],
    uint2 pos [[thread_position_in_grid]])
{
    if (pos.x >= outTexture.get_width() || pos.y >= outTexture.get_height()) return;
    uint seed = pos.x + pos.y * outTexture.get_width() + frameIndex * 719393u; //719... is a prime to encourage randomness 

    // jitter the sample within the pixel (anti-aliasing), then scale coordinates to between -1, 1
    float2 jitter = float2(randFloat(seed) - 0.5f, randFloat(seed) - 0.5f);
    float2 uv = (float2(pos) + jitter) / float2(outTexture.get_width(), outTexture.get_height());
    uv *= 2.0f;
    uv -= 1.0f;
    uv.x *= (float)outTexture.get_width() / (float)outTexture.get_height(); // aspect ratio fix
    uv.y = -uv.y;
    //TODO move these hyperparameters into sphere class
    float fov = camera.fov;
    float fovScale = tan(fov * 0.5f * 3.14159f / 180.0f); // 60 degree FOV
    float blur_coefficient = camera.blurCoefficient;
    float focalDistance = camera.focalDistance;
    float3 cameraPosition = camera.cameraOrientation.position.xyz;
    float3 cameraDirection = camera.cameraOrientation.direction.xyz;
    float3 focalPoint = cameraPosition + cameraDirection * focalDistance;
    float2 jitter_depth = randomPointInCircle(seed) * blur_coefficient;

    float3 a = cameraPosition + float3(jitter_depth, 0); 
    float3 dir = normalize(focalPoint - a); //forward direction (should be normalized)
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
    for (int i = 0; i < bounces; i++) {

        metadata meta = traceBVH(&ray, primitiveRef, bvhNodes, spheres, triangles);
        

        if (meta.distance < 0) {
            // float3 skyColor = {0.4f, 0.7f, 1.0f};
            // light += skyColor * lightContribution * 0.3f;
            break;
        }

        int mat_idx;
        Material objectMaterial;
        if (meta.objectType == 1){
            mat_idx = spheres[meta.objectIndex].materialIndex;
        } else if(meta.objectType == 2) {
            mat_idx = triangles[meta.objectIndex].materialIndex;
        }
        objectMaterial = materials[mat_idx];


       
        // float4 params; //metallic, specularProbability, indexOfRefraction, unused
        float3 albeto = objectMaterial.albetoAndRoughness.xyz;
        float roughness = objectMaterial.albetoAndRoughness.w;
        float metallic = objectMaterial.params.x;
        float specularProbability = objectMaterial.params.y;
        float IOR = objectMaterial.params.z;
        float3 emissionColor = objectMaterial.emissionColor.xyz;
        float emissionIntensity = objectMaterial.emissionColor.w;
        float materialType = objectMaterial.type;
        float3 rayDirection = ray.direction.xyz;
        
        if (materialType == 1) { //checks if object is dielectric (glass)
            ray = hitGlass(1.0, IOR, seed, meta);

        } else {

            // light += lightIntensity * objectMaterial.color * multiplier;
            light += emissionColor * emissionIntensity * lightContribution;

            bool isDiffuse = specularProbability < randFloat(seed); // materials has probability p of reflecting specular and 1-p of diffuse

            // The reason simple multiplication works is that we're treating color as a per-channel energy scale factor. Each RGB channel is an
            // independent wavelength band, and the surface's albedo just says "what fraction of incoming energy survives this bounce per channel."
            lightContribution *= albeto; // this can be interpreted as the light being absorbed by the sphere
            // perfect mirror reflection
            float3 specularDir = reflect(rayDirection, meta.worldNormal);

            // fully random scatter around the normal (cosine weighted ditribution)
            float3 diffuseDir = normalize(meta.worldNormal + randFloat3(seed, -1.0f, 1.0f));
            // blend between mirror and diffuse based on roughness (0 = mirror, 1 = fully diffuse)
            float3 dir; 

            if (isDiffuse) {
                dir = mix(specularDir, diffuseDir, roughness); // this is the general direction defined by roughness the more rough the less specular
            } else{
                dir = specularDir; // this represents a full specular reflection with a probability p
            }

            // calculate the new position and direction
            ray.position = float4(meta.worldPosition + meta.worldNormal * 0.01, 0); 
            ray.direction = float4(dir, 0);

        }
    }
    uint idx = outTexture.get_width() * pos.y + pos.x;
    if (frameIndex == 1) accumulatedColor[idx] = float4(0);
    accumulatedColor[idx] = float4(accumulatedColor[idx].xyz + light, 0);
    float3 averaged = accumulatedColor[idx].xyz / float(frameIndex);
    float3 tonemapped = averaged / (averaged + 1.0f); // Reinhard tonemapping
    outTexture.write(float4(tonemapped, 1.0f), pos);
    // outTexture.write(float4(1.0, 0.0, 0.0, 1.0), pos);
}
