#include <catch2/catch_test_macros.hpp>
#include "board.hh"
#include "move.hh"
#include "movegen.hh"

TEST_CASE("Empty movegen returns zero")
{
    engine::Board b{};
    REQUIRE(engine::generate_moves(b).empty());
}

TEST_CASE("Empty board has zero moves")
{
    engine::Board empty{};
    REQUIRE(engine::generate_moves(empty).empty());
}

TEST_CASE("Startpos has 20 moves (pawn + knight)")
{
    engine::Board b = engine::Board::startpos();
    REQUIRE(engine::generate_moves(b).size() == 20);
}