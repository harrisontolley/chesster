#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include "engine/board.hh"
#include "engine/fen.hh"
#include "engine/perft.hh"

using namespace engine;

static void require_perft_counts(Board base, const std::vector<std::uint64_t> &counts)
{
    for (std::size_t d = 0; d < counts.size(); ++d)
    {
        Board b = base; // perft mutates; copy per depth
        auto got = perft(b, static_cast<int>(d + 1));
        REQUIRE(got == counts[d]);
        // sanity: perft_divide must sum to perft
        b = base;
        auto parts = perft_divide(b, static_cast<int>(d + 1));
        std::uint64_t sum = 0;
        for (auto &kv : parts)
            sum += kv.second;
        REQUIRE(sum == counts[d]);
    }
}

TEST_CASE("Perft reference: Start position")
{
    Board base = Board::startpos();
    // CPW/UCI standard perft
    std::vector<std::uint64_t> counts = {20, 400, 8902, 197281, 4865609};
    require_perft_counts(base, counts);
}

TEST_CASE("Perft reference: Kiwipete")
{
    const char *FEN = "r3k2r/p1ppqpb1/bn2pnp1/2PpP3/1p2P3/2N2N2/PPQ1BPPP/R3K2R w KQkq - 0 1";
    Board base = from_fen(FEN);
    // CPW/UCI standard perft
    std::vector<std::uint64_t> counts = {48, 2039, 97862, 4085603};
    require_perft_counts(base, counts);
}

TEST_CASE("Perft reference: Position 5")
{
    const char *FEN = "r4rk1/1pp1qppp/p1np1n2/4p3/4P3/1NNP1Q1P/PPP2PP1/R4RK1 w - - 0 10";
    Board base = from_fen(FEN);
    // CPW/UCI standard perft
    std::vector<std::uint64_t> counts = {46, 2079, 89890, 3894594};
    require_perft_counts(base, counts);
}
