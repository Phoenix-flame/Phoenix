cmake_minimum_required(VERSION 3.6)
project(Example)
# Without a build type CMake passes no -O flags, so the engine and Jolt build unoptimised.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread ")
# Add sub directories
add_subdirectory(phoenix)
add_subdirectory(Example)
add_subdirectory(Sandbox)


file(COPY assets DESTINATION Example)
file(COPY assets DESTINATION Sandbox)