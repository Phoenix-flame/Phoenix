cmake_minimum_required(VERSION 3.2)

project(Phoenix VERSION 4.7.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY true)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${Phoenix_SOURCE_DIR}/cmake")

message (${CMAKE_MODULE_PATH})


set(sources
    main.cpp
    source/application.cpp
    source/core.cpp
    source/window.cpp
    source/log.cpp
)



find_package(SDL2 REQUIRED)
include_directories(${CMAKE_PROJECT_NAME} ${SDL2_INCLUDE_DIRS})

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
include_directories(${CMAKE_PROJECT_NAME} ${OPENGL_INCLUDE_DIR})

find_package(GLM REQUIRED)
message(STATUS "GLM included at ${GLM_INCLUDE_DIR}")

find_package(spdlog REQUIRED)
message(STATUS "SPDLOG included at ${SPDLOG_INCLUDE_DIR}")

add_executable(${CMAKE_PROJECT_NAME} ${sources})
target_link_libraries(${CMAKE_PROJECT_NAME} ${SDL2_LIBRARIES})
target_link_libraries(${CMAKE_PROJECT_NAME} glfw)
target_link_libraries(${CMAKE_PROJECT_NAME} OpenGL::GL)
target_link_libraries(${CMAKE_PROJECT_NAME} ${SPDLOG_LIBRARIES})




