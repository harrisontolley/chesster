#pragma once
#include <vector>
#include "board.hh"
#include "move.hh"

namespace engine
{
    // public API
    std::vector<Move> generate_moves(const Board &);
}
