#pragma once
#include "board.hh"
#include "move.hh"

#include <vector>

namespace engine {
// pseudo-legal generator (fast, may include moves leaving king in check)
std::vector<Move> generate_moves(const Board&);

// legal generator (filters out illegal moves by make/unmake + check test)
std::vector<Move> generate_legal_moves(Board&);
} // namespace engine
