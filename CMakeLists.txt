cmake_minimum_required(VERSION 3.6)
project(Example)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread ")
# Add sub directories
add_subdirectory(phoenix)
add_subdirectory(Example)
add_subdirectory(LearningOpengl)


file(COPY assets DESTINATION Example)
file(COPY assets DESTINATION LearningOpengl)