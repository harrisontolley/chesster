#include "eval.hh"
#include "../engine/board.hh"
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace eval
{
    // Layout (TrainerBuilder):
    // l0w: [H x 768] row-major (we'll transpose to [768 x H] for fast column adds)
    // l0b: [H]
    // l1w: [2H]  -> first H for stm, next H for ntm
    // l1b: [1]

    static std::vector<float> L0W_T; // transposed: [768 * H], column-major by feature
    static std::vector<float> L0B;   // [H]
    static std::vector<float> L1W;   // [2H]
    static float L1B = 0.0f;
    static int H = 0;
    static bool READY = false;

    static inline float screlu(float x)
    {
        constexpr float CAP = 1.0f;
        if (x <= 0.0f)
            return 0.0f;
        return x >= CAP ? CAP : x;
    }

    // Build sparse feature indices for "ref" colour (our pieces first, then their pieces)
    // Feature index: ((c==ref ? 0 : 6) + piece) * 64 + sq
    static void fill_indices(const engine::Board &b, engine::Colour ref, std::vector<int> &idx)
    {
        idx.clear();
        idx.reserve(32); // typical number of pieces
        for (int c = engine::WHITE; c <= engine::BLACK; ++c)
        {
            for (int p = engine::PAWN; p <= engine::KING; ++p)
            {
                engine::Bitboard bb = b.pieces[c][p];
                if (!bb)
                    continue;
                const int chanBase = ((c == ref) ? 0 : 6) + p; // p in [0..5]
                const int base = chanBase * 64;
                while (bb)
                {
                    int sq = __builtin_ctzll(bb);
                    bb &= (bb - 1);
                    idx.push_back(base + sq);
                }
            }
        }
    }

    // Try to open either a raw.bin directly or a checkpoint directory containing raw.bin
    static std::ifstream open_raw(const char *path)
    {
        std::ifstream in;
        if (!path || !*path)
        {
            if (const char *env = std::getenv("CHESSTER_NET"))
            {
                in.open(env, std::ios::binary);
                if (in)
                    return in;
                // try as directory/env + "/raw.bin"
                std::string alt = std::string(env) + "/raw.bin";
                in.open(alt, std::ios::binary);
                return in;
            }
            return in;
        }
        // first try exactly
        in.open(path, std::ios::binary);
        if (in)
            return in;
        // then try as directory + "/raw.bin"
        std::string alt = std::string(path);
        if (!alt.empty() && alt.back() != '/' && alt.back() != '\\')
            alt += "/";
        alt += "raw.bin";
        in.open(alt, std::ios::binary);
        return in;
    }

    bool load_weights(const char *path)
    {
        READY = false;
        L0W_T.clear();
        L0B.clear();
        L1W.clear();
        L1B = 0.0f;
        H = 0;

        std::ifstream in = open_raw(path);
        if (!in)
            return false;

        in.seekg(0, std::ios::end);
        std::streamoff bytes = in.tellg();
        in.seekg(0, std::ios::beg);
        if (bytes <= 0 || (bytes % 4) != 0)
            return false;

        const std::size_t n_floats = static_cast<std::size_t>(bytes) / 4;
        // n = H*(768 + 1 + 2) + 1 = H*771 + 1
        if (n_floats < 1 || ((n_floats - 1) % 771) != 0)
            return false;

        H = static_cast<int>((n_floats - 1) / 771);
        std::vector<float> buf(n_floats);
        in.read(reinterpret_cast<char *>(buf.data()), bytes);
        if (!in)
            return false;

        // Slice out the blocks from the raw (row-major l0w)
        std::size_t off = 0;
        const std::size_t l0w_sz = static_cast<std::size_t>(H) * 768;
        const std::size_t l0b_sz = static_cast<std::size_t>(H);
        const std::size_t l1w_sz = static_cast<std::size_t>(2 * H);

        const float *L0W_row = buf.data();          // [H x 768]
        const float *L0B_raw = buf.data() + l0w_sz; // [H]
        const float *L1W_raw = L0B_raw + l0b_sz;    // [2H]
        const float L1B_raw = *(L1W_raw + l1w_sz);  // [1]

        // Copy biases and output weights
        L0B.assign(L0B_raw, L0B_raw + H);
        L1W.assign(L1W_raw, L1W_raw + 2 * H);
        L1B = L1B_raw;

        // Transpose l0w to [768 x H] for fast "add column" updates
        L0W_T.assign(static_cast<std::size_t>(768) * H, 0.0f);
        for (int i = 0; i < H; ++i)
        {
            const float *row = L0W_row + static_cast<std::size_t>(i) * 768;
            for (int j = 0; j < 768; ++j)
            {
                L0W_T[static_cast<std::size_t>(j) * H + i] = row[j];
            }
        }

        READY = true;
        return true;
    }

    static inline int to_centipawns(float y)
    {
        constexpr float EVAL_SCALE = 400.0f;
        float cp = y * EVAL_SCALE;
        if (cp > 20000)
            cp = 20000.0f;
        if (cp < -20000)
            cp = -20000.0f;
        return static_cast<int>(cp);
    }

    int evaluate(const engine::Board &b)
    {
        if (!READY)
            throw std::runtime_error("NNUE not ready");

        const engine::Colour stm = b.side_to_move;
        const engine::Colour ntm = (stm == engine::WHITE) ? engine::BLACK : engine::WHITE;

        // 1 gather sparse features
        std::vector<int> idx_stm, idx_ntm;
        fill_indices(b, stm, idx_stm);
        fill_indices(b, ntm, idx_ntm);

        // 2 hidden sums start at bias
        std::vector<float> h_stm(L0B);
        std::vector<float> h_ntm(L0B);

        // 3 add columns for each active feature
        //    L0W_T is [768 x H], so column for feature f starts at &L0W_T[f*H]
        for (int f : idx_stm)
        {
            const float *col = &L0W_T[static_cast<std::size_t>(f) * H];
            for (int i = 0; i < H; ++i)
                h_stm[i] += col[i];
        }
        for (int f : idx_ntm)
        {
            const float *col = &L0W_T[static_cast<std::size_t>(f) * H];
            for (int i = 0; i < H; ++i)
                h_ntm[i] += col[i];
        }

        // 4 screlu
        for (int i = 0; i < H; ++i)
            h_stm[i] = screlu(h_stm[i]);
        for (int i = 0; i < H; ++i)
            h_ntm[i] = screlu(h_ntm[i]);

        // 5 output
        float y = L1B;
        const float *w_stm = L1W.data();
        const float *w_ntm = L1W.data() + H;
        for (int i = 0; i < H; ++i)
            y += w_stm[i] * h_stm[i];
        for (int i = 0; i < H; ++i)
            y += w_ntm[i] * h_ntm[i];

        return to_centipawns(y);
    }

}
