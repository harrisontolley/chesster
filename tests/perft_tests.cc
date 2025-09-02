#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <utility>
#include <string>
#include <map>
#include "board.hh"
#include "fen.hh"
#include "move.hh"
#include "movegen.hh"
#include "perft.hh"

using namespace engine;

// --------- small sanity checks ---------
TEST_CASE("Empty board has zero legal moves and perft(1) == 0")
{
    Board empty{};
    REQUIRE(generate_legal_moves(empty).empty());

    Board b2 = empty;
    REQUIRE(perft(b2, 1) == 0);
}

TEST_CASE("Startpos pseudo-legal has 20")
{
    Board b = Board::startpos();
    REQUIRE(generate_moves(b).size() == 20);
}

// helper to assert perft counts and divide sums
static void require_perft_counts(Board base, const std::vector<std::uint64_t> &counts)
{
    for (std::size_t d = 0; d < counts.size(); ++d)
    {
        const int depth = static_cast<int>(d + 1);

        // perft total
        Board b = base; // perft mutates; copy per depth
        auto got = perft(b, depth);
        REQUIRE(got == counts[d]);

        // perft_divide should sum to the same total
        b = base;
        auto parts = perft_divide(b, depth);
        std::uint64_t sum = 0;
        for (auto &kv : parts)
            sum += kv.second;
        REQUIRE(sum == counts[d]);

        // depth-1: nodes should equal number of legal moves
        if (depth == 1)
        {
            b = base;
            auto legal = generate_legal_moves(b);
            REQUIRE(static_cast<std::uint64_t>(legal.size()) == counts[d]);
        }
    }
}

struct PerftCase
{
    const char *name;
    const char *fen;                   // nullptr defaults to startpos
    std::vector<std::uint64_t> counts; // perft counts for depths 1..n
};

TEST_CASE("Perft reference suite (CPW standard positions)")
{
    const PerftCase cases[] = {
        // Start position
        {
            "Start position",
            nullptr, // startpos
            // CPW: 20, 400, 8902, 197281, 4865609
            {20ULL, 400ULL, 8902ULL, 197281ULL, 4865609ULL, 119060324ULL}},
        // Position 5 (CPW)
        {
            "Position 5",
            "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
            // CPW: perft(1..3)
            {44ULL, 1486ULL, 62379ULL}},
        // Position 6 (bishop-full variant that yields 46 at depth 1)
        {
            "Position 6",
            "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
            // CPW: 46, 2079, 89890, 3894594
            {46ULL, 2079ULL, 89890ULL, 3894594ULL}},
    };

    for (const auto &tc : cases)
    {
        SECTION(tc.name)
        {
            Board b = tc.fen ? from_fen(tc.fen) : Board::startpos();
            require_perft_counts(b, tc.counts);
        }
    }
}

struct TrickyPerftCase
{
    const char *name;
    const char *fen;
    int depth;
    std::uint64_t nodes;
};

TEST_CASE("Perft tricky artificial positions (final count only)")
{
    const TrickyPerftCase cases[] = {
        // avoid illegal en passant capture
        {"avoid illegal ep (w)", "8/5bk1/8/2Pp4/8/1K6/8/8 w - d6 0 1", 6, 824064ULL},
        {"avoid illegal ep (b)", "8/8/1k6/8/2pP4/8/5BK1/8 b - d3 0 1", 6, 824064ULL},

        // en passant capture checks opponent
        {"ep capture checks (b)", "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467ULL},
        {"ep capture checks (w)", "8/5k2/8/2Pp4/2B5/1K6/8/8 w - d6 0 1", 6, 1440467ULL},

        // short castling gives check
        {"O-O gives check (w)", "5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072ULL},
        {"O-O gives check (b)", "4k2r/8/8/8/8/8/8/5K2 b k - 0 1", 6, 661072ULL},

        // long castling gives check
        {"O-O-O gives check (w)", "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711ULL},
        {"O-O-O gives check (b)", "r3k3/8/8/8/8/8/8/3K4 b q - 0 1", 6, 803711ULL},

        // castling (including losing rights due to rook capture)
        {"castling + rook capture rights (w)", "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206ULL},
        {"castling + rook capture rights (b)", "r3k2r/7b/8/8/8/8/1B4BQ/R3K2R b KQkq - 0 1", 4, 1274206ULL},

        // castling prevented (attacked squares)
        {"castling prevented (b)", "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476ULL},
        {"castling prevented (w)", "r3k2r/8/5Q2/8/8/3q4/8/R3K2R w KQkq - 0 1", 4, 1720476ULL},

        // promote out of check
        {"promote out of check (w)", "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001ULL},
        {"promote out of check (b)", "3K4/8/8/8/8/8/4p3/2k2R2 b - - 0 1", 6, 3821001ULL},

        // discovered check
        {"discovered check (b)", "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658ULL},
        {"discovered check (w)", "5K2/8/1Q6/2N5/8/1p2k3/8/8 w - - 0 1", 5, 1004658ULL},

        // promote to give check
        {"promote giving check (w)", "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342ULL},
        {"promote giving check (b)", "8/k7/8/8/8/8/1p6/4K3 b - - 0 1", 6, 217342ULL},

        // underpromote to check
        {"underpromote to check (w)", "8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683ULL},
        {"underpromote to check (b)", "8/8/8/8/8/k7/p1K5/8 b - - 0 1", 6, 92683ULL},

        // self stalemate
        {"self stalemate (w)", "K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217ULL},
        {"self stalemate (b)", "8/8/8/8/8/p7/8/k1K5 b - - 0 1", 6, 2217ULL},

        // stalemate/checkmate patterns
        {"stalemate/checkmate (w)", "8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584ULL},
        {"stalemate/checkmate (b)", "8/8/8/8/1k6/8/K1p5/8 b - - 0 1", 7, 567584ULL},

        // double check
        {"double check (b)", "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527ULL},
        {"double check (w)", "8/5k2/8/5N2/5Q2/2K5/8/8 w - - 0 1", 4, 23527ULL},
    };

    for (const auto &tc : cases)
    {
        SECTION(tc.name)
        {
            Board b = from_fen(tc.fen);
            auto got = perft(b, tc.depth);
            REQUIRE(got == tc.nodes);
        }
    }
}
