#include "move_do.hh"
#include "bitboard.hh"
#include "attack_tables.hh"
#include <cassert>

namespace engine
{
    // bitboard helpers
    static inline void bb_set(Bitboard &bb, int sq) { bb |= (1ULL << sq); }
    static inline void bb_clear(Bitboard &bb, int sq) { bb &= ~(1ULL << sq); }

    static inline Piece piece_at(const Board &b, Colour c, int sq)
    {
        Bitboard mask = 1ULL << sq;
        for (int p = PAWN; p <= KING; ++p)
            if (b.pieces[c][p] & mask)
                return static_cast<Piece>(p);
        return NO_PIECE;
    }

    bool is_square_attacked(const Board &b, int sq, Colour by)
    {
        const Bitboard occAll = occupancy(b);
        const Bitboard target = 1ULL << sq;

        // pawns: shift the target TOWARD the side that could capture into it
        if (by == WHITE)
        {
            // A white pawn attacks from one rank below target (south) diagonally
            if ((se(target) & b.pieces[WHITE][PAWN]) || (sw(target) & b.pieces[WHITE][PAWN]))
                return true;
        }
        else
        {
            // A black pawn attacks from one rank above target (north) diagonally
            if ((ne(target) & b.pieces[BLACK][PAWN]) || (nw(target) & b.pieces[BLACK][PAWN]))
                return true;
        }

        // knights
        if (KNIGHT_ATTACKS[sq] & b.pieces[by][KNIGHT])
            return true;

        // kings (adjacent)
        {
            Bitboard k = target;
            Bitboard kingAtt = (north(k) | south(k) | east(k) | west(k) |
                                ne(k) | nw(k) | se(k) | sw(k));
            if (kingAtt & b.pieces[by][KING])
                return true;
        }

        // sliders unchanged ...
        auto ray_hit = [&](Bitboard (*step)(Bitboard), Bitboard sliders)
        {
            Bitboard r = target;
            while (true)
            {
                r = step(r);
                if (!r)
                    break;
                if (r & occAll)
                {
                    if (r & sliders)
                        return true;
                    break;
                }
            }
            return false;
        };

        const Bitboard rq = b.pieces[by][ROOK] | b.pieces[by][QUEEN];
        const Bitboard bq = b.pieces[by][BISHOP] | b.pieces[by][QUEEN];

        if (ray_hit(&north, rq) || ray_hit(&south, rq) ||
            ray_hit(&east, rq) || ray_hit(&west, rq))
            return true;

        if (ray_hit(&ne, bq) || ray_hit(&nw, bq) ||
            ray_hit(&se, bq) || ray_hit(&sw, bq))
            return true;

        return false;
    }

    static inline void clear_castle_if_rook_moves(Board &b, Colour us, int fromSq)
    {
        if (us == WHITE)
        {
            if (fromSq == H1)
                b.castle.wk = false;
            if (fromSq == A1)
                b.castle.wq = false;
        }
        else
        {
            if (fromSq == H8)
                b.castle.bk = false;
            if (fromSq == A8)
                b.castle.bq = false;
        }
    }

    static inline void clear_castle_if_rook_captured(Board &b, Colour them, int atSq)
    {
        if (them == WHITE)
        {
            if (atSq == H1)
                b.castle.wk = false;
            if (atSq == A1)
                b.castle.wq = false;
        }
        else
        {
            if (atSq == H8)
                b.castle.bk = false;
            if (atSq == A8)
                b.castle.bq = false;
        }
    }

    // ----------------- Make / Unmake -----------------
    void make_move(Board &b, Move m, Undo &u)
    {
        const Colour us = b.side_to_move;
        const Colour them = (us == WHITE) ? BLACK : WHITE;
        const int from = from_sq(m);
        const int to = to_sq(m);
        const int fl = flag(m);

        // Save state
        u.ep_prev = b.ep_square;
        u.castle_prev = b.castle;
        u.halfmove_prev = b.halfmove_clock;
        u.fullmove_prev = b.fullmove_number;
        u.captured_piece = NO_PIECE;
        u.moved_piece = piece_at(b, us, from);
        assert(u.moved_piece != NO_PIECE && "No piece on from-square");

        // defaults
        b.ep_square.reset();

        auto remove_piece = [&](Colour c, Piece p, int sq)
        { bb_clear(b.pieces[c][p], sq); };
        auto add_piece = [&](Colour c, Piece p, int sq)
        { bb_set(b.pieces[c][p], sq); };

        // Handle captures first (incl. promo captures & EP)
        bool any_capture = false;
        if (fl == CAPTURE || fl == PROMO_N_CAPTURE || fl == PROMO_B_CAPTURE ||
            fl == PROMO_R_CAPTURE || fl == PROMO_Q_CAPTURE)
        {
            u.captured_piece = piece_at(b, them, to);
            assert(u.captured_piece != NO_PIECE && "Capture flag but no piece on target");
            remove_piece(them, u.captured_piece, to);
            any_capture = true;
        }
        else if (fl == EN_PASSANT)
        {
            int capSq = (us == WHITE) ? (to - 8) : (to + 8);
            u.captured_piece = PAWN;
            remove_piece(them, PAWN, capSq);
            any_capture = true;
        }

        // Move our piece off 'from'
        remove_piece(us, u.moved_piece, from);

        // Promotions map
        auto flag_to_promo_piece = [&](int f) -> Piece
        {
            switch (f)
            {
            case PROMO_N:
            case PROMO_N_CAPTURE:
                return KNIGHT;
            case PROMO_B:
            case PROMO_B_CAPTURE:
                return BISHOP;
            case PROMO_R:
            case PROMO_R_CAPTURE:
                return ROOK;
            case PROMO_Q:
            case PROMO_Q_CAPTURE:
                return QUEEN;
            default:
                return NO_PIECE;
            }
        };

        // Place to-square based on move type
        if (fl == KING_CASTLE || fl == QUEEN_CASTLE)
        {
            add_piece(us, KING, to);
            if (us == WHITE)
            {
                if (fl == KING_CASTLE)
                {
                    remove_piece(WHITE, ROOK, H1);
                    add_piece(WHITE, ROOK, F1);
                }
                else
                {
                    remove_piece(WHITE, ROOK, A1);
                    add_piece(WHITE, ROOK, D1);
                }
                b.castle.wk = b.castle.wq = false;
            }
            else
            {
                if (fl == KING_CASTLE)
                {
                    remove_piece(BLACK, ROOK, H8);
                    add_piece(BLACK, ROOK, F8);
                }
                else
                {
                    remove_piece(BLACK, ROOK, A8);
                    add_piece(BLACK, ROOK, D8);
                }
                b.castle.bk = b.castle.bq = false;
            }
        }
        else if (fl == PROMO_N || fl == PROMO_B || fl == PROMO_R || fl == PROMO_Q ||
                 fl == PROMO_N_CAPTURE || fl == PROMO_B_CAPTURE || fl == PROMO_R_CAPTURE || fl == PROMO_Q_CAPTURE)
        {
            assert(u.moved_piece == PAWN);
            add_piece(us, flag_to_promo_piece(fl), to);
        }
        else
        {
            add_piece(us, u.moved_piece, to);
            if (fl == DOUBLE_PUSH)
            {
                // EP square is the jumped-over square
                b.ep_square = (us == WHITE) ? std::optional<int>(from + 8) : std::optional<int>(from - 8);
            }
        }

        // Update castling rights (king/rook move or rook captured)
        if (u.moved_piece == KING)
        {
            if (us == WHITE)
            {
                b.castle.wk = b.castle.wq = false;
            }
            else
            {
                b.castle.bk = b.castle.bq = false;
            }
        }
        else if (u.moved_piece == ROOK)
        {
            clear_castle_if_rook_moves(b, us, from);
        }
        if (any_capture)
        {
            clear_castle_if_rook_captured(b, them, to);
            if (fl == EN_PASSANT)
            {
                int capSq = (us == WHITE) ? (to - 8) : (to + 8);
                clear_castle_if_rook_captured(b, them, capSq);
            }
        }

        // 50-move clock
        if (u.moved_piece == PAWN || any_capture)
            b.halfmove_clock = 0;
        else
            b.halfmove_clock += 1;

        // move number and side to move
        if (us == BLACK)
            b.fullmove_number += 1;
        b.side_to_move = them;
    }

    void unmake_move(Board &b, Move m, Undo &u)
    {
        const Colour them = b.side_to_move;                // side who is to move now
        const Colour us = (them == WHITE) ? BLACK : WHITE; // side who just moved
        const int from = from_sq(m);
        const int to = to_sq(m);
        const int fl = flag(m);

        auto remove_piece = [&](Colour c, Piece p, int sq)
        { bb_clear(b.pieces[c][p], sq); };
        auto add_piece = [&](Colour c, Piece p, int sq)
        { bb_set(b.pieces[c][p], sq); };

        // Restore side and counters/rights/ep
        b.side_to_move = us;
        b.ep_square = u.ep_prev;
        b.castle = u.castle_prev;
        b.halfmove_clock = u.halfmove_prev;
        b.fullmove_number = u.fullmove_prev;

        // Undo placement
        if (fl == KING_CASTLE || fl == QUEEN_CASTLE)
        {
            remove_piece(us, KING, to);
            add_piece(us, KING, from);

            if (us == WHITE)
            {
                if (fl == KING_CASTLE)
                {
                    remove_piece(WHITE, ROOK, F1);
                    add_piece(WHITE, ROOK, H1);
                }
                else
                {
                    remove_piece(WHITE, ROOK, D1);
                    add_piece(WHITE, ROOK, A1);
                }
            }
            else
            {
                if (fl == KING_CASTLE)
                {
                    remove_piece(BLACK, ROOK, F8);
                    add_piece(BLACK, ROOK, H8);
                }
                else
                {
                    remove_piece(BLACK, ROOK, D8);
                    add_piece(BLACK, ROOK, A8);
                }
            }
        }
        else if (fl == PROMO_N || fl == PROMO_B || fl == PROMO_R || fl == PROMO_Q ||
                 fl == PROMO_N_CAPTURE || fl == PROMO_B_CAPTURE || fl == PROMO_R_CAPTURE || fl == PROMO_Q_CAPTURE)
        {
            // remove promoted piece from 'to', restore pawn on 'from'
            Piece pp;
            switch (fl)
            {
            case PROMO_N:
            case PROMO_N_CAPTURE:
                pp = KNIGHT;
                break;
            case PROMO_B:
            case PROMO_B_CAPTURE:
                pp = BISHOP;
                break;
            case PROMO_R:
            case PROMO_R_CAPTURE:
                pp = ROOK;
                break;
            default:
                pp = QUEEN;
                break;
            }
            remove_piece(us, pp, to);
            add_piece(us, PAWN, from);
        }
        else
        {
            remove_piece(us, u.moved_piece, to);
            add_piece(us, u.moved_piece, from);
        }

        // Restore captured piece (if any)
        if (u.captured_piece != NO_PIECE)
        {
            if (fl == EN_PASSANT)
            {
                int capSq = (us == WHITE) ? (to - 8) : (to + 8);
                add_piece((us == WHITE) ? BLACK : WHITE, PAWN, capSq);
            }
            else
            {
                add_piece((us == WHITE) ? BLACK : WHITE, u.captured_piece, to);
            }
        }
    }
}
