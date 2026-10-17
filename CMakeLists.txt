cmake_minimum_required(VERSION 3.16)
project(Phoenix LANGUAGES C CXX)

# Without a build type CMake passes no -O flags, so the engine and Jolt build unoptimised.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Lets clangd / IDEs pick up include paths and flags from build/compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(phoenix)
add_subdirectory(Sandbox)
add_subdirectory(Example)
