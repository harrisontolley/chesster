#pragma once
#include "board.hh"
#include "move.hh"
#include <cstdint>
#include <vector>
#include <utility>

namespace engine
{
    std::uint64_t perft(Board &b, int depth);

    std::vector<std::pair<Move, std::uint64_t>> perft_divide(Board &b, int depth);
}
