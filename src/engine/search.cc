#include "../eval/eval.hh"
#include "board.hh"
#include "move_do.hh"
#include "movegen.hh"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace engine {

static inline int negamax(Board& b, int depth, int alpha, int beta)
{
    if (depth == 0)
        return eval::evaluate(b);

    int best = std::numeric_limits<int>::min() / 2;
    std::vector<Move> moves = generate_legal_moves(b);

    if (moves.empty()) {
        // mate/stalemate as static eval
        return eval::evaluate(b);
    }

    for (Move m : moves) {
        Undo u;
        make_move(b, m, u);
        int score = -negamax(b, depth - 1, -beta, -alpha);
        unmake_move(b, m, u);

        if (score > best)
            best = score;

        if (best > alpha)
            alpha = best;

        if (alpha >= beta)
            break;
    }

    return best;
}

Move search_best_move(Board& b, int depth)
{

    Move best_move = 0;
    int alpha = -30000;
    int beta = 30000;

    for (int d = 1; d <= depth; ++d) {
        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        std::vector<Move> moves = generate_legal_moves(b);

        // capture moves first (! naive move ordering !)
        std::stable_sort(moves.begin(), moves.end(), [](Move a, Move b) {
            const bool ca = is_capture(a), cb = is_capture(b);
            if (ca != cb)
                return ca > cb;
            return a < b; // stable tie-breaker
        });

        for (Move m : moves) {
            Undo u;
            make_move(b, m, u);
            int score = -negamax(b, depth - 1, -beta, -alpha);
            unmake_move(b, m, u);

            if (score > best) {
                best = score;
                local_best = m;
            }

            if (best > alpha)
                alpha = best;
        }

        if (local_best)
            best_move = local_best;
    }

    return best_move;
}
} // namespace engine