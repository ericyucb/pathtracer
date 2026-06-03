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
    uint32_t width = 1280, height = 720;
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
    SDL_RaiseWindow(window);

    // 4. Render loop
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        CA::MetalDrawable* drawable = layer->nextDrawable();
        if (!drawable) continue;

        MTL::CommandBuffer* cmdBuffer = commandQueue->commandBuffer();
        MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder();
        encoder->setComputePipelineState(pipeline);
        encoder->setTexture(drawable->texture(), 0);
        encoder->dispatchThreads(MTL::Size(width, height, 1), MTL::Size(16, 16, 1));
        encoder->endEncoding();
        cmdBuffer->presentDrawable(drawable);
        cmdBuffer->commit();
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
