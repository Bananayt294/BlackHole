cmake_minimum_required(VERSION 3.24)
project(BlackHole LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL3-devel-3.4.18-VC/SDL3-3.4.18/cmake"
    NO_DEFAULT_PATH
)
add_subdirectory(vendor/glm)

set(VULKAN_SDK_LIB_DIR "$ENV{VULKAN_SDK}/Lib")
find_library(SHADERC_LIBRARY shaderc REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SHADERC_COMBINED_LIBRARY shaderc_combined REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(GLSLANG_LIBRARY glslang REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(GLSLANG_LIMITS_LIBRARY glslang-default-resource-limits REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_LIBRARY SPIRV REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_TOOLS_LIBRARY SPIRV-Tools REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_TOOLS_OPT_LIBRARY SPIRV-Tools-opt REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_TOOLS_LINK_LIBRARY SPIRV-Tools-link REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SHADERC_DEBUG_LIBRARY shadercd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SHADERC_COMBINED_DEBUG_LIBRARY shaderc_combinedd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(GLSLANG_DEBUG_LIBRARY glslangd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(GLSLANG_LIMITS_DEBUG_LIBRARY glslang-default-resource-limitsd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_DEBUG_LIBRARY SPIRVd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_TOOLS_DEBUG_LIBRARY SPIRV-Toolsd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_TOOLS_OPT_DEBUG_LIBRARY SPIRV-Tools-optd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")
find_library(SPIRV_TOOLS_LINK_DEBUG_LIBRARY SPIRV-Tools-linkd REQUIRED PATHS "${VULKAN_SDK_LIB_DIR}")

add_executable(vulkanapp
    BlackHole/BlackHole/main.cpp
    BlackHole/BlackHole/Application.cpp
    BlackHole/BlackHole/utils.cpp
    BlackHole/BlackHole/VmaImplementation.cpp
    BlackHole/BlackHole/VolkImplementation.cpp
)

target_include_directories(vulkanapp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/VulkanMemoryAllocator/include"
)

target_link_libraries(vulkanapp PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
    optimized "${SHADERC_LIBRARY}" debug "${SHADERC_DEBUG_LIBRARY}"
    optimized "${SHADERC_COMBINED_LIBRARY}" debug "${SHADERC_COMBINED_DEBUG_LIBRARY}"
    optimized "${GLSLANG_LIBRARY}" debug "${GLSLANG_DEBUG_LIBRARY}"
    optimized "${GLSLANG_LIMITS_LIBRARY}" debug "${GLSLANG_LIMITS_DEBUG_LIBRARY}"
    optimized "${SPIRV_LIBRARY}" debug "${SPIRV_DEBUG_LIBRARY}"
    optimized "${SPIRV_TOOLS_LIBRARY}" debug "${SPIRV_TOOLS_DEBUG_LIBRARY}"
    optimized "${SPIRV_TOOLS_OPT_LIBRARY}" debug "${SPIRV_TOOLS_OPT_DEBUG_LIBRARY}"
    optimized "${SPIRV_TOOLS_LINK_LIBRARY}" debug "${SPIRV_TOOLS_LINK_DEBUG_LIBRARY}"
)

add_custom_command(TARGET vulkanapp POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/BlackHole/BlackHole/src"
        "$<TARGET_FILE_DIR:vulkanapp>/src"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:SDL3::SDL3>"
        "$<TARGET_FILE_DIR:vulkanapp>"
)
