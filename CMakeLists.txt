cmake_minimum_required(VERSION 3.16)
project(FileCollector CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  )
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

add_executable(non_overlapping_intervals_test test/NonOverlappingIntervals.cpp)
target_include_directories(non_overlapping_intervals_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(non_overlapping_intervals_test PRIVATE UNIT_TEST)
target_compile_options(non_overlapping_intervals_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(non_overlapping_intervals_test PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(non_overlapping_intervals_test)
