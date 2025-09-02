#include "perft.hh"
#include "movegen.hh"
#include "move_do.hh"

namespace engine
{
    //  follows chess programming wiki for performance test (perft) function.
    // https://www.chessprogramming.org/Perft
    // possible furture speed up: hashing game states -> number of positions.
    // possible because can reach same game state from differing orders of moves.
    std::uint64_t perft(Board &b, int depth)
    {
        if (depth == 0)
            return 1;

        auto moves = generate_legal_moves(b);

        // can return early here as we know the number of legal moves
        // the number of legal moves with depth 1 is the number of states.
        if (depth == 1)
            return moves.size();

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

    // perft divide lists all moves and perft of the decremented depth.
    // returns a vector of pairs Moves/number of nodes.
    std::vector<std::pair<Move, std::uint64_t>> perft_divide(Board &b, int depth)
    {
        std::vector<std::pair<Move, std::uint64_t>> out;
        auto moves = generate_legal_moves(b);

        for (auto m : moves)
        {
            Undo u;
            make_move(b, m, u);
            std::uint64_t n = perft(b, depth - 1);
            unmake_move(b, m, u);
            out.emplace_back(m, n);
        }

        return out;
    }
}
