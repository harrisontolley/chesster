// tests/en_passant_tests.cc
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <vector>
#include "engine/fen.hh"
#include "engine/move.hh"
#include "engine/movegen.hh"

using namespace engine;

static std::size_t count_moves_with_flag(const std::vector<Move> &ms, int f)
{
    return static_cast<std::size_t>(std::count_if(ms.begin(), ms.end(), [&](Move m)
                                                  { return flag(m) == f; }));
}

// Helper: count legal EP moves in a position FEN
static std::size_t ep_legal_count(const char *fen)
{
    Board b = from_fen(fen);
    auto ms = generate_legal_moves(b);
    return count_moves_with_flag(ms, EN_PASSANT);
}

TEST_CASE("EP only if an enemy pawn actually double-pushed to create the EP square (white to move)")
{
    // Valid EP: black pawn b5 just double-pushed last move, EP square is b6; white pawn on c5 can capture b6.
    // Pieces: black pawn b5, white pawn c5. White to move, ep=b6.
    REQUIRE(ep_legal_count("8/8/8/1pP5/8/8/8/8 w - b6 0 1") == 1);

    // No enemy pawn behind the EP square => no EP should be generated
    REQUIRE(ep_legal_count("8/8/8/2P5/8/8/8/8 w - b6 0 1") == 0);
}

TEST_CASE("EP with two capturing options (white to move)")
{
    // White pawns a5 and c5, black pawn b5 just double-pushed -> ep=b6
    REQUIRE(ep_legal_count("8/8/8/PpP5/8/8/8/8 w - b6 0 1") == 2);
}

TEST_CASE("No phantom EP after h2h4 a7a5")
{
    // After h2h4 (white) and a7a5 (black), it's White to move with EP square a6.
    // White has no pawn adjacent to a5, so there must be zero EP moves.
    const char *fen = "rnbqkbnr/1ppppppp/8/p7/7P/8/PPPPPPP1/RNBQKBNR w KQkq a6 0 2";
    REQUIRE(ep_legal_count(fen) == 0);
}

TEST_CASE("EP is illegal if it leaves own king in check (discovered rook on the e-file)")
{
    // Position:
    //   - White king e1, white pawn e5
    //   - Black rook e8, black pawn d5; last move was d7-d5 -> ep square d6
    // EP move e5xd6 would open the e-file and leave Ke1 in check by Re8, so it must NOT be generated.
    const char *fen = "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";
    REQUIRE(ep_legal_count(fen) == 0);
}
