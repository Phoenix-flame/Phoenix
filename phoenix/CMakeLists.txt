
project (phoenix)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread ")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${phoenix_SOURCE_DIR}/cmake")
set(APP_TARGET phoenix)

file(GLOB core_src
    "src/core/*.cpp"
    "src/imGui/*.cpp"
    "src/opengl/*.cpp"
    "src/renderer/*.cpp"
)

add_library(
        ${APP_TARGET} STATIC
        ${core_src}
)
# add_library(phoenix ALIAS ${APP_TARGET})
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(
    ${APP_TARGET}
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)


get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir='${dir}'")
endforeach()

#find_package(SDL2 REQUIRED)
#include_directories(${APP_TARGET} ${SDL2_INCLUDE_DIRS})
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
include_directories(${APP_TARGET} ${OPENGL_INCLUDE_DIR})
find_package(GLM REQUIRED)
# glew
find_package(GLEW REQUIRED)
include_directories(${APP_TARGET} ${GLEW_INCLUDE_DIRS})

find_package(spdlog REQUIRED)

#target_link_libraries(${APP_TARGET} ${SDL2_LIBRARIES})
target_link_libraries(${APP_TARGET} glfw)
target_link_libraries(${APP_TARGET} OpenGL::GL ${GLEW_LIBRARIES})
target_link_libraries(${APP_TARGET} ${SPDLOG_LIBRARIES})
