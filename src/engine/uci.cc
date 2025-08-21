// universal chess interface
#include "board.hh"
#include "move.hh"
#include <iostream>

int main()
{
    std::cout << "id name Chesster\n";
    std::cout << "uciok\n";
    std::string cmd;

    while (std::getline(std::cin, cmd))
    {
        if (cmd == "quit")
        {
            break;
        }

        if (cmd.rfind("isready", 0) == 0)
        {
            std::cout << "readyok\n";
        }
    }

    return 0;
}