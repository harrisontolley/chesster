#pragma once
#include "board.hh"
#include "move.hh"
#include "move_do.hh"

#include <string>

namespace engine {

inline int king_sq(const Board& b, Colour c)
{
    Bitboard k = b.pieces[c][KING];
#if defined(__GNUG__) || defined(__clang__)
    return k ? __builtin_ctzll(k) : -1;
#else
    if (!k)
        return -1;
    int idx = 0;
    while ((k & 1ULL) == 0ULL) {
        k >>= 1;
        ++idx;
    }
    return idx;
#endif
}

inline std::string sq_to_str(int sq)
{
    if (sq < 0 || sq > 63)
        return "??";

    char f = char('a' + (sq & 7));
    char r = char('1' + (sq >> 3));

    return std::string() + f + r;
}

inline char promotion_char(Move m)
{
    switch (flag(m)) {
    case PROMO_Q:
    case PROMO_Q_CAPTURE:
        return 'q';
    case PROMO_R:
    case PROMO_R_CAPTURE:
        return 'r';
    case PROMO_B:
    case PROMO_B_CAPTURE:
        return 'b';
    case PROMO_N:
    case PROMO_N_CAPTURE:
        return 'n';
    default:
        return '\0';
    }
}

// Piece 'values' for MVV/LVA (relative ordering)
static inline int val_cp(Piece p)
{
    switch (p) {
    case PAWN:
        return 100;
    case KNIGHT:
        return 320;
    case BISHOP:
        return 330;
    case ROOK:
        return 500;
    case QUEEN:
        return 900;
    case KING:
        return 20000;
    default:
        return 0;
    }
}

static inline int promo_gain_cp(int fl)
{
    switch (fl) {
    case PROMO_Q:
    case PROMO_Q_CAPTURE:
        return 900 - 100;
    case PROMO_R:
    case PROMO_R_CAPTURE:
        return 500 - 100;
    case PROMO_B:
    case PROMO_B_CAPTURE:
        return 330 - 100;
    case PROMO_N:
    case PROMO_N_CAPTURE:
        return 320 - 100;
    default:
        return 0;
    }
}

static inline int pop_lsb(std::uint64_t& b)
{
    int s = __builtin_ctzll(b);
    b &= (b - 1);
    return s;
}

static inline bool in_check(const Board& b)
{
    const Colour us = b.side_to_move;
    const Colour them = (us == WHITE ? BLACK : WHITE);
    int ksq = king_sq(b, us);
    return ksq >= 0 && is_square_attacked(b, ksq, them);
}

inline std::string move_to_uci(Move m)
{
    std::string s = sq_to_str(from_sq(m)) + sq_to_str(to_sq(m));
    if (char pc = promotion_char(m))
        s += pc;
    return s;
}

inline bool is_promo_any(Move m)
{
    const int f = flag(m);
    return f >= PROMO_N && f <= PROMO_Q_CAPTURE;
}

inline bool is_promo_noncap(Move m)
{
    const int f = flag(m);
    return (f == PROMO_N || f == PROMO_B || f == PROMO_R || f == PROMO_Q);
}

inline Piece promo_piece_from_flag(int fl)
{
    switch (fl) {
    case PROMO_N:
    case PROMO_N_CAPTURE:
        return KNIGHT;
    case PROMO_B:
    case PROMO_B_CAPTURE:
        return BISHOP;
    case PROMO_R:
    case PROMO_R_CAPTURE:
        return ROOK;
    case PROMO_Q:
    case PROMO_Q_CAPTURE:
        return QUEEN;
    default:
        return NO_PIECE;
    }
}

inline Piece piece_on(const Board& b, Colour c, int sq)
{
    Bitboard mask = (1ULL << sq);
    for (int p = PAWN; p <= KING; ++p)
        if (b.pieces[c][p] & mask)
            return static_cast<Piece>(p);
    return NO_PIECE;
}

} // namespace engine