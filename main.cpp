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
    SDL_Window* window = SDL_CreateWindow("Raytracer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        0, 0, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_MetalView view = SDL_Metal_CreateView(window);
    CA::MetalLayer* layer = (CA::MetalLayer*)SDL_Metal_GetLayer(view);
    layer->setDevice(device);
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    layer->setFramebufferOnly(false);
    layer->setDisplaySyncEnabled(true);
    SDL_RaiseWindow(window);

    int drawW, drawH;
    SDL_Metal_GetDrawableSize(window, &drawW, &drawH);
    layer->setDrawableSize(CGSizeMake(drawW, drawH));
    const uint32_t width = (uint32_t)drawW, height = (uint32_t)drawH;

    Camera camera(float4{0.0f, 0.0f, 5.0f,0}, float4{0.0f, 0.0f, 1.0f,0}, 0, 0, 45.0f, 0.001, 5.0f);

    Scene scene;


    // grey ground
    Material mGround = {};
    mGround.albetoAndRoughness = simd_make_float4(0.4f, 0.4f, 0.4f, 1.0f);
    mGround.params = simd_make_float4(0, 0, 0, 0);
    mGround.type = 0;
    scene.materials.push_back(mGround);
    int groundMaterialIndex = (int)scene.materials.size() - 1;

    Sphere sGround = {};
    sGround.positionAndRadius = simd_make_float4(0, -101, 0, 100.0f);
    sGround.materialIndex = groundMaterialIndex;
    scene.spheres.push_back(sGround);

    // a basic triangle mesh: a 2x2 diffuse panel made of two triangles, off to the side
    Material mPanel = {};
    mPanel.albetoAndRoughness = simd_make_float4(0.2f, 0.6f, 0.9f, 1.0f);
    mPanel.params = simd_make_float4(0, 0, 0, 0);
    mPanel.type = 0;
    scene.materials.push_back(mPanel);  // index 17
    int panelMaterialIndex = (int)scene.materials.size() - 1;

    Triangle t1 = {};
    t1.p1 = simd_make_float3(-7.5f, 0.0f, 0.0f);
    t1.p2 = simd_make_float3(-5.5f, 0.0f, 0.0f);
    t1.p3 = simd_make_float3(-5.5f, 2.0f, 0.0f);
    t1.materialIndex = panelMaterialIndex;
    scene.triangles.push_back(t1);

    Triangle t2 = {};
    t2.p1 = simd_make_float3(-7.5f, 0.0f, 0.0f);
    t2.p2 = simd_make_float3(-5.5f, 2.0f, 0.0f);
    t2.p3 = simd_make_float3(-7.5f, 2.0f, 0.0f);
    t2.materialIndex = panelMaterialIndex;
    scene.triangles.push_back(t2);

    // === A simple room (Cornell-box style) made of triangles, centered in front of the camera ===
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
    mRoomLight.emissionColor = simd_make_float4(1, 1, 1, 15.0f);
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

    // Two glass spheres on the floor inside the room
    Material mGlass1 = {};
    mGlass1.type = 1;
    mGlass1.params = simd_make_float4(0, 0, 1.5f, 0);
    scene.materials.push_back(mGlass1);
    int glass1Mat = (int)scene.materials.size() - 1;

    Material mGlass2 = {};
    mGlass2.type = 1;
    mGlass2.params = simd_make_float4(0, 0, 2.0f, 0);
    scene.materials.push_back(mGlass2);
    int glass2Mat = (int)scene.materials.size() - 1;

    float roomMidZ = (roomZ0 + roomZ1) / 2.0f;

    Sphere glassSphere1 = {};
    glassSphere1.positionAndRadius = simd_make_float4(roomX0 + 1.5f, roomY0 + 1.0f, roomMidZ, 1.0f);
    glassSphere1.materialIndex = glass1Mat;
    scene.spheres.push_back(glassSphere1);

    Sphere glassSphere2 = {};
    glassSphere2.positionAndRadius = simd_make_float4(roomX1 - 1.5f, roomY0 + 1.0f, roomMidZ, 1.0f);
    glassSphere2.materialIndex = glass2Mat;
    scene.spheres.push_back(glassSphere2);

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
