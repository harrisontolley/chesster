cmake_minimum_required(VERSION 3.24)
project(chesster VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(src/engine)
add_subdirectory(src/eval)
add_subdirectory(tests)

