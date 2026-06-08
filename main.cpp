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

#include "ray.h"
#include "scene.h"


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
    const uint32_t width = 2560, height = 1600;
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Raytracer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_METAL);
    SDL_MetalView view = SDL_Metal_CreateView(window);
    CA::MetalLayer* layer = (CA::MetalLayer*)SDL_Metal_GetLayer(view);
    layer->setDevice(device);
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    layer->setFramebufferOnly(false);
    layer->setDrawableSize(CGSizeMake(width, height));
    layer->setDisplaySyncEnabled(true);
    SDL_RaiseWindow(window);

    Camera camera(simd::float4{0.0f, 0.0f, 5.0f,0}, simd::float4{0.0f, 0.0f, 1.0f,0}, 0, 0);

    Scene scene;

    // red object sphere
    Material mObject = {};
    mObject.color = simd_make_float4(1, 0.2f, 0.2f, 0);
    mObject.roughness = 0.5f;
    scene.materials.push_back(mObject);


    //green object reflective
    Material mObjectReflective = {};
    mObjectReflective.color = simd_make_float4(0.2f, 1, 0.2f, 0);
    mObjectReflective.roughness = 0;
    scene.materials.push_back(mObjectReflective);

    // grey ground
    Material mGround = {};
    mGround.color = simd_make_float4(0.4f, 0.4f, 0.4f, 0);
    mGround.roughness = 1.0f;
    scene.materials.push_back(mGround);

    // white light source
    Material mLight = {};
    mLight.color = simd_make_float4(0, 0, 0, 0);
    mLight.roughness = 1.0f;
    mLight.emissionColor = simd_make_float4(1, 1, 1, 0);
    mLight.emissionIntensity = 35.0f;
    scene.materials.push_back(mLight);

    Sphere sObject;
    sObject.positionAndRadius = simd_make_float4(0, 0, 0, 0.8f);
    sObject.materialIndex = 0;

    Sphere reflectiveObject;
    reflectiveObject.positionAndRadius = simd_make_float4(2, 0, 0, 0.8f);
    reflectiveObject.materialIndex = 1;

    Sphere sGround;
    sGround.positionAndRadius = simd_make_float4(0, -101, 0, 100.0f);
    sGround.materialIndex = 2;

    Sphere sLight;
    sLight.positionAndRadius = simd_make_float4(-10, 1, 0, 5.0f); // light at 45 degrees, lower and larger
    sLight.materialIndex = 3;


    scene.objects.push_back(sObject);
    scene.objects.push_back(sGround);
    scene.objects.push_back(sLight);
    scene.objects.push_back(reflectiveObject);

    int sceneCount = scene.objects.size();

    
    // scene.objects.push_back(s3);

    int frameIndex = 1; //this represents how many frames we have accumulated, this is reset back to 1 whenever the camera moves 

    MTL::Buffer* cameraBuffer = device->newBuffer(sizeof(Ray), MTL::ResourceStorageModeShared);
    MTL::Buffer* objectBuffer = device->newBuffer(scene.objects.size() * sizeof(Sphere), MTL::ResourceStorageModeShared);
    MTL::Buffer* materialsBuffer = device->newBuffer(scene.materials.size() * sizeof(Material), MTL::ResourceStorageModeShared);
    MTL::Buffer* sceneCountBuffer = device->newBuffer(sizeof(int), MTL::ResourceStorageModeShared);
    MTL::Buffer* accumulatedColorBuffer = device->newBuffer(sizeof(simd::float4) * width*height, MTL::ResourceStorageModeShared);
    MTL::Buffer* frameIndexBuffer = device->newBuffer(sizeof(int), MTL::ResourceStorageModeShared);

    // 4. Render loop
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        const int factor = 10.0f;
        const float speed = 0.05f;
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W]) { camera.moveZ(speed); frameIndex = 1;}
        if (keys[SDL_SCANCODE_S]) { camera.moveZ(-speed); frameIndex = 1;}
        if (keys[SDL_SCANCODE_A]) { camera.moveX(-speed); frameIndex = 1;}
        if (keys[SDL_SCANCODE_D]) { camera.moveX(speed);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_UP]) { camera.pitch(speed * factor);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_DOWN]) { camera.pitch(-speed * factor);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_LEFT]) { camera.yaw(-speed * factor);  frameIndex = 1;memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}
        if (keys[SDL_SCANCODE_RIGHT]) { camera.yaw(speed * factor);  frameIndex = 1; memset(accumulatedColorBuffer->contents(), 0, sizeof(simd::float4) * width * height);}

        CA::MetalDrawable* drawable = layer->nextDrawable();
        if (!drawable) continue;

        MTL::CommandBuffer* cmdBuffer = commandQueue->commandBuffer();
        MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder();
        
        Ray* r = camera.getRay();
        
        memcpy(cameraBuffer->contents(), r, sizeof(Ray));
        memcpy(objectBuffer->contents(), scene.objects.data(), scene.objects.size()*sizeof(Sphere)); //passes in by copy not reference
        memcpy(materialsBuffer->contents(), scene.materials.data(), scene.materials.size()*sizeof(Material));
        memcpy(sceneCountBuffer->contents(), &sceneCount, sizeof(int));
        // memcpy(accumulatedColorBuffer->contents(), &accumulatedColor, sizeof(simd::float4)*width*height);
        memcpy(frameIndexBuffer->contents(), &frameIndex, sizeof(int));



        encoder->setComputePipelineState(pipeline);
        encoder->setTexture(drawable->texture(), 0);
        encoder->setBuffer(cameraBuffer, 0, 0);
        encoder->setBuffer(objectBuffer, 0, 1);
        encoder->setBuffer(materialsBuffer, 0, 2);
        encoder->setBuffer(sceneCountBuffer, 0, 3);
        encoder->setBuffer(accumulatedColorBuffer, 0, 4);
        encoder->setBuffer(frameIndexBuffer, 0, 5);


        encoder->dispatchThreads(MTL::Size(width, height, 1), MTL::Size(16, 16, 1));
        encoder->endEncoding();
        cmdBuffer->presentDrawable(drawable);
        cmdBuffer->commit();

        //update the frame index
        frameIndex += 1;
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
