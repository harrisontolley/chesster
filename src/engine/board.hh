#pragma once
#include "bitboard.hh"

#include <array>
#include <cstdint>
#include <optional>

namespace engine {

enum Colour { WHITE, BLACK };

enum Piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE };

struct CastlingRights {
    bool wk{true};
    bool wq{true};
    bool bk{true};
    bool bq{true};
};

struct Board {
    Bitboard pieces[2][6]{};
    Colour side_to_move{WHITE};
    CastlingRights castle{};
    std::optional<int> ep_square{}; // en passent square 0...63 if available.
    int halfmove_clock{0};
    int fullmove_number{1};

    static Board startpos();

    // zobrist key
    std::uint64_t zkey_{0};
    std::uint64_t zkey() const;
};

inline Bitboard occupancy(const Board& b, Colour c)
{
    Bitboard occ = 0;
    for (int p = PAWN; p <= KING; ++p)
        occ |= b.pieces[c][p];
    return occ;
}
inline Bitboard occupancy(const Board& b)
{
    return occupancy(b, WHITE) | occupancy(b, BLACK);
}

// Returns true for trivial insufficient material draws
// KK, KBK, KNK
bool trivial_insufficient_material(const Board& b);

} // namespace engine