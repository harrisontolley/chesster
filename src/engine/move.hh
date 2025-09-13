#pragma once
#include <cstdint>

namespace engine {

// 16-bit move: [0..5]=from, [6..11]=to, [12..15]=flags
using Move = std::uint16_t;

enum MoveFlag : uint16_t {
    QUIET = 0,
    DOUBLE_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EN_PASSANT = 5,

    // Promotions (non-captures)
    PROMO_N = 8,
    PROMO_B = 9,
    PROMO_R = 10,
    PROMO_Q = 11,

    // Promotion captures
    PROMO_N_CAPTURE = 12,
    PROMO_B_CAPTURE = 13,
    PROMO_R_CAPTURE = 14,
    PROMO_Q_CAPTURE = 15
};

constexpr Move make_move(int from, int to, int flags = QUIET)
{
    return static_cast<Move>((from & 63) | ((to & 63) << 6) | ((flags & 15) << 12));
}
constexpr int from_sq(Move m)
{
    return m & 63;
}
constexpr int to_sq(Move m)
{
    return (m >> 6) & 63;
}
constexpr int flag(Move m)
{
    return (m >> 12) & 15;
}
constexpr bool is_capture(Move m)
{
    const int f = flag(m);
    return f == CAPTURE || f == EN_PASSANT || f >= PROMO_N_CAPTURE;
}

} // namespace engine
