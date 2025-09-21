#include "see.hh"

#include "attack_tables.hh"
#include "bitboard.hh"
#include "util.hh"

#include <algorithm>
#include <cstdint>

namespace engine {

static inline Bitboard bb_from(int sq)
{
    return 1ULL << sq;
}

// Local position snapshot helpers
struct Snap {
    Bitboard pcs[2][6];
};

static inline void snap_from_board(const Board& b, Snap& s)
{
    for (int c = 0; c < 2; ++c)
        for (int p = 0; p < 6; ++p)
            s.pcs[c][p] = b.pieces[c][p];
}

static inline Bitboard occ_all(const Snap& s)
{
    Bitboard o = 0;
    for (int c = 0; c < 2; ++c)
        for (int p = 0; p < 6; ++p)
            o |= s.pcs[c][p];
    return o;
}

static inline Piece piece_on_snap(const Snap& s, Colour c, int sq)
{
    Bitboard m = bb_from(sq);
    for (int p = PAWN; p <= KING; ++p)
        if (s.pcs[c][p] & m)
            return (Piece)p;
    return NO_PIECE;
}

static inline void remove_piece(Snap& s, Colour c, Piece p, int sq)
{
    s.pcs[c][p] &= ~bb_from(sq);
}

static inline void add_piece(Snap& s, Colour c, Piece p, int sq)
{
    s.pcs[c][p] |= bb_from(sq);
}

// First-blocker-from-target ray test. If the first blocker on step
// is a (c, sliderOK) it counts as an attacker.
template <Bitboard (*STEP)(Bitboard)> static inline Bitboard first_blocker_mask(Bitboard occ, int targetSq)
{
    Bitboard r = bb_from(targetSq);
    while (true) {
        r = STEP(r);
        if (!r)
            break;
        if (r & occ)
            return r;
    }
    return 0ULL;
}

static Bitboard attackers_to_sq(const Snap& s, Bitboard occ, int sq, Colour side)
{
    Bitboard target = bb_from(sq);
    Bitboard att = 0ULL;

    // pawns
    if (side == WHITE) {
        att |= (se(target) | sw(target)) & s.pcs[WHITE][PAWN];
    } else {
        att |= (ne(target) | nw(target)) & s.pcs[BLACK][PAWN];
    }

    // knights
    att |= KNIGHT_ATTACKS[sq] & s.pcs[side][KNIGHT];

    // king
    {
        Bitboard ring = north(target) | south(target) | east(target) | west(target) | ne(target) | nw(target) |
                        se(target) | sw(target);
        att |= ring & s.pcs[side][KING];
    }

    // Sliders via first-blocker
    // diagonal queen/bishop
    {
        Bitboard b;

        b = first_blocker_mask<north>(occ, sq);
        if (b && ((b & (s.pcs[side][ROOK] | s.pcs[side][QUEEN]))))
            att |= b;
        b = first_blocker_mask<south>(occ, sq);
        if (b && ((b & (s.pcs[side][ROOK] | s.pcs[side][QUEEN]))))
            att |= b;
        b = first_blocker_mask<east>(occ, sq);
        if (b && ((b & (s.pcs[side][ROOK] | s.pcs[side][QUEEN]))))
            att |= b;
        b = first_blocker_mask<west>(occ, sq);
        if (b && ((b & (s.pcs[side][ROOK] | s.pcs[side][QUEEN]))))
            att |= b;
    }

    // Diagonals: bishop/queen
    {
        Bitboard b;

        b = first_blocker_mask<ne>(occ, sq);
        if (b && ((b & (s.pcs[side][BISHOP] | s.pcs[side][QUEEN]))))
            att |= b;
        b = first_blocker_mask<nw>(occ, sq);
        if (b && ((b & (s.pcs[side][BISHOP] | s.pcs[side][QUEEN]))))
            att |= b;
        b = first_blocker_mask<se>(occ, sq);
        if (b && ((b & (s.pcs[side][BISHOP] | s.pcs[side][QUEEN]))))
            att |= b;
        b = first_blocker_mask<sw>(occ, sq);
        if (b && ((b & (s.pcs[side][BISHOP] | s.pcs[side][QUEEN]))))
            att |= b;
    }

    return att;
}

// Pick least-valuable attacker square + piece type from mask.
static inline bool pick_lva(const Snap& s, Colour side, Bitboard attMask, int& outFrom, Piece& outP)
{
    Bitboard bb;

    bb = attMask & s.pcs[side][PAWN];
    if (bb) {
        outFrom = __builtin_ctzll(bb);
        outP = PAWN;
        return true;
    }

    bb = attMask & s.pcs[side][KNIGHT];
    if (bb) {
        outFrom = __builtin_ctzll(bb);
        outP = KNIGHT;
        return true;
    }

    bb = attMask & s.pcs[side][BISHOP];
    if (bb) {
        outFrom = __builtin_ctzll(bb);
        outP = BISHOP;
        return true;
    }

    bb = attMask & s.pcs[side][ROOK];
    if (bb) {
        outFrom = __builtin_ctzll(bb);
        outP = ROOK;
        return true;
    }

    bb = attMask & s.pcs[side][QUEEN];
    if (bb) {
        outFrom = __builtin_ctzll(bb);
        outP = QUEEN;
        return true;
    }

    bb = attMask & s.pcs[side][KING];
    if (bb) {
        outFrom = __builtin_ctzll(bb);
        outP = KING;
        return true;
    }

    return false;
}

int see(const Board& b, Move m)
{
    const Colour us = b.side_to_move;
    const Colour them = (us == WHITE ? BLACK : WHITE);

    const int from = from_sq(m);
    const int to = to_sq(m);
    const int fl = flag(m);

    // Identify captured piece
    int capSq = to;
    Piece cap = NO_PIECE;
    if (fl == EN_PASSANT) {
        cap = PAWN;
        capSq = (us == WHITE) ? (to - 8) : (to + 8);
    } else {
        cap = piece_on(b, them, to);
    }

    if (cap == NO_PIECE && fl < PROMO_N) // nonpromo noncapture - nothing to evaluate
        return 0;

    Snap s;
    snap_from_board(b, s);
    Bitboard occ = occ_all(s);

    int gains[32];
    int d = 0;

    // First force the given move (se SEE is about THIS capture, not any other capture)
    // Determine our moving piece and the piece that will sit on 'to' after the move

    Piece mover = piece_on_snap(s, us, from);
    if (mover == NO_PIECE) // should never happen
        return 0;

    Piece placed = mover;
    int promo_bonus = 0;
    if (fl == PROMO_N || fl == PROMO_B || fl == PROMO_R || fl == PROMO_Q || fl == PROMO_N_CAPTURE ||
        fl == PROMO_B_CAPTURE || fl == PROMO_R_CAPTURE || fl == PROMO_Q_CAPTURE) {

        switch (fl) {
        case PROMO_N:
        case PROMO_N_CAPTURE:
            placed = KNIGHT;
            break;
        case PROMO_B:
        case PROMO_B_CAPTURE:
            placed = BISHOP;
            break;
        case PROMO_R:
        case PROMO_R_CAPTURE:
            placed = ROOK;
            break;
        default:
            placed = QUEEN;
            break;
        }
        promo_bonus = val_cp(placed) - val_cp(PAWN);
    }

    // initial gain is captured piece value + promo bonus (if any)
    gains[d++] = val_cp(cap) + promo_bonus;

    // apply our capture to the snapshot (virtual make)
    if (cap != NO_PIECE)
        remove_piece(s, them, cap, capSq);
    remove_piece(s, us, mover, from);
    add_piece(s, us, placed, to);

    occ &= ~bb_from(from);
    if (cap != NO_PIECE)
        occ &= ~bb_from(capSq);
    occ |= bb_from(to);

    // Square 'to' is occupied by (us, placed). Alternate recaptures.
    Colour side = them;
    Piece victim = placed;

    while (true) {
        Bitboard att = attackers_to_sq(s, occ, to, side);
        if (!att)
            break;

        // pick least valuable attacker
        int aFrom;
        Piece aPc;

        if (!pick_lva(s, side, att, aFrom, aPc))
            break;

        // add swap gain term for this ply
        gains[d] = val_cp(victim) - gains[d - 1];
        ++d;

        // virtually execute recapture
        remove_piece(s, side, aPc, aFrom);

        // Remove previous victim at 'to' from the other side set
        remove_piece(s, (side == WHITE) ? BLACK : WHITE, victim, to);
        add_piece(s, side, aPc, to);

        // update occupancy
        occ &= ~bb_from(aFrom);

        // next ply
        side = (side == WHITE) ? BLACK : WHITE;
        victim = aPc;

        if (d >= 31)
            break; // prevent overflow
    }

    while (--d) {
        gains[d - 1] = -std::max(-gains[d - 1], gains[d]);
    }
    return gains[0];
}

} // namespace engine