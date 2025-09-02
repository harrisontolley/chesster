#include "movegen.hh"
#include "attack_tables.hh"
#include "bitboard.hh"
#include "move_do.hh" // use is_square_attacked, Undo, make/unmake
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
                        break;
                    case -1:
                        step = west(step);
                        break;
                    case 8:
                        step = north(step);
                        break;
                    case -8:
                        step = south(step);
                        break;
                    case 9:
                        step = ne(step);
                        break;
                    case 7:
                        step = nw(step);
                        break;
                    case -7:
                        step = se(step);
                        break;
                    case -9:
                        step = sw(step);
                        break;
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
            // single pushes
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

            // double pushes (rank 2 -> rank 4)
            Bitboard dbl = north(single & RankBB[2]) & ~occAll;
            while (dbl)
            {
                int to = pop_lsb(dbl);
                push(moves, to - 16, to, DOUBLE_PUSH);
            }

            // captures
            Bitboard capL = nw(pawns) & occThem;
            Bitboard capR = ne(pawns) & occThem;

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

            // en passant
            if (b.ep_square)
            {
                int eps = *b.ep_square;

                // Ensure an enemy pawn actually double-pushed to create this ep square
                if (eps >= 8 && (b.pieces[BLACK][PAWN] & (1ULL << (eps - 8))))
                {
                    Bitboard target = 1ULL << eps;

                    // From squares are eps-9 (NE from white pawn) and eps-7 (NW from white pawn)
                    if (ne(pawns) & target)
                        push(moves, eps - 9, eps, EN_PASSANT);
                    if (nw(pawns) & target)
                        push(moves, eps - 7, eps, EN_PASSANT);
                }
            }
        }
        else // BLACK
        {
            // single pushes
            Bitboard single = south(pawns) & ~occAll;

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

            // double pushes (rank 7 -> rank 5)
            Bitboard dbl = south(single & RankBB[5]) & ~occAll;
            while (dbl)
            {
                int to = pop_lsb(dbl);
                push(moves, to + 16, to, DOUBLE_PUSH);
            }

            // captures
            Bitboard capL = sw(pawns) & occThem; // to = from - 9
            Bitboard capR = se(pawns) & occThem; // to = from - 7

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

            if (b.ep_square)
            {
                int eps = *b.ep_square;

                // Ensure an enemy pawn actually double-pushed to create this ep square
                if (eps <= 55 && (b.pieces[WHITE][PAWN] & (1ULL << (eps + 8))))
                {
                    Bitboard target = 1ULL << eps;

                    // From squares are eps+7 (SE from black pawn) and eps+9 (SW from black pawn)
                    if (se(pawns) & target)
                        push(moves, eps + 7, eps, EN_PASSANT);
                    if (sw(pawns) & target)
                        push(moves, eps + 9, eps, EN_PASSANT);
                }
            }
        }

        // Knights
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

        // Bishops
        {
            int deltas[4] = {9, 7, -7, -9};
            gen_sliding(moves, b, b.pieces[us][BISHOP], occUs, occThem, deltas);
        }

        // Rooks
        {
            int deltas[4] = {1, -1, 8, -8};
            gen_sliding(moves, b, b.pieces[us][ROOK], occUs, occThem, deltas);
        }

        // Queens
        {
            int deltas8[4] = {1, -1, 8, -8};
            int deltasD[4] = {9, 7, -7, -9};
            gen_sliding(moves, b, b.pieces[us][QUEEN], occUs, occThem, deltas8);
            gen_sliding(moves, b, b.pieces[us][QUEEN], occUs, occThem, deltasD);
        }

        // King (+ castling, pseudo-legal)
        {
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

                // Castling (still pseudo-legal here; legality filter will remove if king in check after)
                if (us == WHITE)
                {
                    const bool kingOnE1 = (b.pieces[WHITE][KING] & (1ULL << E1)) != 0;
                    if (b.castle.wk && kingOnE1)
                    {
                        bool rookOnH1 = (b.pieces[WHITE][ROOK] & (1ULL << H1)) != 0;
                        bool pathEmpty = (occAll & ((1ULL << D1) | (1ULL << C1))) == 0;
                        bool safe = !is_square_attacked(b, E1, them) &&
                                    !is_square_attacked(b, F1, them) &&
                                    !is_square_attacked(b, G1, them);
                        if (rookOnH1 && pathEmpty && safe)
                            push(moves, E1, G1, KING_CASTLE);
                    }
                    if (b.castle.wq && kingOnE1)
                    {
                        bool rookOnA1 = (b.pieces[WHITE][ROOK] & (1ULL << A1)) != 0;
                        bool pathEmpty = (occAll & ((1ULL << D8) | (1ULL << C8))) == 0;
                        bool safe = !is_square_attacked(b, E1, them) &&
                                    !is_square_attacked(b, D1, them) &&
                                    !is_square_attacked(b, C1, them);
                        if (rookOnA1 && pathEmpty && safe)
                            push(moves, E1, C1, QUEEN_CASTLE);
                    }
                }
                else
                {
                    const bool kingOnE8 = (b.pieces[BLACK][KING] & (1ULL << E8)) != 0;
                    if (b.castle.bk && kingOnE8)
                    {
                        bool rookOnH8 = (b.pieces[BLACK][ROOK] & (1ULL << H8)) != 0;
                        bool pathEmpty = (occAll & ((1ULL << F8) | (1ULL << G8))) == 0;
                        bool safe = !is_square_attacked(b, E8, them) &&
                                    !is_square_attacked(b, F8, them) &&
                                    !is_square_attacked(b, G8, them);
                        if (rookOnH8 && pathEmpty && safe)
                            push(moves, E8, G8, KING_CASTLE);
                    }
                    if (b.castle.bq && kingOnE8)
                    {
                        bool rookOnA8 = (b.pieces[BLACK][ROOK] & (1ULL << A8)) != 0;
                        bool pathEmpty = (occAll & ((1ULL << D8) | (1ULL << C8) | (1ULL << B8))) == 0;
                        bool safe = !is_square_attacked(b, E8, them) &&
                                    !is_square_attacked(b, D8, them) &&
                                    !is_square_attacked(b, C8, them);
                        if (rookOnA8 && pathEmpty && safe)
                            push(moves, E8, C8, QUEEN_CASTLE);
                    }
                }
            }
        }

        return moves;
    }

    // uses pseudo-move generator to generate all possible moves.
    // moves are legal if they dont result in the player moving to be
    // placed in check.
    std::vector<Move> generate_legal_moves(Board &b)
    {
        std::vector<Move> legal;
        std::vector<Move> pseudo = generate_moves(b);

        const Colour us = b.side_to_move;
        const Colour them = (us == WHITE) ? BLACK : WHITE;

        for (Move m : pseudo)
        {
            Undo u;
            make_move(b, m, u);

            int ksq = king_sq(b, us);
            bool in_check = (ksq >= 0) && is_square_attacked(b, ksq, them);

            unmake_move(b, m, u);

            if (!in_check)
                legal.push_back(m);
        }

        return legal;
    }
}
