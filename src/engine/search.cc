#include "../eval/eval.hh"
#include "board.hh"
#include "move.hh"
#include "move_do.hh"
#include "movegen.hh"
#include "util.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
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

static constexpr bool ASP_DEBUG = true;
// Aspiration window half-width
static constexpr int ASP_DELTA_CP = 1024;

static inline bool tt_probe(const Board&, int depth, int alpha, int beta, int& outScore, Move& outBest);

// Move ordering machinery
namespace {

constexpr int MAX_PLY = 128;

// two killer moves per ply (quiet moves that caused beta cutoffs)
static Move g_killer1[MAX_PLY];
static Move g_killer2[MAX_PLY];

// STM history table: history[side][from][to]
static int g_history[2][64][64];

// Piece 'values' for MVV/LVA (relative ordering)
static constexpr int PVAL[6] = {100, 320, 330, 500, 900, 20000}; // P,N,B,R,Q,K

inline Piece captured_piece(const Board& b, Move m)
{
    const int fl = flag(m);
    if (fl == EN_PASSANT)
        return PAWN;

    const Colour us = b.side_to_move;
    const Colour them = (us == WHITE) ? BLACK : WHITE;
    const int to = to_sq(m);

    return piece_on(b, them, to);
}

// Attacker piece current sits on 'from' before making move m
inline Piece attacker_piece(const Board& b, Move m)
{
    const Colour us = b.side_to_move;
    return piece_on(b, us, from_sq(m));
}

// higher is better
inline int mvv_lva(const Board& b, Move m)
{
    Piece vic = captured_piece(b, m);
    Piece att = attacker_piece(b, m);
    if (vic == NO_PIECE || att == NO_PIECE)
        return 0;

    return PVAL[vic] * 16 - PVAL[att];
}

inline int score_move(const Board& b, Move m, Move ttBest, int ply)
{
    // big bucket constants
    constexpr int S_TT = 1'000'000'000;
    constexpr int S_CAP_BASE = 800'000'000;
    constexpr int S_PROMO = 700'000'000; // non-capture promotions
    constexpr int S_k1 = 600'000'000;
    constexpr int S_k2 = 599'000'000;
    constexpr int S_HIST = 1'000; // base added to history

    if (m == ttBest)
        return S_TT;

    if (is_capture(m)) {
        return S_CAP_BASE + mvv_lva(b, m);
    }

    if (is_promo_noncap(m)) {
        int bonus = 0;
        switch (flag(m)) {
        case PROMO_Q:
            bonus = 900;
            break;
        case PROMO_R:
            bonus = 500;
            break;
        case PROMO_B:
            bonus = 330;
            break;
        case PROMO_N:
            bonus = 320;
            break;
        default:
            break;
        }
        return S_PROMO + bonus;
    }

    // Killer quiets
    if (ply >= 0 && ply < MAX_PLY) {
        if (m == g_killer1[ply])
            return S_k1;
        if (m == g_killer2[ply])
            return S_k2;
    }

    const Colour us = b.side_to_move;
    int h = g_history[us][from_sq(m)][to_sq(m)];
    return S_HIST + h;
}

// Sort helper
inline void order_moves(Board& b, std::vector<Move>& moves, int ply)
{
    Move ttBest = 0;
    int dummy;
    (void)dummy;

    (void)tt_probe(b, 0, -30000, 30000, dummy, ttBest);

    std::stable_sort(moves.begin(), moves.end(), [&](Move m1, Move m2) {
        int sa = score_move(b, m1, ttBest, ply);
        int sb = score_move(b, m2, ttBest, ply);
        if (sa != sb)
            return sa > sb;
        return m1 < m2; // stable
    });
}

// update killer moves and history on quiet beta cutoff
inline void on_quiet_cutoff(const Board& b, Move m, int depth, int ply)
{
    // only on quiet moves (no captures/promos)
    if (is_capture(m) || is_promo_any(m))
        return;

    // Killers
    if (ply >= 0 && ply < MAX_PLY) {
        if (g_killer1[ply] != m) {
            g_killer2[ply] = g_killer1[ply];
            g_killer1[ply] = m;
        }
    }

    // history bump (depth ^ 2 is common heuristic)
    const int bump = depth * depth;
    const Colour us = b.side_to_move;
    int& cell = g_history[us][from_sq(m)][to_sq(m)];

    // simple saturation
    if (cell < 2'000'000'000 - bump)
        cell += bump;
}

inline void clear_move_ordering()
{
    std::memset(g_killer1, 0, sizeof(g_killer1));
    std::memset(g_killer2, 0, sizeof(g_killer2));
    std::memset(g_history, 0, sizeof(g_history));
}
} // namespace

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

static constexpr std::size_t TT_LOG2 = 23;
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

// Quiesence search (captures and promotions only)
static inline int qsearch(Board& b, eval::EvalState& es, int alpha, int beta)
{
    g_nodes.fetch_add(1, std::memory_order_relaxed);

    if (time_enabled()) {
        static thread_local int tick = 0;
        if ((++tick & 31) == 0 && past_hard())
            return eval::evaluate(es);
    }

    // If we're in check at qsearch, we must search evasions (no stand-pat).
    if (in_check(b)) {
        std::vector<Move> moves = generate_legal_moves(b);
        order_moves(b, moves, 0);

        if (moves.empty())
            return -MATE_SCORE + 1; // bounded mate-ish value

        for (Move m : moves) {
            Undo u;
            make_move(b, m, u, &es);
            int score = -qsearch(b, es, -beta, -alpha);
            unmake_move(b, m, u, &es);

            if (score > alpha) {
                alpha = score;
                if (alpha >= beta)
                    return alpha;
            }
        }
        return alpha;
    }

    // normal stand-pat
    int stand = eval::evaluate(es);
    if (stand >= beta)
        return stand;
    if (stand > alpha)
        alpha = stand;

    std::vector<Move> moves = generate_legal_moves(b);
    order_moves(b, moves, 0);

    for (Move m : moves) {
        if (!(is_capture(m) || is_promo_any(m)))
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
    order_moves(b, moves, ply);

    bool first = true;
    for (Move m : moves) {
        Undo u;
        make_move(b, m, u, &es);

        int score;
        if (first) {
            // first move: full window (likely PV)
            score = -negamax(b, es, depth - 1, -beta, -alpha, ply + 1);
            first = false;
        } else {
            // subsequent moves: try cheap null window
            int nwBeta = alpha + 1;
            score = -negamax(b, es, depth - 1, -nwBeta, -alpha, ply + 1);

            // Fail high? research with full window to get exact score.
            if (score > alpha) {
                score = -negamax(b, es, depth - 1, -beta, -alpha, ply + 1);
            }
        }

        unmake_move(b, m, u, &es);

        if (score > best) {
            best = score;
            bestMove = m;
        }

        if (best > alpha)
            alpha = best;

        if (alpha >= beta) {
            on_quiet_cutoff(b, m, depth, ply);
            break; // alpha-beta cutoff
        }

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

    clear_move_ordering();

    eval::EvalState es;
    eval::init_position(b, es);

    Move best_move = 0;
    bool have_last = false;
    int last_score = 0;

    for (int d = 1; d <= maxDepth; ++d) {
        // Reset aspiration window each depth
        int delta = have_last ? ASP_DELTA_CP : 500; // wide for the first real score
        int alpha_try = have_last ? (last_score - delta) : -MATE_SCORE;
        int beta_try = have_last ? (last_score + delta) : MATE_SCORE;

        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        while (true) {
            if (time_enabled() && past_hard())
                break;

            int alpha = alpha_try, beta = beta_try;
            best = std::numeric_limits<int>::min() / 2;
            local_best = 0;

            std::vector<Move> moves = generate_legal_moves(b);
            order_moves(b, moves, 0);

            for (Move m : moves) {
                if (time_enabled() && past_soft())
                    break;

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
                    break;
            }

            if (time_enabled() && past_hard())
                break;

            // aspiration result check (use the tried window, not the updated alpha/beta)
            if (best <= alpha_try) {
                // fail-low: widen downward once
                int oldA = alpha_try, oldB = beta_try, oldDelta = delta;
                delta *= 2;
                alpha_try = (have_last ? last_score : best) - delta;
                beta_try = (have_last ? last_score : best) + delta;
                if (alpha_try < -MATE_SCORE)
                    alpha_try = -MATE_SCORE;
                if (beta_try > MATE_SCORE)
                    beta_try = MATE_SCORE;

                if (ASP_DEBUG) {
                    std::cout << "info string asp depth " << d << " fail-low last=" << (have_last ? last_score : best)
                              << " best=" << best << " win0=[" << oldA << "," << oldB << "] Δ0=" << oldDelta
                              << " -> win1=[" << alpha_try << "," << beta_try << "] Δ1=" << delta << "\n";
                }
                continue;
            } else if (best >= beta_try) {
                // fail-high: widen upward once (symmetrically)
                int oldA = alpha_try, oldB = beta_try, oldDelta = delta;
                delta *= 2;
                alpha_try = (have_last ? last_score : best) - delta;
                beta_try = (have_last ? last_score : best) + delta;
                if (alpha_try < -MATE_SCORE)
                    alpha_try = -MATE_SCORE;
                if (beta_try > MATE_SCORE)
                    beta_try = MATE_SCORE;

                if (ASP_DEBUG) {
                    std::cout << "info string asp depth " << d << " fail-high last=" << (have_last ? last_score : best)
                              << " best=" << best << " win0=[" << oldA << "," << oldB << "] Δ0=" << oldDelta
                              << " -> win1=[" << alpha_try << "," << beta_try << "] Δ1=" << delta << "\n";
                }
                continue;
            }
            break; // score inside window
        }

        if (local_best)
            best_move = local_best;
        last_score = best;
        have_last = true;

        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(clock::now() - g_start).count();
        auto nodes = g_nodes.load(std::memory_order_relaxed);
        long nps = ms > 0 ? static_cast<long>((nodes * 1000) / ms) : 0;
        std::cout << "info depth " << d << " score cp " << best << " time " << ms << " nodes " << nodes << " nps "
                  << nps << " pv " << move_to_uci(best_move) << "\n";

        if (time_enabled() && past_soft())
            break;
    }

    g_soft_ms = g_hard_ms = 0;
    return best_move;
}

// Fixed-depth (no time limits); still prints UCI info per depth.
Move search_best_move(Board& b, int depth)
{
    auto start = clock::now();
    g_nodes = 0;

    clear_move_ordering();

    eval::EvalState es;
    eval::init_position(b, es);

    Move best_move = 0;
    bool have_last = false;
    int last_score = 0;

    for (int d = 1; d <= depth; ++d) {
        int delta = have_last ? ASP_DELTA_CP : 500;
        int alpha_try = have_last ? (last_score - delta) : -MATE_SCORE;
        int beta_try = have_last ? (last_score + delta) : MATE_SCORE;

        int best = std::numeric_limits<int>::min() / 2;
        Move local_best = 0;

        while (true) {
            int alpha = alpha_try, beta = beta_try;
            best = std::numeric_limits<int>::min() / 2;
            local_best = 0;

            std::vector<Move> moves = generate_legal_moves(b);
            order_moves(b, moves, 0);

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

            if (best <= alpha_try) {
                int oldA = alpha_try, oldB = beta_try, oldDelta = delta;
                delta *= 2;
                alpha_try = (have_last ? last_score : best) - delta;
                beta_try = (have_last ? last_score : best) + delta;
                if (alpha_try < -MATE_SCORE)
                    alpha_try = -MATE_SCORE;
                if (beta_try > MATE_SCORE)
                    beta_try = MATE_SCORE;

                if (ASP_DEBUG) {
                    std::cout << "info string asp depth " << d << " fail-low last=" << (have_last ? last_score : best)
                              << " best=" << best << " win0=[" << oldA << "," << oldB << "] Δ0=" << oldDelta
                              << " -> win1=[" << alpha_try << "," << beta_try << "] Δ1=" << delta << "\n";
                }
                continue;
            } else if (best >= beta_try) {
                int oldA = alpha_try, oldB = beta_try, oldDelta = delta;
                delta *= 2;
                alpha_try = (have_last ? last_score : best) - delta;
                beta_try = (have_last ? last_score : best) + delta;
                if (alpha_try < -MATE_SCORE)
                    alpha_try = -MATE_SCORE;
                if (beta_try > MATE_SCORE)
                    beta_try = MATE_SCORE;

                if (ASP_DEBUG) {
                    std::cout << "info string asp depth " << d << " fail-high last=" << (have_last ? last_score : best)
                              << " best=" << best << " win0=[" << oldA << "," << oldB << "] Δ0=" << oldDelta
                              << " -> win1=[" << alpha_try << "," << beta_try << "] Δ1=" << delta << "\n";
                }
                continue;
            }
            break; // success inside window
        }

        if (local_best)
            best_move = local_best;
        last_score = best;
        have_last = true;

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
