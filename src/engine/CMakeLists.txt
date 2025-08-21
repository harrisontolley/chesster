add_library(engine STATIC
    bitboard.hh
    board.hh board.cc
    attack_tables.hh attack_tables.cc
    move.hh
    movegen.hh movegen.cc
    search.cc
    fen.hh fen.cc) 


target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(engine PRIVATE -Wall -Wextra -Wpedantic)

add_executable(chesster uci.cc)
target_link_libraries(chesster PRIVATE engine eval)
