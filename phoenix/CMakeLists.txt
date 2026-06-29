
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
    "src/Physics/*.cpp"
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

# Jolt Physics: fetched and built from source (not packaged in apt).
include(FetchContent)
set(TARGET_UNIT_TESTS       OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD      OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES          OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER           OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.5.0
    GIT_SHALLOW ON
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(JoltPhysics)
target_link_libraries(${APP_TARGET} Jolt)

#target_link_libraries(${APP_TARGET} ${SDL2_LIBRARIES})
target_link_libraries(${APP_TARGET} fmt)
target_link_libraries(${APP_TARGET} glfw)
target_link_libraries(${APP_TARGET} OpenGL::GL ${GLEW_LIBRARIES})
target_link_libraries(${APP_TARGET} ${SPDLOG_LIBRARIES} ${CMAKE_DL_LIBS})
