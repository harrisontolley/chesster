#include "movegen.hh"
#include "attack_tables.hh"
#include "bitboard.hh"

namespace engine
{

    static void push(std::vector<Move> &out, Square f, Square t)
    {
        out.push_back(make_move(f, t));
    }

    std::vector<Move> generate_moves(const Board &b)
    {
        std::vector<Move> moves;
        Colour us = b.side_to_move;
        Bitboard occ = occupancy(b);
        Bitboard empty = ~occ;
        // —— Pawns —————————————————————————
        Bitboard pawns = b.pieces[us][PAWN];
        if (us == WHITE)
        {
            Bitboard single = north(pawns) & empty;
            Bitboard dbl = north(single & RankBB[2]) & empty;
            // single
            while (single)
            {
                Square to = Square(__builtin_ctzll(single));
                push(moves, Square(to - 8), to);
                single &= single - 1;
            }
            // double
            while (dbl)
            {
                Square to = Square(__builtin_ctzll(dbl));
                push(moves, Square(to - 16), to);
                dbl &= dbl - 1;
            }
        }
        else
        {
            Bitboard single = south(pawns) & empty;
            Bitboard dbl = south(single & RankBB[5]) & empty;
            while (single)
            {
                Square to = Square(__builtin_ctzll(single));
                push(moves, Square(to + 8), to);
                single &= single - 1;
            }
            while (dbl)
            {
                Square to = Square(__builtin_ctzll(dbl));
                push(moves, Square(to + 16), to);
                dbl &= dbl - 1;
            }
        }
        // —— Knights —————————————————————————
        Bitboard knights = b.pieces[us][KNIGHT];
        while (knights)
        {
            Square from = Square(__builtin_ctzll(knights));
            Bitboard att = KNIGHT_ATTACKS[from] & ~occupancy(b, us);
            while (att)
            {
                Square to = Square(__builtin_ctzll(att));
                push(moves, from, to);
                att &= att - 1;
            }
            knights &= knights - 1;
        }
        return moves;
    }

} // namespace engine
