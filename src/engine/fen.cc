#include "fen.hh"
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace engine
{
    static inline int sq_index(int file, int rank)
    {
        return rank * 8 + file;
    }

    Board from_fen(const std::string &fen)
    {

        Board b{};
        std::istringstream ss(fen);

        // variables to define
        std::string board, stm, cast, ep;
        int half, full;

        if (!(ss >> board >> stm >> cast >> ep >> half >> full))
            throw std::runtime_error("Invalid FEN: " + fen);

        int r = 7, f = 0;
        for (char c : board)
        {

            //  end of row in board fen string
            if (c == '/')
            {
                --r;
                f = 0;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                // add 'c' empty squares to board
                f += c - '0';
                continue;
            }

            // uppercase is white, else black
            int colour = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
            char piece_symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            Piece p = NO_PIECE;
            switch (piece_symbol)
            {
            case 'p':
                p = PAWN;
                break;
            case 'r':
                p = ROOK;
                break;
            case 'n':
                p = KNIGHT;
                break;
            case 'b':
                p = BISHOP;
                break;
            case 'q':
                p = QUEEN;
                break;
            case 'k':
                p = KING;
                break;
            default:
                throw std::runtime_error("Invalid piece in FEN: " + std::string(1, c));
            }

            b.pieces[colour][p] |= (1ULL << sq_index(f, r)); // set bitboard to 1 at colour and piece
            ++f;
        }

        b.side_to_move = (stm == "w") ? WHITE : BLACK;
        b.castle = {};
        b.castle.wk = cast.find('K') != std::string::npos;
        b.castle.wq = cast.find('Q') != std::string::npos;
        b.castle.bk = cast.find('k') != std::string::npos;
        b.castle.bq = cast.find('q') != std::string::npos;

        if (ep != "-")
        {
            int file = ep[0] - 'a';
            int rank = ep[1] - '1';
            b.ep_square = sq_index(file, rank);
        }

        b.halfmove_clock = half;
        b.fullmove_number = full;
        return b;
    }

    std::string to_fen(const Board &b)
    {

        // Helper lambda to get the piece at a specific square
        auto piece_at = [&](int sq) -> char
        {
            for (int c = 0; c < 2; c++)
                for (int p = PAWN; p <= KING; ++p)
                {
                    if (b.pieces[c][p] & (1ULL << sq))
                    {
                        const char *sym = "pnbrqk";
                        char ch = sym[p];
                        return c == WHITE ? static_cast<char>(std::toupper(ch)) : ch;
                    }
                }
            return ' ';
        };

        std::ostringstream out;
        for (int r = 7; r >= 0; --r)
        {
            int empty = 0;
            for (int f = 0; f < 8; ++f)
            {
                char ch = piece_at(sq_index(f, r));
                if (ch == ' ')
                {
                    ++empty;
                }
                else
                {
                    if (empty)
                    {
                        out << empty;
                        empty = 0;
                    }
                    out << ch;
                }
            }
            if (empty)
                out << empty;
            if (r)
                out << '/';
        }

        out << ' ' << (b.side_to_move == WHITE ? 'w' : 'b') << ' ';
        std::string cr;

        if (b.castle.wk)
            cr.push_back('K');
        if (b.castle.wq)
            cr.push_back('Q');
        if (b.castle.bk)
            cr.push_back('k');
        if (b.castle.bq)
            cr.push_back('q');

        out << (cr.empty() ? "-" : cr) << ' ';
        if (b.ep_square)
        {
            int file = *b.ep_square % 8;
            int rank = *b.ep_square / 8;
            out << char('a' + file) << char('1' + rank);
        }
        else
            out << '-';
        out << ' ' << b.halfmove_clock << ' ' << b.fullmove_number;

        return out.str();
    }

}