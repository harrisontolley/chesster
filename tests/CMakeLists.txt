include(${PROJECT_SOURCE_DIR}/cmake/FetchCatch2.cmake)

add_executable(unit_tests
    perft_tests.cc
    bitboard_tests.cc
    movegen_tests.cc 
    movegen_legal_tests.cc)

target_link_libraries(unit_tests PRIVATE engine eval Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)   # auto-registers each TEST_CASE in CTest
