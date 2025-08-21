cmake_minimum_required(VERSION 3.24)
project(chesster VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHESSTER_SANITIZE "Enable ASan/UBSan in Debug" ON)
option(CHESSTER_NATIVE   "Enable -march=native in Release" ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND CHESSTER_SANITIZE)
  add_compile_options(-fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Release" AND CHESSTER_NATIVE)
  add_compile_options(-O3 -DNDEBUG -march=native)
endif()

enable_testing()

add_subdirectory(src/engine)
add_subdirectory(src/eval)
add_subdirectory(tests)
