#include "perft.hh"
#include "movegen.hh"
// (When you implement make/unmake, include it and update this)

namespace engine
{

    static void do_make(Board &, Move) { /* TODO: implement */ }
    static void do_unmake(Board &, Move) { /* TODO: implement */ }

    std::uint64_t perft(Board &b, int depth)
    {
        if (depth == 0)
            return 1;
        auto moves = generate_moves(b); // pseudo-legal now; replace with legal later
        std::uint64_t nodes = 0;
        for (auto m : moves)
        {
            do_make(b, m);
            nodes += perft(b, depth - 1);
            do_unmake(b, m);
        }
        return nodes;
    }

} // namespace engine
