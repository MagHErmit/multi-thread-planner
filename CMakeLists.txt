project(cpp-course)

cmake_minimum_required(VERSION 3.8)

set(CMAKE_CXX_STANDARD             17)
set(CMAKE_MODULE_PATH              "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_EXPORT_COMPILE_COMMANDS  ON)

include(cmake/BuildFlags.cmake)
include(cmake/TestSolution.cmake)

find_package(Catch REQUIRED)
find_package(Benchmark REQUIRED)
find_package(GTest REQUIRED)



add_gtest(test_executors
  test_executors.cpp
  test_future.cpp
  SOLUTION_SRCS executors/executors.cpp)

add_benchmark(bench_executors
  run.cpp
  SOLUTION_SRCS executors/executors.cpp)