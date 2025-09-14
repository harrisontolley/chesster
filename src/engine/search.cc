#include "../eval/eval.hh"
#include "board.hh"
#include "move.hh"
#include "move_do.hh"
#include "movegen.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace engine {

// -------- Time & accounting --------
using clock = std::chrono::steady_clock;
static clock::time_point g_start;
static int g_soft_ms = 0;
static int g_hard_ms = 0;
static std::atomic<std::uint64_t> g_nodes{0};

static inline bool time_enabled()
{
    return g_hard_ms > 0;
}

static inline bool past_soft()
{
    return time_enabled() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - g_start).count() >= g_soft_ms;
}
static inline bool past_hard()
{
    return time_enabled() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - g_start).count() >= g_hard_ms;
}

// -------- Minimal UCI helpers (internal linkage) --------
static std::string sq_to_str(int sq)
{
    if (sq < 0 || sq > 63)
        return "??";
    char f = char('a' + (sq & 7));
    char r = char('1' + (sq >> 3));
    return std::string() + f + r;
}
static std::string move_to_uci(Move m)
{
    std::string s = sq_to_str(from_sq(m)) + sq_to_str(to_sq(m));
    switch (flag(m)) {
    case PROMO_Q:
    case PROMO_Q_CAPTURE:
        s += 'q';
        break;
    case PROMO_R:
    case PROMO_R_CAPTURE:
        s += 'r';
        break;
    case PROMO_B:
    case PROMO_B_CAPTURE:
        s += 'b';
        break;
    case PROMO_N:
    case PROMO_N_CAPTURE:
        s += 'n';
        break;
    default:
        break;
    }
    return s;
}

// -------- Core search --------
static inline int negamax(Board& b, int depth, int alpha, int beta)
{
    g_nodes.fetch_add(1, std::memory_order_relaxed);

    // Cheap periodic time test
    static thread_local int check_counter = 0;
    if (time_enabled()) {
        if ((++check_counter & 31) == 0 && past_hard()) {
            // Out of time: return static eval as a bounded fallback
            return eval::evaluate(b);
        }
    }

    if (depth == 0)
        return eval::evaluate(b);

    int best = std::numeric_limits<int>::min() / 2;
    std::vector<Move> moves = generate_legal_moves(b);

    if (moves.empty()) {
        // Mate/stalemate are handled by static eval
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
            break; // alpha-beta cutoff
    }

    return best;
}

// Iterative deepening with (soft, hard) time limits in ms.
Move search_best_move_timed(Board& b, int maxDepth, int soft_ms, int hard_ms)
{
    g_start = clock::now();
    g_soft_ms = soft_ms;
    g_hard_ms = hard_ms;
    g_nodes = 0;

    Move best_move = 0;
    int alpha = -30000, beta = 30000;

    for (int d = 1; d <= maxDepth; ++d) {
        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        std::vector<Move> moves = generate_legal_moves(b);
        // Naive ordering: captures first
        std::stable_sort(moves.begin(), moves.end(), [](Move a, Move b) {
            const bool ca = is_capture(a), cb = is_capture(b);
            if (ca != cb)
                return ca > cb;
            return a < b;
        });

        for (Move m : moves) {
            if (time_enabled() && past_soft())
                break; // don't start new branches past soft

            Undo u;
            make_move(b, m, u);
            int score = -negamax(b, d - 1, -beta, -alpha);
            unmake_move(b, m, u);

            if (score > best) {
                best = score;
                local_best = m;
            }
            if (best > alpha)
                alpha = best;

            if (time_enabled() && past_hard())
                break; // hit hard wall mid-iteration
        }

        if (local_best)
            best_move = local_best;

        // UCI info line (depth complete)
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(clock::now() - g_start).count();
        auto nodes = g_nodes.load(std::memory_order_relaxed);
        long nps = ms > 0 ? static_cast<long>((nodes * 1000) / ms) : 0;
        std::cout << "info depth " << d << " score cp " << best << " time " << ms << " nodes " << nodes << " nps "
                  << nps << " pv " << move_to_uci(best_move) << "\n";

        if (time_enabled() && past_soft())
            break; // stop after finishing this depth
    }

    g_soft_ms = g_hard_ms = 0; // reset for next call
    return best_move;
}

// Fixed-depth (no time limits); still prints UCI info per depth.
Move search_best_move(Board& b, int depth)
{
    auto start = clock::now();
    g_nodes = 0;

    Move best_move = 0;
    int alpha = -30000, beta = 30000;

    for (int d = 1; d <= depth; ++d) {
        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        std::vector<Move> moves = generate_legal_moves(b);
        std::stable_sort(moves.begin(), moves.end(), [](Move a, Move b) {
            const bool ca = is_capture(a), cb = is_capture(b);
            if (ca != cb)
                return ca > cb;
            return a < b;
        });

        for (Move m : moves) {
            Undo u;
            make_move(b, m, u);
            int score = -negamax(b, d - 1, -beta, -alpha);
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

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
        auto nodes = g_nodes.load(std::memory_order_relaxed);
        long nps = ms > 0 ? static_cast<long>((nodes * 1000) / ms) : 0;
        std::cout << "info depth " << d << " score cp " << best << " time " << ms << " nodes " << nodes << " nps "
                  << nps << " pv " << move_to_uci(best_move) << "\n";
    }

    return best_move;
}

} // namespace engine
