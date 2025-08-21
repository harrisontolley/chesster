#pragma once
#include "board.hh"
#include "move.hh"
#include <cstdint>

namespace engine
{
    std::uint64_t perft(Board &b, int depth);
}
