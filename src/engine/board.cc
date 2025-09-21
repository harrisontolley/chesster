#include "board.hh"

#include "bitboard.hh"
#include "zobrist.hh"

#include <cstdint>

namespace engine {

static inline int popcnt_u64(std::uint64_t x)
{
#if defined(__GNUG__) || defined(__clang__)
    return __builtin_popcountll(x);
#else
    // Portable fallback
    int c = 0;
    while (x) {
        x &= (x - 1);
        ++c;
    }
    return c;
#endif
}

Board Board::startpos()
{
    Board b{};

    /* White pieces */
    b.pieces[WHITE][PAWN] = 0x000000000000FF00ULL;
    b.pieces[WHITE][ROOK] = 0x0000000000000081ULL;
    b.pieces[WHITE][KNIGHT] = 0x0000000000000042ULL;
    b.pieces[WHITE][BISHOP] = 0x0000000000000024ULL;
    b.pieces[WHITE][QUEEN] = 0x0000000000000008ULL;
    b.pieces[WHITE][KING] = 0x0000000000000010ULL;

    /* Black pieces */
    b.pieces[BLACK][PAWN] = 0x00FF000000000000ULL;
    b.pieces[BLACK][ROOK] = 0x8100000000000000ULL;
    b.pieces[BLACK][KNIGHT] = 0x4200000000000000ULL;
    b.pieces[BLACK][BISHOP] = 0x2400000000000000ULL;
    b.pieces[BLACK][QUEEN] = 0x0800000000000000ULL;
    b.pieces[BLACK][KING] = 0x1000000000000000ULL;

    b.side_to_move = WHITE;
    zobrist::init();
    b.zkey_ = zobrist::compute(b);

    return b;
}

std::uint64_t Board::zkey() const
{
    return zkey_;
}

bool trivial_insufficient_material(const Board& b)
{

    // any pawns, queens, rooks present? if so, not draw
    if ((b.pieces[WHITE][PAWN] | b.pieces[BLACK][PAWN] | b.pieces[WHITE][QUEEN] | b.pieces[BLACK][QUEEN] |
         b.pieces[WHITE][ROOK] | b.pieces[BLACK][ROOK]) != 0ULL) {
        return false;
    }

    const int wB = popcnt_u64(b.pieces[WHITE][BISHOP]);
    const int bB = popcnt_u64(b.pieces[BLACK][BISHOP]);
    const int wN = popcnt_u64(b.pieces[WHITE][KNIGHT]);
    const int bN = popcnt_u64(b.pieces[BLACK][KNIGHT]);

    const int wNonKing = wB + wN;
    const int bNonKing = bB + bN;

    const int total = wNonKing + bNonKing;

    // KK
    if (total == 0)
        return true;

    // exactly 1 minor on entire board
    if (total == 1)
        return true;

    // intentionally do not count K+N vs K+N or K+B vs K+B as draw
    return false;
}

} // namespace engine