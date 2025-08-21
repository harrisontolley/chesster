include(${PROJECT_SOURCE_DIR}/cmake/FetchCatch2.cmake)
include(CTest)

add_executable(unit_tests
    perft_tests.cc)          
    
target_link_libraries(unit_tests
    PRIVATE engine eval Catch2::Catch2WithMain)


add_test(
  NAME unit_tests
  COMMAND $<TARGET_FILE:unit_tests>
)