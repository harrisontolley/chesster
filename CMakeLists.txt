cmake_minimum_required(VERSION 3.24) # match CMakePresets.json
project(chesster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Options from your presets ----
option(CHESSTER_SANITIZE "Enable ASan/UBSan" OFF)
option(CHESSTER_NATIVE   "Enable -march=native" OFF)

if (CHESSTER_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-march=native)
endif()

if (CHESSTER_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- Engine library (no main) ----
add_library(chesster_engine
  src/engine/attack_tables.cc
  src/engine/board.cc
  src/engine/fen.cc
  src/engine/move_do.cc
  src/engine/movegen.cc
  src/engine/perft.cc
  src/engine/search.cc
  src/eval/stub_eval.cc
)
target_include_directories(chesster_engine PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/engine
)
target_compile_options(chesster_engine PRIVATE -Wall -Wextra -Wpedantic)

# ---- UCI executable ----
add_executable(chesster src/engine/uci.cc)
target_link_libraries(chesster PRIVATE chesster_engine)
target_compile_options(chesster PRIVATE -Wall -Wextra -Wpedantic)

# ---- Tests ----
include(CTest)
enable_testing()

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(chesster_tests
  tests/bitboard_tests.cc
  tests/movegen_tests.cc
  tests/en_passant_tests.cc
  tests/perft_tests.cc
)
target_link_libraries(chesster_tests PRIVATE chesster_engine Catch2::Catch2WithMain)
target_include_directories(chesster_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/engine
)
target_compile_options(chesster_tests PRIVATE -Wall -Wextra -Wpedantic)

# Catch2 test discovery
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(chesster_tests)
