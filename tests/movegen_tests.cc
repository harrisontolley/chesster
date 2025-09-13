#include "fen.hh"
#include "move.hh"
#include "movegen.hh"

#include <catch2/catch_test_macros.hpp>

using namespace engine;

static std::size_t count_flag(const std::vector<Move>& ms, int f)
{
    std::size_t n = 0;
    for (auto m : ms)
        if (flag(m) == f)
            ++n;
    return n;
}

TEST_CASE("Pawn single + double pushes on empty board")
{
    // White pawn at a2, empty elsewhere -> a3 (quiet), a4 (double)
    Board b = from_fen("8/8/8/8/8/8/P7/8 w - - 0 1");
    auto ms = generate_moves(b);
    REQUIRE(ms.size() == 2);
    REQUIRE(count_flag(ms, QUIET) == 1);
    REQUIRE(count_flag(ms, DOUBLE_PUSH) == 1);
}

TEST_CASE("Pawn promotions (no captures)")
{
    // White pawn a7 -> a8=N/B/R/Q (4 moves)
    Board b = from_fen("8/P7/8/8/8/8/8/8 w - - 0 1");
    auto ms = generate_moves(b);
    REQUIRE(ms.size() == 4);
    REQUIRE(count_flag(ms, PROMO_N) == 1);
    REQUIRE(count_flag(ms, PROMO_B) == 1);
    REQUIRE(count_flag(ms, PROMO_R) == 1);
    REQUIRE(count_flag(ms, PROMO_Q) == 1);
}

TEST_CASE("Pawn captures")
{
    // White pawn e4 can capture d5 and f5
    Board b = from_fen("8/8/8/3p1p2/4P3/8/8/8 w - - 0 1");
    auto ms = generate_moves(b);
    std::size_t caps = 0;
    for (auto m : ms)
        if (flag(m) == CAPTURE)
            ++caps;
    REQUIRE(caps == 2);
}

TEST_CASE("En passant generation from FEN ep square")
{
    // White to move, ep square at d6, white pawn e5 can capture ep on d6
    Board b = from_fen("8/8/8/3pP3/8/8/8/8 w - d6 0 1");
    auto ms = generate_moves(b);
    std::size_t ep = 0;
    for (auto m : ms)
        if (flag(m) == EN_PASSANT)
            ++ep;
    REQUIRE(ep == 1);
}

TEST_CASE("Rook moves on empty board from a1")
{
    Board b = from_fen("8/8/8/8/8/8/8/R7 w - - 0 1");
    auto ms = generate_moves(b);
    // 7 up + 7 right = 14
    REQUIRE(ms.size() == 14);
}

TEST_CASE("Bishop moves on empty board from a1")
{
    Board b = from_fen("8/8/8/8/8/8/8/B7 w - - 0 1");
    auto ms = generate_moves(b);
    // 7 along NE diagonal
    REQUIRE(ms.size() == 7);
}

TEST_CASE("Queen moves on empty board from a1")
{
    Board b = from_fen("8/8/8/8/8/8/8/Q7 w - - 0 1");
    auto ms = generate_moves(b);
    // rook(14) + bishop(7) = 21
    REQUIRE(ms.size() == 21);
}

TEST_CASE("King moves from center on empty board")
{
    // King on e5 (rank 5): 8 moves
    Board b = from_fen("8/8/8/4K3/8/8/8/8 w - - 0 1");
    auto ms = generate_moves(b);
    REQUIRE(ms.size() == 8);
}

TEST_CASE("White castling both sides when clear and not attacked")
{
    engine::Board b = engine::from_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    auto ms = engine::generate_moves(b);
    REQUIRE(std::count_if(ms.begin(), ms.end(),
                          [](engine::Move m) { return engine::flag(m) == engine::KING_CASTLE; }) == 1);
    REQUIRE(std::count_if(ms.begin(), ms.end(),
                          [](engine::Move m) { return engine::flag(m) == engine::QUEEN_CASTLE; }) == 1);
}

TEST_CASE("Castling blocked by own piece on the path (white king side)")
{
    engine::Board b = engine::from_fen("r3k2r/8/8/8/8/8/8/R3KB1R w KQkq - 0 1");
    auto ms = engine::generate_moves(b);
    REQUIRE(std::none_of(ms.begin(), ms.end(), [](engine::Move m) { return engine::flag(m) == engine::KING_CASTLE; }));
}

TEST_CASE("Castling forbidden if king is in check (white)")
{
    engine::Board b = engine::from_fen("r3k2r/8/8/8/1b6/8/8/R3K2R w KQkq - 0 1");
    auto ms = engine::generate_moves(b);
    REQUIRE(std::none_of(ms.begin(), ms.end(), [](engine::Move m) { return engine::flag(m) == engine::KING_CASTLE; }));
    REQUIRE(std::none_of(ms.begin(), ms.end(), [](engine::Move m) { return engine::flag(m) == engine::QUEEN_CASTLE; }));
}

TEST_CASE("Black castling both sides when clear and not attacked")
{
    engine::Board b = engine::from_fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    auto ms = engine::generate_moves(b);
    REQUIRE(std::count_if(ms.begin(), ms.end(),
                          [](engine::Move m) { return engine::flag(m) == engine::KING_CASTLE; }) == 1);
    REQUIRE(std::count_if(ms.begin(), ms.end(),
                          [](engine::Move m) { return engine::flag(m) == engine::QUEEN_CASTLE; }) == 1);
}
