// universal chess interface
#include "board.hh"
#include "fen.hh"
#include "move.hh"
#include "perft.hh"
#include <iostream>
#include <sstream>

using namespace engine;

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

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
            std::string tok;
            ss >> tok; // position
            ss >> tok;
            if (tok == "startpos")
            {
                pos = Board::startpos();
            }
            else if (tok == "fen")
            {
                std::string fen;
                std::getline(ss, fen);
                // strip leading space
                if (!fen.empty() && fen[0] == ' ')
                    fen.erase(0, 1);
                pos = from_fen(fen);
            }
            continue;
        }

        if (line.rfind("go", 0) == 0)
        {
            // quick utility: "go perft depth N"
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

            else
            {
                // TODO: integrate search; for now, do nothing.
                std::cout << "bestmove 0000\n";
            }

            continue;
        }
    }
    return 0;
}
