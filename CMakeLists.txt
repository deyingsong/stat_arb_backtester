cmake_minimum_required(VERSION 3.14)
project(stat_arb_backtester)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -ffast-math")

# Detect Apple Silicon and enable NEON
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+simd")
endif()

include_directories(include third_party/Eigen)

file(GLOB SOURCES "src/*.cpp" "test/*.cpp")
add_executable(stat_arb ${SOURCES})