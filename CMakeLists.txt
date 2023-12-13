#  minimum required cmake version: 3.1.0
cmake_minimum_required(VERSION 3.1.0)

project(Point_Cloud_Stitching)

# Save the command line compile commands in the build output
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_CXX_FLAGS_RELEASE "-O3")

option(BUILD_CLIENT "Build the central computing client" OFF)

include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src)
# add_subdirectory(snappy)
