#pragma once
#include <string>
#include "board.hh"

// Extracts and converts between FEN strings and Board objects
namespace engine
{
    Board from_fen(const std::string &fen);
    std::string to_fen(const Board &board);
}