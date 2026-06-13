#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <simd/simd.h>

#include "camera.h"

#include "scene.h"
#include "bvh.h"
#include "obj_loader.h"

using namespace simd;


std::string ReadShaderSource(const std::string& filePath) {
    std::ifstream file(filePath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    //SETUP

    // 1. GPU device + command queue
    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    if (!device) { std::cerr << "No Metal device found.\n"; return -1; }
    std::cout << "Running on: " << device->name()->utf8String() << "\n";

    MTL::CommandQueue* commandQueue = device->newCommandQueue();

    // 2. Compile shader
    std::string shaderSource = ReadShaderSource("/Users/ericyu/Documents/raytracing/shaders.metal"); //reads shaders.metal from disk into a plain C++ string
    NS::Error* error = nullptr;
    NS::String* sourceStr = NS::String::string(shaderSource.c_str(), NS::UTF8StringEncoding);  //converts it from a C++ string to a NS::String, which is the string type that metal's api expects
    MTL::Library* library = device->newLibrary(sourceStr, nullptr, &error); // compiles the metal shader source code on the spot and return a Library, which is a compiled collection of GPU functions.
    if (!library) { //if the compilation fials print the error message and exit
        std::cerr << "Shader error: " << error->localizedDescription()->utf8String() << "\n";
        return -1;
    }

    NS::String* kernelName = NS::String::string("render_kernel", NS::UTF8StringEncoding); // the libr
    MTL::Function* kernelFunc = library->newFunction(kernelName); // the library contains all compiled functions from shaders.metal. newFunction looks up the function "kernelName" by name which is exactly defined in shaders.metal
    //  Takes that function and compiles it further into a pipeline — a fully GPU-optimized, ready-to-execute program. This is where Metal does final optimization for your specific GPU hardware.
    MTL::ComputePipelineState* pipeline = device->newComputePipelineState(kernelFunc, &error); // adds that function to the pipline


    // 3. SDL window + Metal layer
    SDL_Init(SDL_INIT_VIDEO);

 
    SDL_Rect displayBounds;
    SDL_GetDisplayBounds(0, &displayBounds);
    SDL_Window* window = SDL_CreateWindow("Raytracer",
        displayBounds.x, displayBounds.y,
        displayBounds.w, displayBounds.h,
        SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
    SDL_MetalView view = SDL_Metal_CreateView(window);
    CA::MetalLayer* layer = (CA::MetalLayer*)SDL_Metal_GetLayer(view);
    layer->setDevice(device);
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    layer->setFramebufferOnly(false);
    layer->setDisplaySyncEnabled(true);
    SDL_RaiseWindow(window);
    SDL_SetWindowInputFocus(window); // borderless windows don't always get key focus on macOS

    int drawW, drawH;
    SDL_Metal_GetDrawableSize(window, &drawW, &drawH);
    layer->setDrawableSize(CGSizeMake(drawW, drawH));
    const uint32_t width = (uint32_t)drawW, height = (uint32_t)drawH;

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &dm);
  

    Camera camera(float4{0.0f, 0.0f, 5.0f,0}, float4{0.0f, 0.0f, 1.0f,0}, 0, 0, 45.0f, 0.07, 3.0f);

    Scene scene;

  
    // === Cornell box room enclosing the bunny ===
    float roomX0 = -3.0f, roomX1 = 3.0f;  // left / right walls
    float roomY0 = -1.0f, roomY1 = 5.0f;  // floor / ceiling
    float roomZ0 = 6.0f,  roomZ1 = 12.0f; // open front (near camera) / back wall

    Material mWhite = {};
    mWhite.albetoAndRoughness = simd_make_float4(0.73f, 0.73f, 0.73f, 1.0f);
    mWhite.type = 0;
    scene.materials.push_back(mWhite);
    int whiteMat = (int)scene.materials.size() - 1;

    Material mRed = {};
    mRed.albetoAndRoughness = simd_make_float4(0.65f, 0.05f, 0.05f, 1.0f);
    mRed.type = 0;
    scene.materials.push_back(mRed);
    int redMat = (int)scene.materials.size() - 1;

    Material mGreen = {};
    mGreen.albetoAndRoughness = simd_make_float4(0.12f, 0.45f, 0.15f, 1.0f);
    mGreen.type = 0;
    scene.materials.push_back(mGreen);
    int greenMat = (int)scene.materials.size() - 1;

    Material mRoomLight = {};
    mRoomLight.albetoAndRoughness = simd_make_float4(0, 0, 0, 1.0f);
    mRoomLight.emissionColor = simd_make_float4(1, 1, 1, 50.0f);
    mRoomLight.type = 0;
    scene.materials.push_back(mRoomLight);
    int roomLightMat = (int)scene.materials.size() - 1;

    auto addTri = [&](simd::float3 p1, simd::float3 p2, simd::float3 p3, int matIdx) {
        Triangle t = {};
        t.p1 = p1; t.p2 = p2; t.p3 = p3;
        t.materialIndex = matIdx;
        scene.triangles.push_back(t);
    };

    // Floor (normal +y)
    addTri(simd_make_float3(roomX0, roomY0, roomZ0), simd_make_float3(roomX0, roomY0, roomZ1), simd_make_float3(roomX1, roomY0, roomZ0), whiteMat);
    addTri(simd_make_float3(roomX1, roomY0, roomZ0), simd_make_float3(roomX0, roomY0, roomZ1), simd_make_float3(roomX1, roomY0, roomZ1), whiteMat);

    // Ceiling (normal -y)
    addTri(simd_make_float3(roomX0, roomY1, roomZ0), simd_make_float3(roomX1, roomY1, roomZ0), simd_make_float3(roomX0, roomY1, roomZ1), whiteMat);
    addTri(simd_make_float3(roomX1, roomY1, roomZ0), simd_make_float3(roomX1, roomY1, roomZ1), simd_make_float3(roomX0, roomY1, roomZ1), whiteMat);

    // Back wall (far side, normal -z, faces the camera through the open near side)
    addTri(simd_make_float3(roomX0, roomY0, roomZ1), simd_make_float3(roomX0, roomY1, roomZ1), simd_make_float3(roomX1, roomY0, roomZ1), whiteMat);
    addTri(simd_make_float3(roomX1, roomY0, roomZ1), simd_make_float3(roomX0, roomY1, roomZ1), simd_make_float3(roomX1, roomY1, roomZ1), whiteMat);

    // Left wall (normal +x), red
    addTri(simd_make_float3(roomX0, roomY0, roomZ0), simd_make_float3(roomX0, roomY1, roomZ0), simd_make_float3(roomX0, roomY0, roomZ1), redMat);
    addTri(simd_make_float3(roomX0, roomY0, roomZ1), simd_make_float3(roomX0, roomY1, roomZ0), simd_make_float3(roomX0, roomY1, roomZ1), redMat);

    // Right wall (normal -x), green
    addTri(simd_make_float3(roomX1, roomY0, roomZ0), simd_make_float3(roomX1, roomY0, roomZ1), simd_make_float3(roomX1, roomY1, roomZ0), greenMat);
    addTri(simd_make_float3(roomX1, roomY0, roomZ1), simd_make_float3(roomX1, roomY1, roomZ1), simd_make_float3(roomX1, roomY1, roomZ0), greenMat);

    // Light panel inset into the ceiling (normal -y)
    float lightInset = 1.0f;
    float lightY = roomY1 - 0.02f;
    addTri(simd_make_float3(roomX0 + lightInset, lightY, roomZ0 + lightInset), simd_make_float3(roomX1 - lightInset, lightY, roomZ0 + lightInset), simd_make_float3(roomX0 + lightInset, lightY, roomZ1 - lightInset), roomLightMat);
    addTri(simd_make_float3(roomX1 - lightInset, lightY, roomZ0 + lightInset), simd_make_float3(roomX1 - lightInset, lightY, roomZ1 - lightInset), simd_make_float3(roomX0 + lightInset, lightY, roomZ1 - lightInset), roomLightMat);

    // bunny material (glass)
    Material mBunny = {};
    mBunny.albetoAndRoughness = simd_make_float4(0.8f, 0.8f, 0.8f, 1.0f);
    mBunny.params = simd_make_float4(0, 0, 1.5f, 0); // IOR = 1.5 (glass)
    mBunny.type = 1; // dielectric
    scene.materials.push_back(mBunny);
    int bunnyMat = (int)scene.materials.size() - 1;

    // bunny sits on the floor (roomY0), centered in the room
    loadOBJ("/Users/ericyu/Documents/raytracing/bunny.obj", scene, bunnyMat, float3{0, -1.33f, 9}, 10.0f);

    int sphereCount = scene.spheres.size();
    int triangleCount = scene.triangles.size();
    int meshCount = scene.meshes.size();

    BVH bvh;
    BVHData bvhData = bvh.buildBVH(scene.spheres.data(), scene.triangles.data(), sphereCount, triangleCount);
    
    

    int frameIndex = 1; //this represents how many frames we have accumulated, this is reset back to 1 whenever the camera moves 

    MTL::Buffer* cameraBuffer = device->newBuffer(sizeof(CameraMetadata), MTL::ResourceStorageModeShared);
    MTL::Buffer* sphereBuffer = device->newBuffer(scene.spheres.size() * sizeof(Sphere), MTL::ResourceStorageModeShared);
    MTL::Buffer* sphereCountBuffer = device->newBuffer(sizeof(int), MTL::ResourceStorageModeShared);

    MTL::Buffer* triangleBuffer = device->newBuffer(scene.triangles.size() * sizeof(Triangle), MTL::ResourceStorageModeShared);
    MTL::Buffer* triangleCountBuffer = device->newBuffer(sizeof(int), MTL::ResourceStorageModeShared);

    MTL::Buffer* meshBuffer = device->newBuffer(scene.meshes.size() * sizeof(Mesh), MTL::ResourceStorageModeShared);

    MTL::Buffer* materialsBuffer = device->newBuffer(scene.materials.size() * sizeof(Material), MTL::ResourceStorageModeShared);
    MTL::Buffer* accumulatedColorBuffer = device->newBuffer(sizeof(simd::float4) * width*height, MTL::ResourceStorageModeShared);
    MTL::Buffer* frameIndexBuffer = device->newBuffer(sizeof(int), MTL::ResourceStorageModeShared);

    MTL::Buffer* meshCountBuffer = device->newBuffer(sizeof(int), MTL::ResourceStorageModeShared);

    MTL::Buffer* bvhNodeBuffer = device->newBuffer(bvhData.bvhNodes.size() * sizeof(BVHNode), MTL::ResourceStorageModeShared);
    MTL::Buffer* primitiveRefBuffer = device->newBuffer(bvhData.primitiveRef.size() * sizeof(PrimitiveRef), MTL::ResourceStorageModeShared);

    memcpy(bvhNodeBuffer->contents(), bvhData.bvhNodes.data(), bvhData.bvhNodes.size() * sizeof(BVHNode)); // BVH is built once, so this only needs to happen once
    memcpy(primitiveRefBuffer->contents(), bvhData.primitiveRef.data(), bvhData.primitiveRef.size() * sizeof(PrimitiveRef));

    // scene geometry/materials are static, so these only need to be copied once
    memcpy(sphereBuffer->contents(), scene.spheres.data(), scene.spheres.size()*sizeof(Sphere));
    memcpy(sphereCountBuffer->contents(), &sphereCount, sizeof(int));
    memcpy(triangleBuffer->contents(), scene.triangles.data(), scene.triangles.size()*sizeof(Triangle));
    memcpy(triangleCountBuffer->contents(), &triangleCount, sizeof(int));
    memcpy(meshBuffer->contents(), scene.meshes.data(), scene.meshes.size()*sizeof(Mesh));
    memcpy(materialsBuffer->contents(), scene.materials.data(), scene.materials.size()*sizeof(Material));
    memcpy(meshCountBuffer->contents(), &meshCount, sizeof(int));

    // 4. Render loop
    bool running = true;
    SDL_Event event;
    while (running) {
        NS::AutoreleasePool* framePool = NS::AutoreleasePool::alloc()->init();

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        const int factor = 10.0f;
        const float speed = 0.05f;
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W]) { camera.moveZ(speed); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_S]) { camera.moveZ(-speed); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_A]) { camera.moveX(-speed); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_D]) { camera.moveX(speed);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_UP]) { camera.pitch(speed * factor);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_DOWN]) { camera.pitch(-speed * factor);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_LEFT]) { camera.yaw(-speed * factor);  frameIndex = 1;memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_RIGHT]) { camera.yaw(speed * factor);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}

        // [ / ] adjust depth-of-field blur amount, - / = adjust focal distance
        const float blurStep = 0.005f;
        const float focalStep = 0.1f;
        if (keys[SDL_SCANCODE_LEFTBRACKET]) { camera.adjustBlur(-blurStep); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height); std::cout << "blur: " << camera.getBlurCoefficient() << "\n"; }
        if (keys[SDL_SCANCODE_RIGHTBRACKET]) { camera.adjustBlur(blurStep); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height); std::cout << "blur: " << camera.getBlurCoefficient() << "\n"; }
        if (keys[SDL_SCANCODE_MINUS]) { camera.adjustFocalDistance(-focalStep); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height); std::cout << "focal distance: " << camera.getFocalDistance() << "\n"; }
        if (keys[SDL_SCANCODE_EQUALS]) { camera.adjustFocalDistance(focalStep); frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height); std::cout << "focal distance: " << camera.getFocalDistance() << "\n"; }

        CA::MetalDrawable* drawable = layer->nextDrawable();
        if (!drawable) { framePool->release(); continue; }

        MTL::CommandBuffer* cmdBuffer = commandQueue->commandBuffer();
        MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder();
        
        CameraMetadata cameraMetadata = camera.getCameraStruct();

        memcpy(cameraBuffer->contents(), &cameraMetadata, sizeof(CameraMetadata));
        memcpy(frameIndexBuffer->contents(), &frameIndex, sizeof(int));

        encoder->setComputePipelineState(pipeline);
        encoder->setTexture(drawable->texture(), 0);
        encoder->setBuffer(cameraBuffer, 0, 0);
        encoder->setBuffer(sphereBuffer, 0, 1);
        encoder->setBuffer(sphereCountBuffer, 0, 2);
        encoder->setBuffer(triangleBuffer, 0, 3);
        encoder->setBuffer(triangleCountBuffer, 0, 4);
        encoder->setBuffer(meshBuffer, 0, 5);
        encoder->setBuffer(materialsBuffer, 0, 6);
        encoder->setBuffer(frameIndexBuffer, 0, 7);
        encoder->setBuffer(accumulatedColorBuffer, 0, 8);
        encoder->setBuffer(meshCountBuffer, 0, 9);
        encoder->setBuffer(bvhNodeBuffer, 0, 10);
        encoder->setBuffer(primitiveRefBuffer, 0, 11);

        encoder->dispatchThreads(MTL::Size(width, height, 1), MTL::Size(16, 16, 1));
        encoder->endEncoding();
        cmdBuffer->presentDrawable(drawable);
        cmdBuffer->commit();

        //update the frame index
        frameIndex += 1;
        framePool->release();
    }

    // 5. Cleanup
    pipeline->release();
    kernelFunc->release();
    library->release();
    commandQueue->release();
    device->release();
    SDL_Metal_DestroyView(view);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
