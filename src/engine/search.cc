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

static constexpr int MATE_SCORE = 30000;
static inline int mate_in(int ply)
{
    return MATE_SCORE - ply;
}
static inline int mated_in(int ply)
{
    return -MATE_SCORE + ply;
}

// time management statics
using clock = std::chrono::steady_clock;
static clock::time_point g_start;
static int g_soft_ms = 0;
static int g_hard_ms = 0;
static std::atomic<std::uint64_t> g_nodes{0};

// transposition table
enum : uint8_t { TT_EMPTY = 0, TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };
struct TTEntry {
    std::uint64_t key = 0;   // zobrist key -- 8 bytes
    Move best = 0;           // best/PV move (if known) -- 2 bytes
    int score = 0;           // stored score in cp -- 4 bytes
    int16_t depth = -1;      // search depth remaining when stored -- 2 bytes
    uint8_t flag = TT_EMPTY; // EXACT/LOWER/UPPER -- 1 byte
    uint8_t _pad = 0;        // padding -- 1 byte
    // 18 bytes total + padding
};

static constexpr std::size_t TT_LOG2 = 22;
static constexpr std::size_t TT_SIZE = (1ULL << TT_LOG2);
static constexpr std::uint64_t TT_MASK = TT_SIZE - 1;
static TTEntry g_tt[TT_SIZE]; // fixed size for simplicity

static inline bool in_check(const Board& b)
{
    const Colour us = b.side_to_move;
    const Colour them = (us == WHITE ? BLACK : WHITE);
    int ksq = king_sq(b, us);
    return ksq >= 0 && is_square_attacked(b, ksq, them);
}

static inline std::uint64_t pos_key(const Board& b)
{
    return b.zkey();
}

static inline TTEntry& tt_slot(std::uint64_t key)
{
    return g_tt[key & TT_MASK];
}

// Probe: returns true iff entry can be used to cut or exact return.
// always returns a move hint (outBest) if present, even if not cut-usable.
static inline bool tt_probe(const Board& b, int depth, int alpha, int beta, int& outScore, Move& outBest)
{
    const std::uint64_t k = pos_key(b);
    TTEntry& e = tt_slot(k);

    // if entry at idx does not match key, or is empty
    if (e.key != k || e.flag == TT_EMPTY) {
        return false;
    }

    if (e.best)
        outBest = e.best;

    // searched this position deeper (or equal) previously
    if (e.depth >= depth) {
        if (e.flag == TT_EXACT) {
            outScore = e.score;
            return true;
        }

        if (e.flag == TT_LOWER && e.score >= beta) {
            outScore = e.score;
            return true;
        }

        if (e.flag == TT_UPPER && e.score <= alpha) {
            outScore = e.score;
            return true;
        }
    }
    return false;
}

// Store or replace an entry (unconditional). Replace if depth is greater/equal.
static inline void tt_store(const Board& b, int depth, int score, uint8_t flag, Move best)
{
    const std::uint64_t k = pos_key(b);
    TTEntry& e = tt_slot(k);
    if (e.flag == TT_EMPTY || e.key != k || e.depth <= depth) {
        e.key = k;
        e.best = best;
        e.score = score;
        e.depth = static_cast<int16_t>(depth);
        e.flag = flag;
    }
}

// helper for between game clears
void tt_clear()
{
    for (std::size_t i = 0; i < TT_SIZE; ++i) {
        g_tt[i] = TTEntry{};
    }
}

static void order_moves_tt_first(Board& b, std::vector<Move>& moves)
{
    Move ttBest = 0;
    int dummy;
    (void)tt_probe(b, /*depth*/ 0, -30000, 30000, dummy, ttBest);

    if (ttBest) {
        // move ttBest to front
        auto it = std::find(moves.begin(), moves.end(), ttBest);
        if (it != moves.end())
            std::rotate(moves.begin(), it, it + 1);
    } else {
        // Naive ordering: captures first
        // TODO: better heuristics
        std::stable_sort(moves.begin(), moves.end(), [](Move a, Move b) {
            const bool ca = is_capture(a), cb = is_capture(b);
            if (ca != cb)
                return ca > cb;
            return a < b;
        });
    }
}

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

// Quiesence search (captures and promotions only)
static inline int qsearch(Board& b, eval::EvalState& es, int alpha, int beta)
{
    g_nodes.fetch_add(1, std::memory_order_relaxed);

    if (time_enabled()) {
        static thread_local int tick = 0;
        if ((++tick & 31) == 0 && past_hard())
            return eval::evaluate(es);
    }

    int stand = eval::evaluate(es);
    if (stand >= beta)
        return stand;
    if (stand > alpha)
        alpha = stand;

    std::vector<Move> moves = generate_legal_moves(b);
    for (Move m : moves) {
        const int fl = flag(m);
        const bool isPromo =
                (fl == PROMO_N || fl == PROMO_B || fl == PROMO_R || fl == PROMO_Q || fl == PROMO_N_CAPTURE ||
                 fl == PROMO_B_CAPTURE || fl == PROMO_R_CAPTURE || fl == PROMO_Q_CAPTURE);

        // only consider captures and promotions
        if (!(is_capture(m) || isPromo))
            continue;

        Undo u;
        make_move(b, m, u, &es);
        int score = -qsearch(b, es, -beta, -alpha);
        unmake_move(b, m, u, &es);

        if (score >= beta)
            return score;
        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

// Core search
static inline int negamax(Board& b, eval::EvalState& es, int depth, int alpha, int beta, int ply)
{
    g_nodes.fetch_add(1, std::memory_order_relaxed);

    const int alpha_orig = alpha;

    // Cheap periodic time test
    static thread_local int check_counter = 0;
    if (time_enabled()) {
        // every 32 nodes check if we have exceeded maximum allowable time.
        if ((++check_counter & 31) == 0 && past_hard()) {
            // Out of time: return static eval as a bounded fallback
            return eval::evaluate(es);
        }
    }

    // TT probe (try cut / exact return)
    {
        int tScore;
        Move tBest = 0;
        if (tt_probe(b, depth, alpha, beta, tScore, tBest))
            return tScore;
    }

    // quick draws
    if (b.halfmove_clock >= 100)
        return 0;

    // quiescence to check for captures if depth exhausted
    if (depth == 0)
        return qsearch(b, es, alpha, beta);

    int best = std::numeric_limits<int>::min() / 2;
    Move bestMove = 0;

    std::vector<Move> moves = generate_legal_moves(b);
    if (moves.empty()) {
        // checkmate or stalemate
        int out = in_check(b) ? mated_in(ply) : 0;
        tt_store(b, depth, out, TT_EXACT, 0);
        return out;
    }

    // Move ordering: try TT best move first if available
    order_moves_tt_first(b, moves);

    for (Move m : moves) {
        Undo u;
        make_move(b, m, u, &es);
        int score = -negamax(b, es, depth - 1, -beta, -alpha, ply + 1);
        unmake_move(b, m, u, &es);

        if (score > best) {
            best = score;
            bestMove = m;
        }

        if (best > alpha) {
            alpha = best;
        }

        if (alpha >= beta)
            break; // alpha-beta cutoff

        if (time_enabled() && past_hard())
            break; // hit hard wall mid-iteration
    }

    // Store to TT
    uint8_t flag = TT_EXACT;
    if (best <= alpha_orig)
        flag = TT_UPPER;
    else if (best >= beta)
        flag = TT_LOWER;
    tt_store(b, depth, best, flag, bestMove);

    return best;
}

// Iterative deepening with (soft, hard) time limits in ms.
Move search_best_move_timed(Board& b, int maxDepth, int soft_ms, int hard_ms)
{
    g_start = clock::now();
    g_soft_ms = soft_ms;
    g_hard_ms = hard_ms;
    g_nodes = 0;

    eval::EvalState es;
    eval::init_position(b, es);

    Move best_move = 0;

    for (int d = 1; d <= maxDepth; ++d) {
        int alpha = -30000, beta = 30000;

        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        std::vector<Move> moves = generate_legal_moves(b);

        // Try TT move first; else captures-first
        order_moves_tt_first(b, moves);

        for (Move m : moves) {
            if (time_enabled() && past_soft())
                break; // don't start new branches past soft time cutoff

            Undo u;
            make_move(b, m, u, &es);
            int score = -negamax(b, es, d - 1, -beta, -alpha, 1);
            unmake_move(b, m, u, &es);

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

    eval::EvalState es;
    eval::init_position(b, es);

    Move best_move = 0;

    for (int d = 1; d <= depth; ++d) {
        int alpha = -30000, beta = 30000;

        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        std::vector<Move> moves = generate_legal_moves(b);

        // Try TT move first; else captures-first
        order_moves_tt_first(b, moves);

        for (Move m : moves) {
            Undo u;
            make_move(b, m, u, &es);
            int score = -negamax(b, es, d - 1, -beta, -alpha, 1);
            unmake_move(b, m, u, &es);

            if (score > best) {
                best = score;
                local_best = m;
            }
            if (best > alpha)
                alpha = best;
        }

        if (local_best)
            best_move = local_best;

        // UCI info line (depth complete)
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(clock::now() - start).count();
        auto nodes = g_nodes.load(std::memory_order_relaxed);
        long nps = ms > 0 ? static_cast<long>((nodes * 1000) / ms) : 0;
        std::cout << "info depth " << d << " score cp " << best << " time " << ms << " nodes " << nodes << " nps "
                  << nps << " pv " << move_to_uci(best_move) << "\n";
    }

    return best_move;
}

} // namespace engine
