#include "../engine/board.hh"
#include "../engine/move.hh"
#include "eval.hh"
#include "util.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace eval {

// Model state
static int H = 0;          // hidden size
static bool READY = false; // weights loaded?
static bool IS_Q = false;  // quantised path?

static constexpr int QA = 255;    // l0 quant
static constexpr int QB = 64;     // l1 quant
static constexpr int SCALE = 400; // centipawn scale

// float_raw buffers
static std::vector<float> L0W_Tf; // [768 * H], column-major by feature
static std::vector<float> L0Bf;   // [H]
static std::vector<float> L1Wf;   // [2H]
static float L1Bf = 0.0f;

// quantised buffers (raw i16 from Bullet)
static std::vector<int16_t> L0W_T_q; // [768 * H], column-major by feature
static std::vector<int16_t> L0B_q;   // [H]
static std::vector<int16_t> L1W_q;   // [2H]
static int16_t L1B_q = 0;

static const char* LOADED_FORMAT = "unknown";

static inline float screlu_float(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;
    return x * x;
}

// Square Clipped ReLU for quantised path (matches Rust)
static inline int32_t screlu_i16(int32_t x)
{
    if (x < 0)
        x = 0;
    if (x > QA)
        x = QA;
    return x * x; // i32
}

static inline int flip_rank(int s)
{
    int f = s & 7;
    int r = s >> 3;
    return (7 - r) * 8 + f;
}

static inline int feature_index(engine::Colour ref, engine::Colour pieceSide, int piece, int sq)
{
    // Planes 0...5 = us (c == ref), 6...11 = them
    const int sideBase = (pieceSide == ref) ? 0 : 6;
    const int base = (sideBase + piece) * 64;
    const int sq_norm = (ref == engine::BLACK) ? flip_rank(sq) : sq;
    return base + sq_norm; // 0 ... 768*H-1 after multiplying by H when indexing columns
}

static inline int to_centipawns(float y)
{
    float cp = y * (float)SCALE;
    if (cp > 20000.0f)
        cp = 20000.0f;
    if (cp < -20000.0f)
        cp = -20000.0f;
    return (int)cp;
}

static bool slurp_file(const std::string& p, std::vector<char>& bytes)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;

    in.seekg(0, std::ios::end);
    std::streamoff len = in.tellg();

    if (len <= 0)
        return false;

    in.seekg(0, std::ios::beg);
    bytes.resize((std::size_t)len);
    in.read(bytes.data(), len);

    return (bool)in;
}

static std::vector<std::string> candidate_paths(const char* path)
{
    std::vector<std::string> out;

    auto add_common = [&](const std::string& base) {
        if (base.empty())
            return;

        out.push_back(base);              // exact
        out.push_back(base + ".bin");     // add .bin if omitted
        out.push_back(base + "/raw.bin"); // directory raw.bin

        // directory/<basename>.bin
        std::string bn = base;
        while (!bn.empty() && (bn.back() == '/' || bn.back() == '\\'))
            bn.pop_back();

        auto pos = bn.find_last_of("/\\");
        std::string leaf = (pos == std::string::npos) ? bn : bn.substr(pos + 1);
        if (!leaf.empty())
            out.push_back(base + "/" + leaf + ".bin");
    };

    if (path && *path) {
        add_common(std::string(path));
    } else if (const char* env = std::getenv("CHESSTER_NET")) {
        add_common(std::string(env));
    } else {
        add_common("CHESSTER_NET");
    }
    return out;
}

//  format loaders
static bool try_load_float_raw(const std::vector<char>& blob)
{
    if (blob.size() % 4 != 0)
        return false;
    const std::size_t n_floats = blob.size() / 4;
    if (n_floats < 1 || ((n_floats - 1) % 771) != 0)
        return false;

    H = (int)((n_floats - 1) / 771);
    std::vector<float> buf(n_floats);
    std::memcpy(buf.data(), blob.data(), blob.size());

    L0W_Tf.assign((std::size_t)768 * H, 0.0f);
    L0Bf.assign(H, 0.0f);
    L1Wf.assign(2 * H, 0.0f);

    const float* L0W_row = buf.data(); // [H x 768] row-major
    const float* L0B_raw = L0W_row + (std::size_t)H * 768;
    const float* L1W_raw = L0B_raw + H;
    const float L1B_raw = *(L1W_raw + 2 * H);

    // transpose to [768 x H] (column by feature)
    for (int feat = 0; feat < 768; ++feat)
        for (int i = 0; i < H; ++i)
            L0W_Tf[(std::size_t)feat * H + i] = L0W_row[(std::size_t)i * 768 + feat];

    std::copy(L0B_raw, L0B_raw + H, L0Bf.begin());
    std::copy(L1W_raw, L1W_raw + 2 * H, L1Wf.begin());
    L1Bf = L1B_raw;

    IS_Q = false;
    return true;
}

// Bullet quantised: int16 blocks in order l0w (col-major [768][H]), l0b(H), l1w(2H), l1b(1)
static bool try_load_quantised(const std::vector<char>& blob)
{
    if (blob.size() % 2 != 0)
        return false;
    const std::size_t n_i16 = blob.size() / 2;
    if (n_i16 < 1 || ((n_i16 - 1) % 771) != 0)
        return false;

    H = (int)((n_i16 - 1) / 771);
    std::vector<int16_t> q(n_i16);
    std::memcpy(q.data(), blob.data(), blob.size());

    L0W_T_q.resize((std::size_t)768 * H);
    L0B_q.resize(H);
    L1W_q.resize(2 * H);

    std::size_t off = 0;
    // l0w
    for (int feat = 0; feat < 768; ++feat)
        for (int i = 0; i < H; ++i)
            L0W_T_q[(std::size_t)feat * H + i] = q[off++];

    // l0b
    for (int i = 0; i < H; ++i)
        L0B_q[i] = q[off++];

    // l1w
    for (int i = 0; i < 2 * H; ++i)
        L1W_q[i] = q[off++];

    // l1b
    L1B_q = q[off++];

    IS_Q = true;
    return (off == n_i16);
}

//  API

bool load_weights(const char* path)
{
    std::vector<std::string> cands = candidate_paths(path);
    std::vector<char> blob;

    READY = false;

    for (const auto& p : cands) {
        blob.clear();
        if (!slurp_file(p, blob))
            continue;

        if (try_load_quantised(blob)) { // prefer Bullet's quantised nets
            LOADED_FORMAT = "quantised_i16";
            READY = true;
            return true;
        }
        if (try_load_float_raw(blob)) {
            LOADED_FORMAT = "float_raw";
            READY = true;
            return true;
        }
    }
    return false;
}

void init_position(const engine::Board& b, EvalState& st)
{
    if (!READY)
        throw std::runtime_error("NNUE not ready");

    st.H = H;
    st.quantised = IS_Q;
    st.stm = (uint8_t)b.side_to_move;

    if (IS_Q) {
        st.accW_q.assign(H, 0);
        st.accB_q.assign(H, 0);

        for (int i = 0; i < H; ++i) {
            st.accW_q[i] += (int32_t)L0B_q[i];
            st.accB_q[i] += (int32_t)L0B_q[i];
        }

        for (int c = engine::WHITE; c <= engine::BLACK; ++c) {
            for (int p = engine::PAWN; p <= engine::KING; ++p) {
                engine::Bitboard bb = b.pieces[c][p];
                while (bb) {
                    int sq = __builtin_ctzll(bb);
                    bb &= (bb - 1);

                    int fW = feature_index((engine::Colour)engine::WHITE, (engine::Colour)c, p, sq);
                    int fB = feature_index((engine::Colour)engine::BLACK, (engine::Colour)c, p, sq);

                    const int16_t* colW = &L0W_T_q[(std::size_t)fW * H];
                    const int16_t* colB = &L0W_T_q[(std::size_t)fB * H];

                    for (int i = 0; i < H; ++i) {
                        st.accW_q[i] += (int32_t)colW[i];
                        st.accB_q[i] += (int32_t)colB[i];
                    }
                }
            }
        }
    } else {
        st.accW_f.assign(H, 0.0f);
        st.accB_f.assign(H, 0.0f);
        for (int i = 0; i < H; ++i) {
            st.accW_f[i] += L0Bf[i];
            st.accB_f[i] += L0Bf[i];
        }

        for (int c = engine::WHITE; c <= engine::BLACK; ++c) {
            for (int p = engine::PAWN; p <= engine::KING; ++p) {
                engine::Bitboard bb = b.pieces[c][p];
                while (bb) {
                    int sq = __builtin_ctzll(bb);
                    bb &= (bb - 1);

                    int fW = feature_index((engine::Colour)engine::WHITE, (engine::Colour)c, p, sq);
                    int fB = feature_index((engine::Colour)engine::BLACK, (engine::Colour)c, p, sq);

                    const float* colW = &L0W_Tf[(std::size_t)fW * H];
                    const float* colB = &L0W_Tf[(std::size_t)fB * H];

                    for (int i = 0; i < H; ++i) {
                        st.accW_f[i] += colW[i];
                        st.accB_f[i] += colB[i];
                    }
                }
            }
        }
    }
}

int evaluate(const EvalState& st)
{
    if (!READY)
        throw std::runtime_error("NNUE not ready");
    const bool stmWhite = (st.stm == (uint8_t)engine::WHITE);

    if (IS_Q) {
        std::vector<int32_t> a_stm = stmWhite ? st.accW_q : st.accB_q;
        std::vector<int32_t> a_ntm = stmWhite ? st.accB_q : st.accW_q;

        for (int i = 0; i < H; ++i) {
            a_stm[i] = screlu_i16(a_stm[i]);
            a_ntm[i] = screlu_i16(a_ntm[i]);
        }

        long long out = 0;
        for (int i = 0; i < H; ++i)
            out += (long long)a_stm[i] * (long long)L1W_q[i];
        for (int i = 0; i < H; ++i)
            out += (long long)a_ntm[i] * (long long)L1W_q[H + i];

        out /= QA;
        out += (long long)L1B_q;
        out *= (long long)SCALE;
        out /= ((long long)QA * QB);

        if (out > 20000)
            out = 20000;
        if (out < -20000)
            out = -20000;

        return (int)out;
    } else {
        std::vector<float> a_stm = stmWhite ? st.accW_f : st.accB_f;
        std::vector<float> a_ntm = stmWhite ? st.accB_f : st.accW_f;

        for (int i = 0; i < H; ++i) {
            a_stm[i] = screlu_float(a_stm[i]);
            a_ntm[i] = screlu_float(a_ntm[i]);
        }

        float y = L1Bf;
        for (int i = 0; i < H; ++i)
            y += L1Wf[i] * a_stm[i];
        for (int i = 0; i < H; ++i)
            y += L1Wf[H + i] * a_ntm[i];

        return to_centipawns(y);
    }
}

static inline void apply_col_q(std::vector<int32_t>& acc, const int16_t* col, int sign)
{
    if (sign == 1)
        for (int i = 0; i < H; ++i)
            acc[i] += (int32_t)col[i];
    else
        for (int i = 0; i < H; ++i)
            acc[i] -= (int32_t)col[i];
}

static inline void apply_col_f(std::vector<float>& acc, const float* col, int sign)
{
    if (sign == 1)
        for (int i = 0; i < H; ++i)
            acc[i] += col[i];
    else
        for (int i = 0; i < H; ++i)
            acc[i] -= col[i];
}

void update(EvalState& st, const engine::Board& b, std::uint16_t m, NNUEDelta& d)
{
    using namespace engine;
    if (!READY)
        throw std::runtime_error("NNUE not ready");

    d = NNUEDelta{}; // zero init
    d.stm_before = (uint8_t)b.side_to_move;
    const Colour us = b.side_to_move;
    const Colour them = (us == WHITE) ? BLACK : WHITE;
    const int from = from_sq(m);
    const int to = to_sq(m);
    const int fl = flag(m);

    const Piece moved = piece_on(b, us, from);
    Piece captured = NO_PIECE;
    int capSq = to;

    if (fl == CAPTURE || fl == PROMO_N_CAPTURE || fl == PROMO_B_CAPTURE || fl == PROMO_R_CAPTURE ||
        fl == PROMO_Q_CAPTURE) {
        captured = piece_on(b, them, to);
    } else if (fl == EN_PASSANT) {
        captured = PAWN;
        capSq = (us == WHITE) ? (to - 8) : (to + 8);
    }

    auto push_rem = [&](Colour ref, Colour side, Piece pc, int sq) {
        int f = feature_index(ref, side, pc, sq);
        if (ref == WHITE)
            d.remW[d.remW_n++] = f;
        else
            d.remB[d.remB_n++] = f;
    };

    auto push_add = [&](Colour ref, Colour side, Piece pc, int sq) {
        int f = feature_index(ref, side, pc, sq);
        if (ref == WHITE)
            d.addW[d.addW_n++] = f;
        else
            d.addB[d.addB_n++] = f;
    };

    if (moved != NO_PIECE) {
        push_rem(WHITE, us, moved, from);
        push_rem(BLACK, us, moved, from);
    }

    // capture piece removal
    if (captured != NO_PIECE) {
        push_rem(WHITE, them, captured, capSq);
        push_rem(BLACK, them, captured, capSq);
    }

    // Add moved piece at destination
    Piece placed = moved;
    if (fl == PROMO_N || fl == PROMO_B || fl == PROMO_R || fl == PROMO_Q || fl == PROMO_N_CAPTURE ||
        fl == PROMO_B_CAPTURE || fl == PROMO_R_CAPTURE || fl == PROMO_Q_CAPTURE) {
        placed = promo_piece_from_flag(fl);
    }

    push_add(WHITE, us, placed, to);
    push_add(BLACK, us, placed, to);

    // Castling rook move
    if (fl == KING_CASTLE || fl == QUEEN_CASTLE) {
        int rf, rt;
        if (us == WHITE) {
            if (fl == KING_CASTLE) {
                rf = H1;
                rt = F1;
            } else {
                rf = A1;
                rt = D1;
            }
        } else {
            if (fl == KING_CASTLE) {
                rf = H8;
                rt = F8;
            } else {
                rf = A8;
                rt = D8;
            }
        }

        push_rem(WHITE, us, ROOK, rf);
        push_rem(BLACK, us, ROOK, rf);
        push_add(WHITE, us, ROOK, rt);
        push_add(BLACK, us, ROOK, rt);
    }

    // Apply to accumulators
    if (IS_Q) {
        for (int i = 0; i < d.remW_n; ++i)
            apply_col_q(st.accW_q, &L0W_T_q[(std::size_t)d.remW[i] * H], -1);
        for (int i = 0; i < d.remB_n; ++i)
            apply_col_q(st.accB_q, &L0W_T_q[(std::size_t)d.remB[i] * H], -1);
        for (int i = 0; i < d.addW_n; ++i)
            apply_col_q(st.accW_q, &L0W_T_q[(std::size_t)d.addW[i] * H], +1);
        for (int i = 0; i < d.addB_n; ++i)
            apply_col_q(st.accB_q, &L0W_T_q[(std::size_t)d.addB[i] * H], +1);
    } else {
        for (int i = 0; i < d.remW_n; ++i)
            apply_col_f(st.accW_f, &L0W_Tf[(std::size_t)d.remW[i] * H], -1);
        for (int i = 0; i < d.remB_n; ++i)
            apply_col_f(st.accB_f, &L0W_Tf[(std::size_t)d.remB[i] * H], -1);
        for (int i = 0; i < d.addW_n; ++i)
            apply_col_f(st.accW_f, &L0W_Tf[(std::size_t)d.addW[i] * H], +1);
        for (int i = 0; i < d.addB_n; ++i)
            apply_col_f(st.accB_f, &L0W_Tf[(std::size_t)d.addB[i] * H], +1);
    }

    // flip STM: state now describes the post-move side to move
    st.stm = (uint8_t)((b.side_to_move == WHITE) ? BLACK : WHITE);
}

void revert(EvalState& st, const NNUEDelta& d)
{
    if (!READY)
        throw std::runtime_error("NNUE not ready");

    // Apply inverse operations
    if (IS_Q) {
        for (int i = 0; i < d.addW_n; ++i)
            apply_col_q(st.accW_q, &L0W_T_q[(std::size_t)d.addW[i] * H], -1);
        for (int i = 0; i < d.addB_n; ++i)
            apply_col_q(st.accB_q, &L0W_T_q[(std::size_t)d.addB[i] * H], -1);
        for (int i = 0; i < d.remW_n; ++i)
            apply_col_q(st.accW_q, &L0W_T_q[(std::size_t)d.remW[i] * H], +1);
        for (int i = 0; i < d.remB_n; ++i)
            apply_col_q(st.accB_q, &L0W_T_q[(std::size_t)d.remB[i] * H], +1);
    } else {
        for (int i = 0; i < d.addW_n; ++i)
            apply_col_f(st.accW_f, &L0W_Tf[(std::size_t)d.addW[i] * H], -1);
        for (int i = 0; i < d.addB_n; ++i)
            apply_col_f(st.accB_f, &L0W_Tf[(std::size_t)d.addB[i] * H], -1);
        for (int i = 0; i < d.remW_n; ++i)
            apply_col_f(st.accW_f, &L0W_Tf[(std::size_t)d.remW[i] * H], +1);
        for (int i = 0; i < d.remB_n; ++i)
            apply_col_f(st.accB_f, &L0W_Tf[(std::size_t)d.remB[i] * H], +1);
    }

    // restore STM
    st.stm = d.stm_before;
}

int evaluate(const engine::Board& b)
{
    if (!READY)
        throw std::runtime_error("NNUE not ready");
    EvalState st;
    init_position(b, st);
    return evaluate(st);
}

static inline void stats_vec_f(const char* name, const std::vector<float>& v)
{
    if (v.empty()) {
        std::cerr << name << ": [empty]\n";
        return;
    }
    double mn = v[0], mx = v[0], sum = 0.0, asum = 0.0;
    for (float x : v) {
        mn = std::min<double>(mn, x);
        mx = std::max<double>(mx, x);
        sum += x;
        asum += std::fabs(x);
    }
    double mean = sum / (double)v.size();
    double amean = asum / (double)v.size();
    std::cerr << std::fixed << std::setprecision(6) << name << ": n=" << v.size() << " min=" << mn << " max=" << mx
              << " mean=" << mean << " amean=" << amean << "\n";
}

static inline void stats_vec_q(const char* name, const std::vector<int16_t>& v)
{
    if (v.empty()) {
        std::cerr << name << ": [empty]\n";
        return;
    }
    int mn = v[0], mx = v[0];
    long long sum = 0;
    long long asum = 0;
    for (int16_t x : v) {
        mn = std::min<int>(mn, x);
        mx = std::max<int>(mx, x);
        sum += x;
        asum += std::llabs((long long)x);
    }
    double mean = (double)sum / (double)v.size();
    double amean = (double)asum / (double)v.size();
    std::cerr << name << ": n=" << v.size() << " min=" << mn << " max=" << mx << " mean=" << std::fixed
              << std::setprecision(3) << mean << " amean=" << amean << "\n";
}

// Count features per (side, piece) plane for a given ref colour
static void count_planes(const engine::Board& b, engine::Colour ref, int out[12])
{
    std::fill(out, out + 12, 0);
    for (int c = engine::WHITE; c <= engine::BLACK; ++c)
        for (int p = engine::PAWN; p <= engine::KING; ++p) {
            engine::Bitboard bb = b.pieces[c][p];
            const int chanBase = ((c == ref) ? 0 : 6) + p;
            while (bb) {
                (void)__builtin_ctzll(bb);
                out[chanBase] += 1;
                bb &= (bb - 1);
            }
        }
}

void debug_dump(const engine::Board& b)
{
    if (!READY) {
        std::cerr << "NNUE not loaded.\n";
        return;
    }

    std::cerr << "===== NNUE DIAG =====\n";
    std::cerr << "Format=" << LOADED_FORMAT << "  H=" << H << "  QA=" << QA << "  QB=" << QB << "\n";

    // Weight stats + L1B (in cp)
    if (IS_Q) {
        stats_vec_q("L0B_q", L0B_q);
        stats_vec_q("L1W_q", L1W_q);
        double l1b_cp = (double)L1B_q * (double)SCALE / ((double)QA * (double)QB);
        std::cerr << std::fixed << std::setprecision(6) << "L1B_q: " << L1B_q << "  (L1B*400 cp = " << l1b_cp << ")\n";
    } else {
        stats_vec_f("L0B", L0Bf);
        stats_vec_f("L1W", L1Wf);
        std::cerr << std::fixed << std::setprecision(6) << "L1B: " << L1Bf << "  (L1B*400 cp = " << (L1Bf * 400.0f)
                  << ")\n";
        // Column summaries for float_raw
        std::vector<float> col_min(768), col_max(768), col_mean(768);
        for (int feat = 0; feat < 768; ++feat) {
            float mn = L0W_Tf[feat * H + 0], mx = mn, sum = 0.0f;
            for (int i = 0; i < H; ++i) {
                float x = L0W_Tf[feat * H + i];
                mn = std::min(mn, x);
                mx = std::max(mx, x);
                sum += x;
            }
            col_min[feat] = mn;
            col_max[feat] = mx;
            col_mean[feat] = sum / (float)H;
        }
        stats_vec_f("L0W_T.col_min", col_min);
        stats_vec_f("L0W_T.col_max", col_max);
        stats_vec_f("L0W_T.col_mean", col_mean);
    }

    const auto stm = b.side_to_move;
    const auto ntm = (stm == engine::WHITE) ? engine::BLACK : engine::WHITE;

    // Build both orientations
    auto dump_plane_counts = [&](engine::Colour ref, const char* tag) {
        int cnt[12];
        count_planes(b, ref, cnt);
        const char* PNBRQK = "PNBRQK";
        std::cerr << tag << " planes (us=0..5, them=6..11): ";
        for (int side = 0; side < 2; ++side) {
            for (int p = 0; p < 6; ++p) {
                char ch = (side == 0) ? PNBRQK[p] : (char)std::tolower(PNBRQK[p]);
                std::cerr << ch << "=" << cnt[side * 6 + p] << " ";
            }
            if (side == 0)
                std::cerr << "| ";
        }
        std::cerr << "\n";
    };
    dump_plane_counts(stm, "STM");
    dump_plane_counts(ntm, "NTM");

    // One-shot eval for a sanity check
    std::cerr << "evaluate(b) = " << evaluate(b) << " cp\n";
    std::cerr << "===== end NNUE DIAG =====\n";
}

} // namespace eval
