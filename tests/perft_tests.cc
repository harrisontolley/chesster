#include <catch2/catch_test_macros.hpp>
#include "board.hh"
#include "fen.hh"
#include "move.hh"
#include "movegen.hh"
#include "perft.hh"

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

TEST_CASE("Startpos pseudo-legal has 20")
{
    engine::Board b = engine::Board::startpos();
    REQUIRE(engine::generate_moves(b).size() == 20);
}

TEST_CASE("Startpos perft small depths (legal)")
{
    engine::Board b = engine::Board::startpos();
    // Standard reference: 20, 400, 8902...
    REQUIRE(engine::perft(b, 1) == 20);
    REQUIRE(engine::perft(b, 2) == 400);
    REQUIRE(engine::perft(b, 3) == 8902);
}

TEST_CASE("Kiwipete perft spot-checks (legal)")
{
    // Famous test from chessprogramming wiki
    // r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
    engine::Board b = engine::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    REQUIRE(engine::perft(b, 1) == 48);
    REQUIRE(engine::perft(b, 2) == 2039);
    REQUIRE(engine::perft(b, 3) == 97862);
}
