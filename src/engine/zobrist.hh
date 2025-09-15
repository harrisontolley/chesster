#pragma once
#include <cstdint>

namespace engine {

struct Board;

namespace zobrist {
void init();
std::uint64_t compute(const Board& b);
} // namespace zobrist
} // namespace engine