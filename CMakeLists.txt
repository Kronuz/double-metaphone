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

  # Copies and moves of the encoder, from non-const lvalues included.
  add_executable(double_metaphone_copy_test test/copy_move.cc)
  target_link_libraries(double_metaphone_copy_test PRIVATE double_metaphone)
  add_test(NAME double_metaphone_copy COMMAND double_metaphone_copy_test)

  # Differential test: every key against a frozen copy of the original encoder
  # (test/double_metaphone_old.*), exhaustive on short strings plus random inputs.
  add_executable(double_metaphone_diff_test test/diff_test.cc test/double_metaphone_old.cc)
  target_link_libraries(double_metaphone_diff_test PRIVATE double_metaphone)
  add_test(NAME double_metaphone_diff COMMAND double_metaphone_diff_test)

  # Benchmark against the frozen original (not run by ctest).
  option(DOUBLE_METAPHONE_BUILD_BENCH "Build bench/bench.cc" OFF)
  if(DOUBLE_METAPHONE_BUILD_BENCH)
    add_executable(double_metaphone_bench bench/bench.cc test/double_metaphone_old.cc)
    target_link_libraries(double_metaphone_bench PRIVATE double_metaphone)

    # Reference: PostgreSQL's dmetaphone.c (REL_17_0), downloaded as one file and compiled into this
    # target only; the library never links it. It computes both keys and stops at four characters.
    option(DOUBLE_METAPHONE_BENCH_REFERENCE "Fetch PostgreSQL's dmetaphone.c as a benchmark-only reference" OFF)
    if(DOUBLE_METAPHONE_BENCH_REFERENCE)
      enable_language(C)
      include(FetchContent)
      FetchContent_Declare(pg_dmetaphone
        URL https://raw.githubusercontent.com/postgres/postgres/REL_17_0/contrib/fuzzystrmatch/dmetaphone.c
        URL_HASH SHA256=c64dfd5e7d0bead621678c1686b7bc395333e4735438d490dc66f5fb7e58c952
        DOWNLOAD_NO_EXTRACT TRUE)
      FetchContent_MakeAvailable(pg_dmetaphone)
      target_sources(double_metaphone_bench PRIVATE bench/pg_dmetaphone.c)
      set_source_files_properties(bench/pg_dmetaphone.c PROPERTIES
        COMPILE_DEFINITIONS "DMETAPHONE_PG_SOURCE=\"${pg_dmetaphone_SOURCE_DIR}/dmetaphone.c\"")
      target_compile_definitions(double_metaphone_bench PRIVATE DOUBLE_METAPHONE_BENCH_REFERENCE=1)
    endif()
  endif()

  # libFuzzer target (clang): every key of arbitrary bytes against the frozen original.
  option(DOUBLE_METAPHONE_FUZZ "Build the libFuzzer target" OFF)
  if(DOUBLE_METAPHONE_FUZZ)
    add_executable(double_metaphone_fuzz fuzz/fuzz_encode.cc test/double_metaphone_old.cc)
    target_link_libraries(double_metaphone_fuzz PRIVATE double_metaphone)
    target_compile_options(double_metaphone_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(double_metaphone_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()

  # A runnable tour of the library (not a test): build & run ./build/double_metaphone_demo
  add_executable(double_metaphone_demo examples/demo.cc)
  target_link_libraries(double_metaphone_demo PRIVATE double_metaphone)
endif()
