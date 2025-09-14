#pragma once
#include "board.hh"
#include "move.hh"

namespace engine {
Move search_best_move_timed(Board& b, int maxDepth, int soft_ms, int hard_ms);
Move search_best_move(Board& b, int depth);
} // namespace engine
