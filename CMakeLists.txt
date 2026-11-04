cmake_minimum_required(VERSION 3.16)
project(adc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# One library with every component, so the app and the tests share the same code.
file(GLOB ADC_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER ADC_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
add_library(adc_core ${ADC_SOURCES})
target_include_directories(adc_core PUBLIC include)

add_executable(adc_app src/main.cpp)
target_link_libraries(adc_app PRIVATE adc_core)

# Tests (GoogleTest). Turn off with -DADC_BUILD_TESTS=OFF if you have no internet.
option(ADC_BUILD_TESTS "Build the GoogleTest suite" ON)
if(ADC_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(adc_tests ${TEST_SOURCES})
  target_link_libraries(adc_tests PRIVATE adc_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(adc_tests)
endif()
