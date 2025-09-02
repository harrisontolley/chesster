#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <utility>
#include <string>
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
    const char *fen;                   // nullptr -> startpos
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
            {20ULL, 400ULL, 8902ULL, 197281ULL, 4865609ULL}},
        // Kiwipete
        {
            "Kiwipete",
            "r3k2r/p1ppqpb1/bn2pnp1/2PpP3/1p2P3/2N2N2/PPQ1BPPP/R3K2R w KQkq - 0 1",
            // CPW: 48, 2039, 97862, 4085603  (depth 5 = 193690690, typically too heavy for unit tests)
            {48ULL, 2039ULL, 97862ULL, 4085603ULL}},
        // Position 5
        {
            "Position 5",
            "r4rk1/1pp1qppp/p1np1n2/4p3/4P3/1NNP1Q1P/PPP2PP1/R4RK1 w - - 0 10",
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