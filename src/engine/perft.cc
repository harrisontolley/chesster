#include "perft.hh"
#include "movegen.hh"
#include "move_do.hh"

namespace engine
{
    std::uint64_t perft(Board &b, int depth)
    {
        if (depth == 0)
            return 1;

        auto moves = generate_legal_moves(b);
        std::uint64_t nodes = 0;

        for (auto m : moves)
        {
            Undo u;
            make_move(b, m, u);
            nodes += perft(b, depth - 1);
            unmake_move(b, m, u);
        }
        return nodes;
    }
}
