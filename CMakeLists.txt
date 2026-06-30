cmake_minimum_required(VERSION 3.14)
project(double_metaphone LANGUAGES CXX)

# --- Library target ---
# Double Metaphone's rule set is large, so it lives in a .cc and gets compiled
# (unlike header-only soundex). The public surface is the single header
# double_metaphone.h. Zero external dependencies: pure std/<string>, the encoder
# does its own ASCII upper-casing, so there is no FetchContent here.
add_library(double_metaphone double_metaphone.cc)
target_include_directories(double_metaphone PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(double_metaphone PUBLIC cxx_std_20)

# Alias so consumers can link `double_metaphone::double_metaphone` whether they
# add this via add_subdirectory or FetchContent.
add_library(double_metaphone::double_metaphone ALIAS double_metaphone)

# Only build the test and demo when this repo is the top-level project, so
# consumers (e.g. via FetchContent) don't build them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(double_metaphone_test test/test.cc)
  target_link_libraries(double_metaphone_test PRIVATE double_metaphone)
  add_test(NAME double_metaphone COMMAND double_metaphone_test)

  # A runnable tour of the library (not a test): build & run ./build/double_metaphone_demo
  add_executable(double_metaphone_demo examples/demo.cc)
  target_link_libraries(double_metaphone_demo PRIVATE double_metaphone)
endif()
