#pragma once
#include "board.hh"
#include "move.hh"

namespace engine {

// Returns state exchange score (centipawns) for playing Move m
// from STM. Positive -> profitable for STM
int see(const Board& b, Move m);

// True if SEE(M) >= threshold (usually 0 for 'non-losing capture')
inline bool see_ge(const Board& b, Move m, int threshold = 0)
{
    return see(b, m) >= threshold;
}

} // namespace engine