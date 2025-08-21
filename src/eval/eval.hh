#pragma once
#include <cstdint>

namespace engine
{
    struct Board;
}

namespace eval
{
    int evaluate(const engine::Board &); // in centipawns
    bool load_weights(const char *path = nullptr);
}
