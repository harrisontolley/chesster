add_library(eval STATIC stub_eval.cc eval.hh)
target_include_directories(eval PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
