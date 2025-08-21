#pragma once
#include <cstdint>

namespace engine
{
    using Move = std::uint16_t; // 6 bits from, 6 bits to, 4 bits flags

    constexpr Move make_move(int from, int to, int flags = 0)
    {
        return static_cast<Move>((from) | (to << 6) | (flags << 12));
    }
}