cmake_minimum_required(VERSION 3.16)
project(FileCollector CXX)

option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)

if(ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common compile options
add_compile_options(-Wall -Wextra -Wpedantic)

# Library for the core logic
add_library(file_collector
  src/FileCollector.cpp
  src/Exception.cpp
)

target_include_directories(file_collector PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Unit Tests
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

add_executable(non_overlapping_intervals_test test/NonOverlappingIntervalsTest.cpp)
target_link_libraries(non_overlapping_intervals_test PRIVATE
  file_collector
  GTest::gtest_main
)
target_compile_definitions(non_overlapping_intervals_test PRIVATE UNIT_TEST)

add_executable(file_collector_test test/FileCollectorTest.cpp)
target_link_libraries(file_collector_test PRIVATE
  file_collector
  GTest::gtest_main
)
target_compile_definitions(file_collector_test PRIVATE UNIT_TEST)

include(GoogleTest)
gtest_discover_tests(non_overlapping_intervals_test)
gtest_discover_tests(file_collector_test)

if(ENABLE_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_CTEST_COMMAND}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
        COMMAND gcovr -r ${CMAKE_SOURCE_DIR} --html-details -o ${CMAKE_BINARY_DIR}/coverage/index.html
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running tests and generating HTML coverage report..."
    )
    add_dependencies(coverage non_overlapping_intervals_test file_collector_test)
endif()
