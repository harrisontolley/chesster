#pragma once
#include "board.hh"
#include "eval/eval.hh"
#include "move.hh"

#include <optional>

namespace engine {

// Undo structure to faciliate and store information required to undo moves (for searching gametree)
struct Undo {
    std::optional<int> ep_prev;
    CastlingRights castle_prev;
    int halfmove_prev;
    int fullmove_prev;

    Piece moved_piece{NO_PIECE};
    Piece captured_piece{NO_PIECE};

    eval::NNUEDelta nnue{};
};

// helpers exposed so perft and movegen can share
bool is_square_attacked(const Board& b, int sq, Colour by);

inline int king_sq(const Board& b, Colour c)
{
    Bitboard k = b.pieces[c][KING];
    return k ? __builtin_ctzll(k) : -1; // return -1 if no king found
}

// mutating move application - does not check legality
void make_move(Board& b, Move m, Undo& u);
void unmake_move(Board& b, Move m, Undo& u);

// Overloads that also update NNUE accumulators
void make_move(Board& b, Move m, Undo& u, eval::EvalState* es);
void unmake_move(Board& b, Move m, Undo& u, eval::EvalState* es);
} // namespace engine