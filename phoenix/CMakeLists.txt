
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(APP_TARGET phoenix)

# CONFIGURE_DEPENDS re-globs on every build, so new source files are picked up
# without re-running cmake by hand.
file(GLOB core_src CONFIGURE_DEPENDS
    "src/core/*.cpp"
    "src/imGui/*.cpp"
    "src/opengl/*.cpp"
    "src/renderer/*.cpp"
    "src/Scene/*.cpp"
    "src/Physics/*.cpp"
    "src/AI/*.cpp"
    "src/Math/*.cpp"
    "src/vendor/ImGuizmo/*.cpp"
    "src/vendor/ImFileDialog/*.cpp"
)

add_library(
        ${APP_TARGET} STATIC
        ${core_src}
)
target_include_directories(
    ${APP_TARGET}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    PRIVATE
    # The vendored ImGui sources include each other as "imgui.h".
    ${CMAKE_CURRENT_SOURCE_DIR}/include/Phoenix/imGui
)
target_compile_features(${APP_TARGET} PUBLIC cxx_std_17)
# Debug builds: PHX_CORE_ASSERT checks and the OpenGL debug-message callback.
target_compile_definitions(${APP_TARGET} PUBLIC "$<$<CONFIG:Debug>:PHX_DEBUG;PHX_ENABLE_ASSERTS>")


find_package(Threads REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLM REQUIRED)
find_package(GLEW REQUIRED)
target_include_directories(${APP_TARGET} PUBLIC ${OPENGL_INCLUDE_DIR} ${GLEW_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS})

# EnTT is vendored as a single header in third_party/ (added to the include path above).

find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# Assimp: model/mesh (and later, skeletal animation) loading.
find_package(assimp REQUIRED)
if(TARGET assimp::assimp)
    target_link_libraries(${APP_TARGET} assimp::assimp)
else()
    target_include_directories(${APP_TARGET} PUBLIC ${ASSIMP_INCLUDE_DIRS})
    target_link_libraries(${APP_TARGET} ${ASSIMP_LIBRARIES})
endif()

# Lua: entity scripting.
find_package(Lua REQUIRED)
target_include_directories(${APP_TARGET} PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(${APP_TARGET} ${LUA_LIBRARIES})

# yaml-cpp: scene serialization.
find_package(yaml-cpp REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
    target_link_libraries(${APP_TARGET} yaml-cpp::yaml-cpp)
else()
    target_include_directories(${APP_TARGET} PUBLIC ${YAML_CPP_INCLUDE_DIR})
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

target_link_libraries(${APP_TARGET} fmt::fmt spdlog::spdlog)
target_link_libraries(${APP_TARGET} glfw OpenGL::GL ${GLEW_LIBRARIES})
target_link_libraries(${APP_TARGET} Threads::Threads ${CMAKE_DL_LIBS})

# The ray batch caster's CPU path must round exactly like its OpenCL kernel (in every
# build, so observations don't depend on the build options). Jolt exports -mfma, and GCC
# fuses a * b + c into FMAs by default in C++, so turn contraction off for that file.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/AI/RayBatchCaster.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# OpenCL (optional): Ray Sensor casts (Reward Zones and mirrored physics bodies) on a
# GPU, toggled in the editor (Settings > Simulation). Without it zones are tested on the
# CPU and bodies by Jolt.
option(PHOENIX_OPENCL "Build the OpenCL backend for batched ray sensor tests" OFF)
if(PHOENIX_OPENCL)
    find_package(OpenCL REQUIRED)
    target_compile_definitions(${APP_TARGET} PRIVATE PHOENIX_OPENCL)
    target_link_libraries(${APP_TARGET} OpenCL::OpenCL)

    # Checks the batch caster's backends against each other and against Jolt, and
    # times them: `cmake --build build --target ray_batch_bench`.
    add_executable(ray_batch_bench EXCLUDE_FROM_ALL tools/ray_batch_bench.cpp)
    target_link_libraries(ray_batch_bench ${APP_TARGET})
endif()

# Runtime assets the engine itself loads by relative path (its shaders and the ImGui
# fonts). Every app calls this so they land in <exe dir>/assets, and the app chdirs to
# its executable's directory at startup. Extra directories passed after the target
# (the app's own assets) are copied on top, each into <exe dir>/<its name>.
#
# This is a separate always-run target rather than a POST_BUILD step: POST_BUILD only
# fires when the app relinks, so an edited shader alone would never reach the build.
set(PHOENIX_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets" CACHE INTERNAL "")
function(phoenix_copy_assets target)
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
        set(copy copy_directory_if_different) # skip unchanged files (models are large)
    else()
        set(copy copy_directory)
    endif()
    set(commands COMMAND ${CMAKE_COMMAND} -E ${copy}
        ${PHOENIX_ASSETS_DIR} $<TARGET_FILE_DIR:${target}>/assets)
    foreach(dir ${ARGN})
        get_filename_component(name ${dir} NAME)
        list(APPEND commands COMMAND ${CMAKE_COMMAND} -E ${copy}
            ${dir} $<TARGET_FILE_DIR:${target}>/${name})
    endforeach()
    add_custom_target(${target}_assets ALL ${commands} VERBATIM)
    add_dependencies(${target}_assets ${target})
endfunction()
