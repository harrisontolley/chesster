#pragma once
#include <array>
#include <cstdint>

namespace engine
{
    using Bitboard = uint64_t;
    enum Colour
    {
        WHITE,
        BLACK
    };
    enum Piece
    {
        PAWN,
        KNIGHT,
        BISHOP,
        ROOK,
        QUEEN,
        KING,
        NO_PIECE
    };

    struct Board
    {
        Bitboard pieces[2][6]{}; // pieces[colour][piece]
        Colour side_to_move{WHITE};
        // castling, ep, halfmove

        static Board startpos();
    };

    inline Bitboard occupancy(const Board &b, Colour c)
    {
        Bitboard occ = 0;
        for (int p = PAWN; p <= KING; ++p)
            occ |= b.pieces[c][p];
        return occ;
    }
    inline Bitboard occupancy(const Board &b)
    {
        return occupancy(b, WHITE) | occupancy(b, BLACK);
    }
}