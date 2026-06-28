
project (phoenix)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread ")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${phoenix_SOURCE_DIR}/cmake")
set(APP_TARGET phoenix)

file(GLOB core_src
    "src/core/*.cpp"
    "src/imGui/*.cpp"
    "src/opengl/*.cpp"
    "src/renderer/*.cpp"
    "src/Scene/*.cpp"
    "src/Math/*.cpp"
    "src/vendor/ImGuizmo/*.cpp"
    "src/vendor/ImFileDialog/*.cpp"
)

add_library(
        ${APP_TARGET} STATIC
        ${core_src}
)
# add_library(phoenix ALIAS ${APP_TARGET})
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/Phoenix/imGui
)
target_include_directories(
    ${APP_TARGET}
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Absolute path to the project root so assets/shaders can be resolved at runtime
# regardless of the working directory. PUBLIC so consumers (e.g. Sandbox) inherit it.
target_compile_definitions(${APP_TARGET} PUBLIC PHX_PROJECT_DIR="${CMAKE_SOURCE_DIR}")


get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "dir='${dir}'")
endforeach()
target_compile_features(${APP_TARGET}
    PRIVATE
    cxx_std_17
)


find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
include_directories(${APP_TARGET} ${OPENGL_INCLUDE_DIR})
find_package(GLM REQUIRED)
# glew
find_package(GLEW REQUIRED)
include_directories(${APP_TARGET} ${GLEW_INCLUDE_DIRS})

# EnTT is vendored as a single header in third_party/ (added to the include path above).

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# Assimp: model/mesh (and later, skeletal animation) loading.
find_package(assimp REQUIRED)
if(TARGET assimp::assimp)
    target_link_libraries(${APP_TARGET} assimp::assimp)
else()
    include_directories(${ASSIMP_INCLUDE_DIRS})
    target_link_libraries(${APP_TARGET} ${ASSIMP_LIBRARIES})
endif()

# yaml-cpp: scene serialization.
find_package(yaml-cpp REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(${APP_TARGET} yaml-cpp::yaml-cpp)
else()
    include_directories(${YAML_CPP_INCLUDE_DIR})
    target_link_libraries(${APP_TARGET} ${YAML_CPP_LIBRARIES})
endif()

#target_link_libraries(${APP_TARGET} ${SDL2_LIBRARIES})
target_link_libraries(${APP_TARGET} fmt)
target_link_libraries(${APP_TARGET} glfw)
target_link_libraries(${APP_TARGET} OpenGL::GL ${GLEW_LIBRARIES})
target_link_libraries(${APP_TARGET} ${SPDLOG_LIBRARIES} ${CMAKE_DL_LIBS})
