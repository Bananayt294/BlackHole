cmake_minimum_required(VERSION 3.24)
project(BlackHole-C++)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add local dependencies from your vendor folder
add_subdirectory(vendor/SDL)
add_subdirectory(vendor/glm)  # <-- Added GLM here

find_package(Vulkan REQUIRED)

add_executable(vulkanapp
    "BlackHole/BlackHole/main.cpp"
    "BlackHole/BlackHole/Application.h"
    "BlackHole/BlackHole/Application.cpp"
    "BlackHole/BlackHole/main.cpp" 
)

if(MSVC)
    target_compile_options(vulkanapp PRIVATE /std:c++latest)
endif()

target_link_libraries(vulkanapp PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
)