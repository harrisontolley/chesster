#include "board.hh"
#include "../eval/eval.hh"
#include "move.hh"

namespace engine
{

    int negamax(Board &board, int depth)
    {
        if (depth == 0)
            return eval::evaluate(board); // temporary 0
        return 0;
    }
}