#include "board.hh"
#include "fen.hh"
#include "move.hh"
#include "movegen.hh"
#include "move_do.hh"
#include "perft.hh"
#include <iostream>
#include <sstream>
#include <string>

using namespace engine;

static std::string sq_to_str(int sq)
{
    if (sq < 0 || sq > 63)
        return "??";
    char f = char('a' + (sq & 7));
    char r = char('1' + (sq >> 3));
    return std::string() + f + r;
}

static std::string move_to_uci(Move m)
{
    std::string s = sq_to_str(from_sq(m)) + sq_to_str(to_sq(m));
    switch (flag(m))
    {
    case PROMO_Q:
    case PROMO_Q_CAPTURE:
        s += 'q';
        break;
    case PROMO_R:
    case PROMO_R_CAPTURE:
        s += 'r';
        break;
    case PROMO_B:
    case PROMO_B_CAPTURE:
        s += 'b';
        break;
    case PROMO_N:
    case PROMO_N_CAPTURE:
        s += 'n';
        break;
    default:
        break;
    }
    return s;
}

// find & apply a UCI move in the current position
static bool apply_uci_move(Board &pos, const std::string &uciMove)
{
    Board tmp = pos; // legal gen mutates, so use a copy
    auto legal = generate_legal_moves(tmp);
    for (Move m : legal)
    {
        if (move_to_uci(m) == uciMove)
        {
            Undo u;
            make_move(pos, m, u); // apply permanently
            return true;
        }
    }
    return false;
}

int main()
{
    std::ios::sync_with_stdio(false);
    std::cout << std::unitbuf;
    std::cin.tie(&std::cout);

    std::cout << "id name Chesster\n";
    std::cout << "id author harrisontolley\n";
    std::cout << "uciok\n";

    Board pos = Board::startpos();
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line == "quit")
            break;

        if (line == "uci")
        {
            std::cout << "id name Chesster\n";
            std::cout << "id author harrisontolley\n";
            std::cout << "uciok\n";
            continue;
        }

        if (line.rfind("isready", 0) == 0)
        {
            std::cout << "readyok\n";
            continue;
        }
        if (line.rfind("ucinewgame", 0) == 0)
        {
            pos = Board::startpos();
            continue;
        }

        if (line.rfind("position", 0) == 0)
        {
            std::istringstream ss(line);
            std::string word;
            ss >> word; // "position"

            std::string sub;
            if (!(ss >> sub))
                continue; // expect "startpos" or "fen"

            if (sub == "startpos")
            {
                pos = Board::startpos();
            }
            else if (sub == "fen")
            {
                // read exactly 6 FEN tokens
                std::string f1, f2, f3, f4, f5, f6;
                ss >> f1 >> f2 >> f3 >> f4 >> f5 >> f6;
                pos = from_fen(f1 + " " + f2 + " " + f3 + " " + f4 + " " + f5 + " " + f6);
            }

            // optional trailing moves
            std::string w;
            if (ss >> w && w == "moves")
            {
                while (ss >> w)
                {
                    if (!apply_uci_move(pos, w))
                    {
                        std::cout << "info string warning: illegal/unknown move " << w << "\n";
                        break;
                    }
                }
            }
            continue;
        }

        if (line.rfind("go", 0) == 0)
        {
            if (line.find("perft") != std::string::npos)
            {
                int depth = 1;
                auto dpos = line.find("depth");
                if (dpos != std::string::npos)
                {
                    std::istringstream dd(line.substr(dpos + 5));
                    dd >> depth;
                }
                Board tmp = pos;
                auto nodes = perft(tmp, depth);
                std::cout << "info string perft " << depth << " nodes " << nodes << "\n";
                std::cout << "bestmove 0000\n";
            }
            else if (line.find("divide") != std::string::npos)
            {
                int depth = 1;
                auto dpos = line.find("depth");
                if (dpos != std::string::npos)
                {
                    std::istringstream dd(line.substr(dpos + 5));
                    dd >> depth;
                }
                Board tmp = pos;
                auto parts = perft_divide(tmp, depth);
                std::uint64_t total = 0;
                for (auto &kv : parts)
                {
                    std::cout << move_to_uci(kv.first) << ": " << kv.second << "\n";
                    total += kv.second;
                }
                std::cout << "Total: " << total << "\n";
                std::cout << "bestmove 0000\n";
            }
            else
            {
                std::cout << "bestmove 0000\n";
            }
            continue;
        }
    }
    return 0;
}
