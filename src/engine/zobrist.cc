#include "zobrist.hh"

#include "bitboard.hh"
#include "board.hh"

namespace engine {
namespace zobrist {

static std::uint64_t Z_PSQ[2][6][64]; // colour, piece, square
static std::uint64_t Z_SIDE;
static std::uint64_t Z_CASTLE_WK, Z_CASTLE_WQ, Z_CASTLE_BK, Z_CASTLE_BQ;
static std::uint64_t Z_EP_FILE[8];

// SplitMix64: deterministic generator for table fill
static inline std::uint64_t splitmix64(std::uint64_t& x)
{
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void ensure_init()
{
    static bool inited = false;
    if (inited)
        return;

    std::uint64_t seed = 0xC0FFEE5EED5BADULL; // fixed seed for reproducibility
    for (int c = 0; c < 2; ++c)
        for (int p = 0; p < 6; ++p)
            for (int s = 0; s < 64; ++s)
                Z_PSQ[c][p][s] = splitmix64(seed);

    Z_SIDE = splitmix64(seed);

    Z_CASTLE_WK = splitmix64(seed);
    Z_CASTLE_WQ = splitmix64(seed);
    Z_CASTLE_BK = splitmix64(seed);
    Z_CASTLE_BQ = splitmix64(seed);

    for (int f = 0; f < 8; ++f)
        Z_EP_FILE[f] = splitmix64(seed);

    inited = true;
}

void init()
{
    ensure_init();
}

// Include EP file only if a pawn could actually capture there (classic approach)
static inline bool include_ep_file(const Board& b, int epsq)
{
    if (epsq < 0 || epsq > 63)
        return false;
    const Bitboard target = (1ULL << epsq);
    if (b.side_to_move == WHITE) {
        // white pawns would be on the rank below epsq (south)
        return ((se(target) | sw(target)) & b.pieces[WHITE][PAWN]) != 0ULL;
    } else {
        // black pawns would be on the rank above epsq (north)
        return ((ne(target) | nw(target)) & b.pieces[BLACK][PAWN]) != 0ULL;
    }
}

std::uint64_t compute(const Board& b)
{
    ensure_init();

    std::uint64_t k = 0;

    for (int c = 0; c < 2; ++c)
        for (int p = 0; p < 6; ++p) {
            Bitboard bb = b.pieces[c][p];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                bb &= (bb - 1);
                k ^= Z_PSQ[c][p][sq];
            }
        }

    // side to move
    if (b.side_to_move == BLACK)
        k ^= Z_SIDE;

    // castling rights
    if (b.castle.wk)
        k ^= Z_CASTLE_WK;
    if (b.castle.wq)
        k ^= Z_CASTLE_WQ;
    if (b.castle.bk)
        k ^= Z_CASTLE_BK;
    if (b.castle.bq)
        k ^= Z_CASTLE_BQ;

    // en passant file (only if capturable)
    if (b.ep_square && include_ep_file(b, *b.ep_square)) {
        int file = *b.ep_square & 7;
        k ^= Z_EP_FILE[file];
    }

    return k;
}

} // namespace zobrist
} // namespace engine