#include "fen.hh"
#include "move.hh"
#include "move_do.hh"
#include "movegen.hh"
#include "perft.hh"

#include <algorithm>
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

TEST_CASE("Startpos legal == 20")
{
    Board b = Board::startpos();
    auto legal = generate_legal_moves(b);
    REQUIRE(legal.size() == 20);
}

TEST_CASE("Pinned rook: legal keeps only e-file rook moves; sideways are removed")
{
    // a8 black king, e8 black rook; e2 white rook pinned to K e1.
    Board b = from_fen("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1");

    auto pseudo = generate_moves(b);
    auto legal = generate_legal_moves(b);

    // Among moves that originate from e2 (the rook), pseudo has 13 rook moves.
    auto is_rook_from_e2 = [](Move m) { return from_sq(m) == E2; };
    std::vector<Move> pseudo_rook;
    std::copy_if(pseudo.begin(), pseudo.end(), std::back_inserter(pseudo_rook), is_rook_from_e2);
    REQUIRE(pseudo_rook.size() == 13);

    // Legal rook moves: only up the e-file (e3..e8)
    auto is_e_file = [](int sq) { return (sq & 7) == file(E1); }; // file 'e'
    std::vector<Move> legal_rook;
    std::copy_if(legal.begin(), legal.end(), std::back_inserter(legal_rook), is_rook_from_e2);

    // All legal rook moves must stay on e-file
    REQUIRE(std::all_of(legal_rook.begin(), legal_rook.end(), [&](Move m) { return is_e_file(to_sq(m)); }));

    // Count: exactly one capture (e2xe8), remaining five quiet pushes (e3..e7)
    REQUIRE(count_flag(legal_rook, CAPTURE) == 1);
    REQUIRE(count_flag(legal_rook, QUIET) == 5);
}

TEST_CASE("En passant that exposes check is removed by legal movegen")
{
    // White: Ke1, Pe5
    // Black: Ka8, Re8, Pd5; ep square d6 (black just played d7-d5).
    Board b = from_fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");

    auto pseudo = generate_moves(b);
    auto legal = generate_legal_moves(b);

    REQUIRE(count_flag(pseudo, EN_PASSANT) == 1); // suggested
    REQUIRE(count_flag(legal, EN_PASSANT) == 0);  // filtered
}

TEST_CASE("Castling legal both sides in clear position (white to move)")
{
    Board b = from_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    auto legal = generate_legal_moves(b);

    REQUIRE(std::count_if(legal.begin(), legal.end(), [](Move m) { return flag(m) == KING_CASTLE; }) == 1);
    REQUIRE(std::count_if(legal.begin(), legal.end(), [](Move m) { return flag(m) == QUEEN_CASTLE; }) == 1);
}

TEST_CASE("Castling illegal if any traversed squares attacked (white)")
{
    Board b = from_fen("r3k2r/8/8/8/1b6/8/8/R3K2R w KQkq - 0 1");
    auto legal = generate_legal_moves(b);

    REQUIRE(std::none_of(legal.begin(), legal.end(), [](Move m) { return flag(m) == KING_CASTLE; }));
    REQUIRE(std::none_of(legal.begin(), legal.end(), [](Move m) { return flag(m) == QUEEN_CASTLE; }));
}

TEST_CASE("Make/Unmake roundtrip restores exact FEN")
{
    Board b = Board::startpos();
    std::string start = to_fen(b);

    auto legal = generate_legal_moves(b);
    for (auto m : legal) {
        Undo u;
        make_move(b, m, u);
        unmake_move(b, m, u);
        REQUIRE(to_fen(b) == start);
    }
}
