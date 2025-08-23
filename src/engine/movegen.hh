#pragma once
#include <vector>
#include "board.hh"
#include "move.hh"

namespace engine
{
    // pseudo-legal generator (fast, may include moves leaving king in check)
    std::vector<Move> generate_moves(const Board &);

    // legal generator (filters out illegal moves by make/unmake + check test)
    std::vector<Move> generate_legal_moves(Board &);
}
