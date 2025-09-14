#include "../engine/board.hh"
#include "eval.hh"

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

// -------------------------- Model state --------------------------

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

// -------------------------- Helpers --------------------------

static inline float screlu_float(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;
    return x * x;
}

// Square Clipped ReLU for quantised path (matches Rust)
static inline int32_t screlu_i16(int16_t x)
{
    int32_t y = (int32_t)x;
    if (y < 0)
        y = 0;
    if (y > QA)
        y = QA;
    return y * y; // i32
}

static inline int flip_rank(int s)
{
    int f = s & 7;
    int r = s >> 3;
    return (7 - r) * 8 + f;
}

// Build **all** feature indices for a reference colour `ref`.
// Planes 0..5 = us (c==ref), 6..11 = them (c!=ref).
// Square normalization: flip whole board ranks when ref==BLACK.
static void fill_indices_all(const engine::Board& b, engine::Colour ref, std::vector<int>& idx)
{
    idx.clear();
    idx.reserve(40);

    const bool flip_all = (ref == engine::BLACK);

    for (int c = engine::WHITE; c <= engine::BLACK; ++c) {
        const int sideBase = (c == ref) ? 0 : 6;
        for (int p = engine::PAWN; p <= engine::KING; ++p) {
            engine::Bitboard bb = b.pieces[c][p];
            if (!bb)
                continue;
            const int base = (sideBase + p) * 64;
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= (bb - 1);
                int sq_norm = flip_all ? flip_rank(sq) : sq;
                idx.push_back(base + sq_norm);
            }
        }
    }
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

// --------------------------- IO helpers -------------------------

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

// ------------------------- format loaders -----------------------

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

// ---------------------------- API -------------------------------

bool load_weights(const char* path)
{
    std::vector<std::string> cands = candidate_paths(path);
    std::vector<char> blob;

    READY = false;

    for (const auto& p : cands) {
        blob.clear();
        if (!slurp_file(p, blob))
            continue;

        if (try_load_float_raw(blob)) {
            LOADED_FORMAT = "float_raw";
            READY = true;
            return true;
        }
        if (try_load_quantised(blob)) {
            LOADED_FORMAT = "quantised_i16";
            READY = true;
            return true;
        }
    }
    return false;
}

// --- Evaluation (quantised path, bit-for-bit like Rust) ---

static int evaluate_quantised(const engine::Board& b)
{
    const auto stm = b.side_to_move;
    const auto ntm = (stm == engine::WHITE) ? engine::BLACK : engine::WHITE;

    std::vector<int> idx_stm, idx_ntm;
    fill_indices_all(b, stm, idx_stm);
    fill_indices_all(b, ntm, idx_ntm);

    // Start hidden accumulators at bias (i16 semantics), add features (i16)
    std::vector<int32_t> acc_stm(H), acc_ntm(H);
    for (int i = 0; i < H; ++i) {
        acc_stm[i] = (int32_t)L0B_q[i];
        acc_ntm[i] = (int32_t)L0B_q[i];
    }

    for (int f : idx_stm) {
        const int16_t* col = &L0W_T_q[(std::size_t)f * H];
        for (int i = 0; i < H; ++i)
            acc_stm[i] += (int32_t)col[i];
    }
    for (int f : idx_ntm) {
        const int16_t* col = &L0W_T_q[(std::size_t)f * H];
        for (int i = 0; i < H; ++i)
            acc_ntm[i] += (int32_t)col[i];
    }

    // Screlu in i16 domain
    for (int i = 0; i < H; ++i) {
        acc_stm[i] = screlu_i16((int16_t)acc_stm[i]);
        acc_ntm[i] = screlu_i16((int16_t)acc_ntm[i]);
    }

    // Output accumulation (STM first half, NTM second half)
    long long out = 0;
    for (int i = 0; i < H; ++i)
        out += (long long)acc_stm[i] * (long long)L1W_q[i];
    for (int i = 0; i < H; ++i)
        out += (long long)acc_ntm[i] * (long long)L1W_q[H + i];

    // Reduce quantisation and scale exactly like Rust
    out /= QA;                   // QA*QA*QB -> QA*QB
    out += (long long)L1B_q;     // add bias (QA*QB)
    out *= (long long)SCALE;     // to cp
    out /= ((long long)QA * QB); // remove QA*QB

    if (out > 20000)
        out = 20000;
    if (out < -20000)
        out = -20000;
    return (int)out;
}

// --- Evaluation (float_raw path) ---

static int evaluate_float_raw(const engine::Board& b)
{
    const auto stm = b.side_to_move;
    const auto ntm = (stm == engine::WHITE) ? engine::BLACK : engine::WHITE;

    std::vector<int> idx_stm, idx_ntm;
    fill_indices_all(b, stm, idx_stm);
    fill_indices_all(b, ntm, idx_ntm);

    std::vector<float> h_stm(L0Bf), h_ntm(L0Bf);

    for (int f : idx_stm) {
        const float* col = &L0W_Tf[(std::size_t)f * H];
        for (int i = 0; i < H; ++i)
            h_stm[i] += col[i];
    }
    for (int f : idx_ntm) {
        const float* col = &L0W_Tf[(std::size_t)f * H];
        for (int i = 0; i < H; ++i)
            h_ntm[i] += col[i];
    }

    for (int i = 0; i < H; ++i) {
        h_stm[i] = screlu_float(h_stm[i]);
        h_ntm[i] = screlu_float(h_ntm[i]);
    }

    float y = L1Bf;
    for (int i = 0; i < H; ++i)
        y += L1Wf[i] * h_stm[i]; // STM half
    for (int i = 0; i < H; ++i)
        y += L1Wf[H + i] * h_ntm[i]; // NTM half

    return to_centipawns(y);
}

int evaluate(const engine::Board& b)
{
    if (!READY)
        throw std::runtime_error("NNUE not ready");
    return IS_Q ? evaluate_quantised(b) : evaluate_float_raw(b);
}

// --------------------------- debug ------------------------------

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
    std::vector<int> idx_stm, idx_ntm;
    fill_indices_all(b, stm, idx_stm);
    fill_indices_all(b, ntm, idx_ntm);
    std::cerr << "Indices: stm=" << idx_stm.size() << "  ntm=" << idx_ntm.size() << "\n";

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

    // Trace a single eval, mirroring the active path
    if (IS_Q) {
        std::vector<int32_t> s_stm(H), s_ntm(H);
        for (int i = 0; i < H; ++i) {
            s_stm[i] = L0B_q[i];
            s_ntm[i] = L0B_q[i];
        }

        for (int f : idx_stm) {
            const int16_t* col = &L0W_T_q[(std::size_t)f * H];
            for (int i = 0; i < H; ++i)
                s_stm[i] += (int32_t)col[i];
        }
        for (int f : idx_ntm) {
            const int16_t* col = &L0W_T_q[(std::size_t)f * H];
            for (int i = 0; i < H; ++i)
                s_ntm[i] += (int32_t)col[i];
        }

        auto post = [&](std::vector<int32_t>& v, const char* tag) {
            int zeros = 0, ones = 0, gt1 = 0, lt0 = 0;
            long long sum = 0;
            int mn = (int)v[0], mx = (int)v[0];
            for (int i = 0; i < H; ++i) {
                int32_t x = v[i];
                mn = std::min(mn, (int)x);
                mx = std::max(mx, (int)x);
                if (x < 0)
                    lt0++;
                int32_t y = screlu_i16((int16_t)x); // 0..QA^2
                gt1 += (x > QA);
                zeros += (y == 0);
                ones += (y >= (QA * QA));
                v[i] = y;
                sum += y;
            }
            double mean_post = (double)sum / (double)H;
            std::cerr << tag << " activations:\n";
            std::cerr << "   pre: min=" << mn << " max=" << mx << "  (<0: " << lt0 << ", >QA: " << gt1 << ")\n"
                      << "   post screlu: mean=" << std::fixed << std::setprecision(3) << mean_post
                      << "  zeros=" << zeros << "  ones=" << ones << "\n";
        };
        post(s_stm, "STM");
        post(s_ntm, "NTM");

        // Output parts (integer path)
        long long y = 0;
        long long y_stm = 0, y_ntm = 0;
        for (int i = 0; i < H; ++i) {
            y_stm += (long long)s_stm[i] * (long long)L1W_q[i];
        }
        for (int i = 0; i < H; ++i) {
            y_ntm += (long long)s_ntm[i] * (long long)L1W_q[H + i];
        }
        y = y_stm + y_ntm;

        long long y_qaqb = y / QA + (long long)L1B_q; // in QA*QB units
        double y_cp = (double)y_qaqb * (double)SCALE / ((double)QA * (double)QB);

        std::cerr << std::fixed << std::setprecision(6);
        std::cerr << "Output parts:\n";
        std::cerr << "  bias      = " << (double)L1B_q / ((double)QA * (double)QB) << "   ("
                  << ((double)L1B_q * (double)SCALE / ((double)QA * (double)QB)) << " cp)\n";
        std::cerr << "  stm sum   = " << ((double)y_stm / ((double)QA * (double)QA * (double)QB)) << "   ("
                  << ((double)y_stm * (double)SCALE / ((double)QA * (double)QA * (double)QB)) << " cp)\n";
        std::cerr << "  ntm sum   = " << ((double)y_ntm / ((double)QA * (double)QA * (double)QB)) << "   ("
                  << ((double)y_ntm * (double)SCALE / ((double)QA * (double)QA * (double)QB)) << " cp)\n";
        std::cerr << "  TOTAL y   = " << (y_cp / (double)SCALE) << "   (" << y_cp << " cp)\n";

        // quick flip check (not guaranteed to negate)
        engine::Board flip = b;
        flip.side_to_move = (b.side_to_move == engine::WHITE) ? engine::BLACK : engine::WHITE;
        int cp_here = (int)std::lround(y_cp);
        int cp_flip = evaluate(flip);
        std::cerr << "Side-to-move flip check: this=" << cp_here << " cp, flipped=" << cp_flip << " cp\n";
    } else {
        // Float trace mirrors evaluate_float_raw
        std::vector<float> s_stm(L0Bf), s_ntm(L0Bf);
        for (int f : idx_stm) {
            const float* col = &L0W_Tf[(std::size_t)f * H];
            for (int i = 0; i < H; ++i)
                s_stm[i] += col[i];
        }
        for (int f : idx_ntm) {
            const float* col = &L0W_Tf[(std::size_t)f * H];
            for (int i = 0; i < H; ++i)
                s_ntm[i] += col[i];
        }

        auto post = [&](std::vector<float>& v, const char* tag) {
            int zeros = 0, ones = 0, gt1 = 0, lt0 = 0;
            double sum = 0.0, mn = v[0], mx = v[0];
            for (float& x : v) {
                mn = std::min<double>(mn, x);
                mx = std::max<double>(mx, x);
                if (x < 0.0f)
                    lt0++;
                float y = screlu_float(x);
                gt1 += (x > 1.0f);
                zeros += (y <= 0.0f);
                ones += (y >= 1.0f);
                x = y;
                sum += y;
            }
            std::cerr << tag << " activations:\n";
            std::cerr << std::fixed << std::setprecision(6) << "   pre: min=" << mn << " max=" << mx << "  (<0: " << lt0
                      << ", >1: " << gt1 << ")\n"
                      << "   post screlu: mean=" << (sum / (double)v.size()) << "  zeros=" << zeros << "  ones=" << ones
                      << "\n";
        };
        post(s_stm, "STM");
        post(s_ntm, "NTM");

        double y_bias = L1Bf, y_stm = 0.0, y_ntm = 0.0;
        for (int i = 0; i < H; ++i)
            y_stm += (double)L1Wf[i] * (double)s_stm[i];
        for (int i = 0; i < H; ++i)
            y_ntm += (double)L1Wf[H + i] * (double)s_ntm[i];
        double y = y_bias + y_stm + y_ntm;

        std::cerr << std::fixed << std::setprecision(6);
        std::cerr << "Output parts:\n";
        std::cerr << "  bias      = " << y_bias << "   (" << y_bias * 400.0 << " cp)\n";
        std::cerr << "  stm sum   = " << y_stm << "   (" << y_stm * 400.0 << " cp)\n";
        std::cerr << "  ntm sum   = " << y_ntm << "   (" << y_ntm * 400.0 << " cp)\n";
        std::cerr << "  TOTAL y   = " << y << "   (" << y * 400.0 << " cp)\n";

        engine::Board flip = b;
        flip.side_to_move = (b.side_to_move == engine::WHITE) ? engine::BLACK : engine::WHITE;
        int cp_here = to_centipawns((float)y);
        int cp_flip = evaluate(flip);
        std::cerr << "Side-to-move flip check: this=" << cp_here << " cp, flipped=" << cp_flip << " cp\n";
    }

    std::cerr << "===== end NNUE DIAG =====\n";
}

} // namespace eval
