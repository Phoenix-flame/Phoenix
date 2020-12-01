cmake_minimum_required(VERSION 3.6)
project(Example)

# Add sub directories
add_subdirectory(phoenix)
add_subdirectory(Example)
add_subdirectory(LearningOpengl)


file(COPY assets DESTINATION Example)
file(COPY assets DESTINATION LearningOpengl)