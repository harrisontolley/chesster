#pragma once
#include "board.hh"
#include "move.hh"

#include <cstdint>
#include <utility>
#include <vector>

namespace engine {
std::uint64_t perft(Board& b, int depth);

std::vector<std::pair<Move, std::uint64_t>> perft_divide(Board& b, int depth);
} // namespace engine
