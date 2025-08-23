#include "movegen.hh"
#include "attack_tables.hh"
#include "bitboard.hh"
#include <cstdint>

namespace engine
{

    static inline int pop_lsb(Bitboard &b)
    {
        int s = __builtin_ctzll(b);
        b &= b - 1;
        return s;
    }

    static inline void push(std::vector<Move> &out, int f, int t, int fl = QUIET)
    {
        out.push_back(make_move(f, t, fl));
    }

    static void gen_sliding(std::vector<Move> &out, [[maybe_unused]] const Board &b, Bitboard pieces, Bitboard occUs, Bitboard occThem, int deltas[4])
    {
        while (pieces)
        {
            int from = pop_lsb(pieces);
            Bitboard ray = (1ULL << from);
            for (int i = 0; i < 4; ++i)
            {
                Bitboard step = ray;
                while (true)
                {
                    switch (deltas[i])
                    {
                    case 1:
                        step = east(step);
                        break; // +1
                    case -1:
                        step = west(step);
                        break; // -1
                    case 8:
                        step = north(step);
                        break; // +8
                    case -8:
                        step = south(step);
                        break; // -8
                    case 9:
                        step = ne(step);
                        break; // +9
                    case 7:
                        step = nw(step);
                        break; // +7
                    case -7:
                        step = se(step);
                        break; // -7
                    case -9:
                        step = sw(step);
                        break; // -9
                    default:
                        step = 0;
                        break;
                    }
                    if (!step)
                        break;

                    int to = __builtin_ctzll(step);
                    if (step & occUs)
                        break;

                    if (step & occThem)
                    {
                        push(out, from, to, CAPTURE);
                        break;
                    }

                    push(out, from, to, QUIET);
                }
            }
        }
    }

    std::vector<Move> generate_moves(const Board &b)
    {
        std::vector<Move> moves;
        const Colour us = b.side_to_move;
        const Colour them = (us == WHITE) ? BLACK : WHITE;

        const Bitboard occAll = occupancy(b);
        const Bitboard occUs = occupancy(b, us);
        const Bitboard occThem = occupancy(b, them);

        Bitboard pawns = b.pieces[us][PAWN];

        if (us == WHITE)
        {
            Bitboard single = north(pawns) & ~occAll;

            Bitboard promos = single & RankBB[7];
            Bitboard quiets = single & ~RankBB[7];

            while (quiets)
            {
                int to = pop_lsb(quiets);
                push(moves, to - 8, to, QUIET);
            }

            while (promos)
            {
                int to = pop_lsb(promos);
                push(moves, to - 8, to, PROMO_N);
                push(moves, to - 8, to, PROMO_B);
                push(moves, to - 8, to, PROMO_R);
                push(moves, to - 8, to, PROMO_Q);
            }

            // double pushes (from rank 2 -> rank 4)
            Bitboard dbl = north(single & RankBB[2]) & ~occAll;
            while (dbl)
            {
                int to = pop_lsb(dbl);
                push(moves, to - 16, to, DOUBLE_PUSH);
            }

            // captures
            Bitboard capL = nw(pawns) & occThem;
            Bitboard capR = ne(pawns) & occThem;

            // nonpromo captures
            Bitboard capL_np = capL & ~RankBB[7];
            Bitboard capR_np = capR & ~RankBB[7];

            while (capL_np)
            {
                int to = pop_lsb(capL_np);
                push(moves, to - 7, to, CAPTURE);
            }
            while (capR_np)
            {
                int to = pop_lsb(capR_np);
                push(moves, to - 9, to, CAPTURE);
            }

            // capture promotions
            Bitboard capL_pr = capL & RankBB[7];
            Bitboard capR_pr = capR & RankBB[7];

            while (capL_pr)
            {
                int to = pop_lsb(capL_pr);
                int from = to - 7;

                push(moves, from, to, PROMO_N_CAPTURE);
                push(moves, from, to, PROMO_B_CAPTURE);
                push(moves, from, to, PROMO_R_CAPTURE);
                push(moves, from, to, PROMO_Q_CAPTURE);
            }

            while (capR_pr)
            {
                int to = pop_lsb(capR_pr);
                int from = to - 9;

                push(moves, from, to, PROMO_N_CAPTURE);
                push(moves, from, to, PROMO_B_CAPTURE);
                push(moves, from, to, PROMO_R_CAPTURE);
                push(moves, from, to, PROMO_Q_CAPTURE);
            }

            // en passant captures
            if (b.ep_square)
            {
                int eps = *b.ep_square;

                if (eps - 7 >= 0 && (pawns & (1ULL << (eps - 7))))
                    push(moves, eps - 7, eps, EN_PASSANT);
                if (eps - 9 >= 0 && (pawns & (1ULL << (eps - 9))))
                    push(moves, eps - 9, eps, EN_PASSANT);
            }
        }
        else // BLACK STM
        {
            // single pushes
            Bitboard single = south(pawns) & ~occAll;
            // promotions by single push
            Bitboard promos = single & RankBB[0];
            Bitboard quiets = single & ~RankBB[0];
            while (quiets)
            {
                int to = pop_lsb(quiets);
                push(moves, to + 8, to, QUIET);
            }
            while (promos)
            {
                int to = pop_lsb(promos);
                int from = to + 8;
                push(moves, from, to, PROMO_N);
                push(moves, from, to, PROMO_B);
                push(moves, from, to, PROMO_R);
                push(moves, from, to, PROMO_Q);
            }

            // double pushes (from rank 7 -> rank 5)
            Bitboard dbl = south(single & RankBB[5]) & ~occAll;
            while (dbl)
            {
                int to = pop_lsb(dbl);
                push(moves, to + 16, to, DOUBLE_PUSH);
            }

            // captures
            Bitboard capL = sw(pawns) & occThem; // to = from - 9
            Bitboard capR = se(pawns) & occThem; // to = from - 7

            // non-promo captures
            Bitboard capL_np = capL & ~RankBB[0];
            Bitboard capR_np = capR & ~RankBB[0];
            while (capL_np)
            {
                int to = pop_lsb(capL_np);
                push(moves, to + 9, to, CAPTURE);
            }
            while (capR_np)
            {
                int to = pop_lsb(capR_np);
                push(moves, to + 7, to, CAPTURE);
            }

            // capture promotions
            Bitboard capL_pr = capL & RankBB[0];
            Bitboard capR_pr = capR & RankBB[0];
            while (capL_pr)
            {
                int to = pop_lsb(capL_pr);
                int from = to + 9;
                push(moves, from, to, PROMO_N_CAPTURE);
                push(moves, from, to, PROMO_B_CAPTURE);
                push(moves, from, to, PROMO_R_CAPTURE);
                push(moves, from, to, PROMO_Q_CAPTURE);
            }
            while (capR_pr)
            {
                int to = pop_lsb(capR_pr);
                int from = to + 7;
                push(moves, from, to, PROMO_N_CAPTURE);
                push(moves, from, to, PROMO_B_CAPTURE);
                push(moves, from, to, PROMO_R_CAPTURE);
                push(moves, from, to, PROMO_Q_CAPTURE);
            }

            // en passant
            if (b.ep_square)
            {
                int eps = *b.ep_square;
                // from squares that land on eps: from = eps+7 or eps+9 if a black pawn is there
                if (eps + 7 <= 63 && (pawns & (1ULL << (eps + 7))))
                    push(moves, eps + 7, eps, EN_PASSANT);
                if (eps + 9 <= 63 && (pawns & (1ULL << (eps + 9))))
                    push(moves, eps + 9, eps, EN_PASSANT);
            }
        }

        // KNIGHTS
        Bitboard knights = b.pieces[us][KNIGHT];
        while (knights)
        {
            int from = pop_lsb(knights);

            Bitboard att = KNIGHT_ATTACKS[from] & ~occUs;
            Bitboard caps = att & occThem;
            Bitboard quiet = att & ~occThem;

            while (quiet)
            {
                int to = pop_lsb(quiet);
                push(moves, from, to, QUIET);
            }

            while (caps)
            {
                int to = pop_lsb(caps);
                push(moves, from, to, CAPTURE);
            }
        }

        // BISHOPS

        {
            int deltas[4] = {9, 7, -7, -9};
            gen_sliding(moves, b, b.pieces[us][BISHOP], occUs, occThem, deltas);
        }

        // ROOKS
        {
            int deltas[4] = {1, -1, 8, -8};
            gen_sliding(moves, b, b.pieces[us][ROOK], occUs, occThem, deltas);
        }

        // QUEENS
        {
            int deltas8[4] = {1, -1, 8, -8};
            int deltasD[4] = {9, 7, -7, -9};
            gen_sliding(moves, b, b.pieces[us][QUEEN], occUs, occThem, deltas8);
            gen_sliding(moves, b, b.pieces[us][QUEEN], occUs, occThem, deltasD);
        }

        // king  !!! (no castling yet) !!!
        Bitboard king = b.pieces[us][KING];
        if (king)
        {
            int from = __builtin_ctzll(king);
            Bitboard kMoves =
                (north(king) | south(king) | east(king) | west(king) |
                 ne(king) | nw(king) | se(king) | sw(king)) &
                ~occUs;

            Bitboard caps = kMoves & occThem;
            Bitboard quiet = kMoves & ~occThem;

            while (quiet)
            {
                int to = pop_lsb(quiet);
                push(moves, from, to, QUIET);
            }

            while (caps)
            {
                int to = pop_lsb(caps);
                push(moves, from, to, CAPTURE);
            }
        }

        return moves;
    }
}
