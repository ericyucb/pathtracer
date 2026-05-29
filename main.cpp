#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

std::string ReadShaderSource(const std::string& filePath) {
    std::ifstream file(filePath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void SaveTextureToTGA(MTL::Texture* texture, const std::string& filename) {
    uint32_t width  = texture->width();
    uint32_t height = texture->height();

    std::vector<float> floatPixels(width * height * 4);
    MTL::Region region = MTL::Region(0, 0, width, height);
    //- floatPixels.data() — pointer to the destination CPU array
    //   - width * 4 * sizeof(float) — how many bytes make up one row (called the "stride" or "bytes per row")
    //   - region — which part of the texture to copy (here, the whole thing)
    //   - 0 — which mipmap level (0 = full resolution, which is all we have)
    texture->getBytes(floatPixels.data(), width * 4 * sizeof(float), region, 0); // this line moves the texture from the gpu into float pixels which lives on the cpu
    std::vector<uint8_t> tgaPixels(width * height * 4);

//       1. Format conversion — the texture stores colors as floats in the range 0.0–1.0, but TGA files expect bytes in the range 0–255. The * 255.0f and static_cast<uint8_t> do that conversion.
//   2. Channel reordering — the texture is RGBA order, but TGA format expects BGRA order. The loop swaps R and B:
//   tgaPixels[i + 0] = floatPixels[i + 2] // B (was index 2)
//   tgaPixels[i + 2] = floatPixels[i + 0] // R (was index 0)
    for (size_t i = 0; i < floatPixels.size(); i += 4) {
        tgaPixels[i + 0] = static_cast<uint8_t>(floatPixels[i + 2] * 255.0f); // B
        tgaPixels[i + 1] = static_cast<uint8_t>(floatPixels[i + 1] * 255.0f); // G
        tgaPixels[i + 2] = static_cast<uint8_t>(floatPixels[i + 0] * 255.0f); // R
        tgaPixels[i + 3] = static_cast<uint8_t>(floatPixels[i + 3] * 255.0f); // A
    }


    // this sets up the outputted image with a header that the tga documentation expects
    std::ofstream file(filename, std::ios::binary);
    uint8_t header[18] = {0};
    header[2]  = 2;              // uncompressed true-color
    header[12] = width  & 0xFF;
    header[13] = (width  >> 8) & 0xFF;
    header[14] = height & 0xFF;
    header[15] = (height >> 8) & 0xFF;
    header[16] = 32;             // 32 bpp BGRA
    file.write(reinterpret_cast<char*>(header), 18);
    file.write(reinterpret_cast<char*>(tgaPixels.data()), tgaPixels.size()); //writes the tga file

    std::cout << "Saved: " << filename << std::endl;
}

int main() {
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

    // 3. Output texture
    uint32_t width = 1280, height = 720;
    MTL::TextureDescriptor* texDesc = MTL::TextureDescriptor::texture2DDescriptor( //  Creates a descriptor — a specification object that describes what the texture should look like, without allocating it yet
        MTL::PixelFormatRGBA32Float, width, height, false);
    texDesc->setUsage(MTL::TextureUsageShaderWrite); // tells metal this texture will be written to by a shader, metal needs to know this so it can allocate the texture in the right kind of gpu memory
    MTL::Texture* outputTexture = device->newTexture(texDesc); // allocates memory in the gpu for the texture


    //For allocating other kinds of GPU memory you'd use different calls:
//   - device->newBuffer() — for raw unstructured data (like the sphere/material arrays in the old code)
//   - device->newTexture() — for 2D pixel grids

    // 4. Dispatch kernel
    MTL::CommandBuffer* cmdBuffer = commandQueue->commandBuffer(); //think of a command as a sequence of instructions
    MTL::ComputeCommandEncoder* encoder = cmdBuffer->computeCommandEncoder(); //   Creates an encoder attached to that command buffer. The encoder has methods for writing GPU instructions into the buffer.

    encoder->setComputePipelineState(pipeline); //   Records an instruction that tells the GPU which compiled shader program to use.
    encoder->setTexture(outputTexture, 0); //   Records an instruction that binds outputTexture to slot 0, making it accessible inside the shader.
    encoder->dispatchThreads(MTL::Size(width, height, 1), MTL::Size(16, 16, 1)); //   Records an instruction that tells the GPU to launch the shader with a 1280×720 grid of threads, grouped into 16×16 threadgroups.
    encoder->endEncoding(); //   Closes the encoder — no more instructions can be written to this command buffer.

    cmdBuffer->commit(); //  Submits the command buffer to the command queue, which delivers it to the GPU for execution.
    cmdBuffer->waitUntilCompleted(); //   Blocks the CPU thread until the GPU has finished executing all instructions in the command buffer.

    // 5. Save result
    SaveTextureToTGA(outputTexture, "/Users/ericyu/Documents/raytracing/render_output.tga");

    //Free all the memory allocated
    outputTexture->release();
    pipeline->release();
    kernelFunc->release();
    library->release();
    commandQueue->release();
    device->release();

    return 0;
}
