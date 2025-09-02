cmake_minimum_required(VERSION 3.20)
project(chesster CXX)
set(CMAKE_CXX_STANDARD 20)

# --- Engine library (no main()) ---
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
# headers live under src/engine and some tests include "engine/..." while others include headers bare
target_include_directories(chesster_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)

add_executable(chesster src/engine/uci.cc)
target_link_libraries(chesster PRIVATE chesster_engine)

# --- Testing setup ---
include(CTest)
enable_testing()

# Pull Catch2 v3 
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

# Register tests with CTest (Catch2 helper)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(chesster_tests)
