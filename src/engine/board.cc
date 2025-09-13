#include "board.hh"

namespace engine {

Board Board::startpos()
{
    Board b;

    /* White pieces */
    b.pieces[WHITE][PAWN] = 0x000000000000FF00ULL;
    b.pieces[WHITE][ROOK] = 0x0000000000000081ULL;
    b.pieces[WHITE][KNIGHT] = 0x0000000000000042ULL;
    b.pieces[WHITE][BISHOP] = 0x0000000000000024ULL;
    b.pieces[WHITE][QUEEN] = 0x0000000000000008ULL;
    b.pieces[WHITE][KING] = 0x0000000000000010ULL;

    /* Black pieces */
    b.pieces[BLACK][PAWN] = 0x00FF000000000000ULL;
    b.pieces[BLACK][ROOK] = 0x8100000000000000ULL;
    b.pieces[BLACK][KNIGHT] = 0x4200000000000000ULL;
    b.pieces[BLACK][BISHOP] = 0x2400000000000000ULL;
    b.pieces[BLACK][QUEEN] = 0x0800000000000000ULL;
    b.pieces[BLACK][KING] = 0x1000000000000000ULL;

    b.side_to_move = WHITE;
    return b;
}
} // namespace engine