#pragma once
#include "engine/board.hh"

#include <cstdint>
#include <vector>

namespace eval {

// Incremental NNUE API
// init_position(b, state): builds both accumulators
// evaluate(state): returns eval in centipawns
// update(state, board, move, outDelta): apply NNUE delta for the move (pre-move board)
// revert(state, delta): undo the last update
// load_weights: loads bullet i16 quantised or float_raw weights

struct EvalState;

// Reversible change for one move (indices only, columns re-applied on revert)
struct NNUEDelta {
    // Counts (each <= 8)
    uint8_t addW_n{0}, remW_n{0}, addB_n{0}, remB_n{0};

    // feature indices for ref=WHITE / ref=BLACK (0...768*H-1)
    int addW[8], remW[8], addB[8], remB[8];

    // STM before update (so evaluate(state) toggles correctly on revert)
    uint8_t stm_before{0};
};

// load a network file.
bool load_weights(const char* path = nullptr);

// Build accumulators for current board position and set STM
void init_position(const engine::Board& b, EvalState& state);

// Fast eval from accumulators in centipawns
int evaluate(const EvalState& state);

// Forward incremental NNUE update: compute column index deltas from the current
// board and move (pre-move), apply them, and flip STM inside state. The computed
// indices are returned in outDelta so they can be reverted later with revert().
void update(EvalState& st, const engine::Board& b, std::uint16_t move, NNUEDelta& outDelta);

// Revert a previous update (apply inverse columns and restore STM)
void revert(EvalState& st, const NNUEDelta& delta);

// Compatibility helpers
int evaluate(const engine::Board&); // builds a temp state then calls evaluate(state)
void debug_dump(const engine::Board& b);

struct EvalState {
    // model shape / path flag
    int H{0};
    bool quantised{true};

    std::uint8_t stm{0}; // 0=WHITE, 1=BLACK

    // Reference orientation accumulators
    // accW = ref=WHITE, preactivation vector (bias + sum(feature columns))
    // accB = ref=BLACK, preactivation vector (bias + sum(feature columns))
    std::vector<int32_t> accW_q, accB_q; // quantised path
    std::vector<float> accW_f, accB_f;   // float path
};

} // namespace eval
