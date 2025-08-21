#pragma once
#include <vector>
#include "board.hh"
#include "move.hh"

namespace engine
{
    std::vector<Move> generate_moves(const Board &);
}