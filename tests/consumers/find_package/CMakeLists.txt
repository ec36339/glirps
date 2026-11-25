# A project that uses an installed GLIRPS package. Point `CMAKE_PREFIX_PATH` at
# the install prefix.
cmake_minimum_required(VERSION 3.24)

project(glirps_find_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glirps 0.1 CONFIG REQUIRED)
message(STATUS "Found GLIRPS ${glirps_VERSION} in ${glirps_DIR}")

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE glirps::glirps)
