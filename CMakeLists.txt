cmake_minimum_required(VERSION 3.20)
project(MetalVSCodeProject)

# Metal-cpp requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Locate Apple's Native Frameworks
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(QUARTZ_FRAMEWORK QuartzCore REQUIRED)

# 2. Define Executable and Source Files
add_executable(${PROJECT_NAME} main.cpp)

# 3. Include metal-cpp headers
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp)

# 4. Link everything together
target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${METAL_FRAMEWORK} 
    ${FOUNDATION_FRAMEWORK} 
    ${QUARTZ_FRAMEWORK}
)