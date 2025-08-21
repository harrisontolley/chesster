#pragma once
#include <vector>
#include "board.hh"
#include "move.hh"

namespace engine
{
    using Move = std::uint16_t;
    // bits 0-5: from square
    // bits 6-11: to square
    // bits 12-15: flags

    std::vector<Move> generate_moves(const Board &);

    enum MoveFlags : uint16_t
    {
        QUIET = 0,
        CAPTURE = 1,
        EP = 2,
        KING_CASTLE = 3,
        QUEEN_CASTLE = 4,
        PROMO_N = 8,
        PROMO_B = 9,
        PROMO_R = 10,
        PROMO_Q = 11
    }; // + CAPTURE to mark capture promos

    constexpr Move make_move(int from, int to, int flags = QUIET)
    {
        // bit shift into correct parts of move number
        return static_cast<Move>((from & 63) | ((to & 63) << 6) | ((flags & 15) << 12));
    }

    // accessors for move components
    constexpr int from_sq(Move m) { return m & 63; }
    constexpr int to_sq(Move m) { return (m >> 6) & 63; }
    constexpr int flags(Move m) { return (m >> 12) & 15; }
    constexpr bool is_capture(Move m) { return flags(m) & CAPTURE; }
}