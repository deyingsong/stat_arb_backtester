cmake_minimum_required(VERSION 3.14)
project(stat_arb_backtester)

set(CMAKE_CXX_STANDARD 17)

include_directories(include third_party/Eigen)

file(GLOB SOURCES "src/*.cpp")
add_executable(stat_arb ${SOURCES})
