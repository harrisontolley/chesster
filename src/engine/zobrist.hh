#pragma once
#include "board.hh"

#include <cstdint>

namespace engine {

struct Board;

namespace zobrist {

// one time init
void init();

// full recompute from board state
std::uint64_t compute(const Board& b);

// incremental helpers (used by make/unmake move)
// piece-square key
std::uint64_t psq(Colour c, Piece p, int sq);

// STM
std::uint64_t side();

// XOR of all activate castling rights
std::uint64_t castle_mask(const CastlingRights&);

// EP file
std::uint64_t ep_file(int file);

// EP component hashed only if an EP capture is actually possibly by STM
std::uint64_t ep_component(const Board& b, Colour stm);

} // namespace zobrist
} // namespace engine