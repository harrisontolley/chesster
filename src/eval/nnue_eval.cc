// --- src/eval/nnue_eval.cc ---
#include "eval.hh"
#include "../engine/board.hh"
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <optional>
#include <sys/stat.h>

namespace eval
{

    // simple 1-hidden layer perspective NNUE.
    // Layout in raw.bin (float32, little-endian):
    // l0w: [768 x H], l0b: [H], l1w: [2H x 1], l1b: [1]
    // => total floats = H*771 + 1

    static std::vector<float> L0W;
    static std::vector<float> L0B;
    static std::vector<float> L1W;
    static float L1B = 0.0f;
    static int H = 0; // hidden size read from file.
    static bool READY = false;

    static inline float screlu(float x)
    {
        constexpr float CAP = 1.0f;
        if (x <= 0.0f)
            return 0.0f;
        return x >= CAP ? CAP : x;
    }

    // build 768-d sparse binary feature vector from the perspective of ref (friendly first)
    static void fill_chess768_inputs(const engine::Board &b, engine::Colour ref, std::vector<float> &x)
    {
        x.assign(768, 0.0f);
        for (int c = engine::WHITE; c <= engine::BLACK; ++c)
        {
            for (int p = engine::PAWN; p <= engine::KING; ++p)
            {
                engine::Bitboard bb = b.pieces[c][p];
                int chanBase = ((c == ref) ? 0 : 6) + p; // 0..5 for ref, 6..11 for opp
                while (bb)
                {
                    int sq = __builtin_ctzll(bb);
                    bb &= (bb - 1);
                    x[chanBase * 64 + sq] = 1.0f;
                }
            }
        }
    }

    static inline int to_centipawns(float y)
    {
        constexpr float EVAL_SCALE = 400.0f; // match trainer schedule
        float cp = y * EVAL_SCALE;
        if (cp > 20000)
            cp = 20000.0f;
        if (cp < -20000)
            cp = -20000.0f;
        return static_cast<int>(cp);
    }

    static bool is_dir(const std::string &p)
    {
        struct stat st{};
        if (stat(p.c_str(), &st) != 0)
            return false;
        return S_ISDIR(st.st_mode);
    }

    bool load_weights(const char *path)
    {
        READY = false;
        L0W.clear();
        L0B.clear();
        L1W.clear();
        L1B = 0.0f;
        H = 0;

        std::string fpath;
        if (path && *path)
            fpath = path;
        else if (const char *env = std::getenv("CHESSTER_NET"))
            fpath = env;

        if (fpath.empty())
            throw std::runtime_error("eval::load_weights: no path (set UCI option EvalFile or CHESSTER_NET)");

        // Allow passing the *checkpoint directory*; auto-append raw.bin
        if (is_dir(fpath))
        {
            if (fpath.back() != '/' && fpath.back() != '\\')
                fpath += '/';
            fpath += "raw.bin";
        }

        std::ifstream in(fpath, std::ios::binary);
        if (!in)
            return false;

        in.seekg(0, std::ios::end);
        std::streamoff bytes = in.tellg();
        in.seekg(0, std::ios::beg);
        if (bytes <= 0 || (bytes % 4) != 0)
            return false;

        const std::size_t n_floats = static_cast<std::size_t>(bytes) / 4;
        if ((n_floats < 1) || ((n_floats - 1) % 771) != 0)
            return false;

        H = static_cast<int>((n_floats - 1) / 771);
        if (H <= 0)
            return false;

        std::vector<float> buf(n_floats);
        in.read(reinterpret_cast<char *>(buf.data()), bytes);
        if (!in)
            return false;

        std::size_t off = 0;
        const std::size_t l0w_sz = static_cast<std::size_t>(768) * H;
        const std::size_t l0b_sz = static_cast<std::size_t>(H);
        const std::size_t l1w_sz = static_cast<std::size_t>(2 * H);

        L0W.assign(buf.begin() + off, buf.begin() + off + l0w_sz);
        off += l0w_sz;
        L0B.assign(buf.begin() + off, buf.begin() + off + l0b_sz);
        off += l0b_sz;
        L1W.assign(buf.begin() + off, buf.begin() + off + l1w_sz);
        off += l1w_sz;
        L1B = buf[off];
        READY = true;
        return true;
    }

    int evaluate(const engine::Board &b)
    {
        if (!READY)
            throw std::runtime_error("NNUE not ready (call load_weights)");

        const engine::Colour stm = b.side_to_move;
        const engine::Colour ntm = (stm == engine::WHITE) ? engine::BLACK : engine::WHITE;

        std::vector<float> x_stm, x_ntm;
        fill_chess768_inputs(b, stm, x_stm);
        fill_chess768_inputs(b, ntm, x_ntm);

        // Hidden for stm
        std::vector<float> h_stm(H), h_ntm(H);
        for (int i = 0; i < H; ++i)
        {
            const float *wrow = &L0W[i * 768];
            float a = L0B[i];
            float b2 = L0B[i];
            for (int j = 0; j < 768; ++j)
            {
                a += wrow[j] * x_stm[j];
                b2 += wrow[j] * x_ntm[j];
            }
            h_stm[i] = screlu(a);
            h_ntm[i] = screlu(b2);
        }

        float y = L1B;
        for (int i = 0; i < H; ++i)
            y += L1W[i] * h_stm[i];
        for (int i = 0; i < H; ++i)
            y += L1W[H + i] * h_ntm[i];

        return to_centipawns(y); // stm-relative centipawns
    }

} // namespace eval
