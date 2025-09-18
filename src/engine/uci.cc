// --- src/engine/uci.cc ---
#include "../eval/eval.hh"
#include "board.hh"
#include "fen.hh"
#include "move.hh"
#include "move_do.hh"
#include "movegen.hh"
#include "perft.hh"
#include "search.hh"

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

using namespace engine;
static std::string eval_file_path;   // from UCI setoption
static std::string last_loaded_path; // actually loaded file
static bool eval_initialised = false;
static int move_overhead_ms = 80;

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
    switch (flag(m)) {
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

static bool apply_uci_move(Board& pos, const std::string& uciMove)
{
    Board tmp = pos;
    auto legal = generate_legal_moves(tmp);
    for (Move m : legal) {
        if (move_to_uci(m) == uciMove) {
            Undo u;
            make_move(pos, m, u);
            return true;
        }
    }
    return false;
}

static void initialise_eval()
{
    const char* p = eval_file_path.empty() ? nullptr : eval_file_path.c_str();

    // reload if never loaded, or requested path differs
    if (!eval_initialised || last_loaded_path != eval_file_path) {
        if (!eval::load_weights(p)) {
            std::cout << "info string eval: FAILED to load weights\n";
        } else {
            std::cout << "info string eval: weights loaded from "
                      << (eval_file_path.empty() ? "env CHESSTER_NET or ./CHESSTER_NET/raw.bin" : eval_file_path)
                      << "\n";
            last_loaded_path = eval_file_path;
            eval_initialised = true;
        }
    }
}

static void uci_print_id()
{
    std::cout << "id name Chesster\n";
    std::cout << "id author harrisontolley\n";
}

static void handle_uci_command()
{
    uci_print_id();
    std::cout << "option name EvalFile type string default (use setoption or CHESSTER_NET/raw.bin)\n";
    std::cout << "option name MoveOverhead type spin default 80 min 0 max 5000\n";
    std::cout << "uciok\n";
}

static void handle_is_ready()
{
    // Load eval lazily here (so setoption has happened)
    try {
        initialise_eval();
    } catch (const std::exception& e) {
        std::cout << "info string eval init error: " << e.what() << "\n";
    }
    std::cout << "readyok\n";
}

static void handle_setoption(const std::string& line)
{
    std::istringstream ss(line);
    std::string w, name, key, value;
    ss >> w;    // setoption
    ss >> w;    // name
    ss >> name; // EvalFile / MoveOverhead / ...
    if (name == "EvalFile") {
        ss >> w; // value
        std::getline(ss, value);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
        eval_file_path = value;
    } else if (name == "MoveOverhead") {
        ss >> w; // value
        int v = 0;
        ss >> v;
        if (v >= 0 && v <= 5000)
            move_overhead_ms = v;
    }
}

static void handle_position(const std::string& line, Board& pos)
{
    std::istringstream ss(line);
    std::string word;
    ss >> word; // "position"

    std::string sub;
    if (!(ss >> sub))
        return;

    if (sub == "startpos") {
        pos = Board::startpos();
    } else if (sub == "fen") {
        std::string f1, f2, f3, f4, f5, f6;
        ss >> f1 >> f2 >> f3 >> f4 >> f5 >> f6;
        pos = from_fen(f1 + " " + f2 + " " + f3 + " " + f4 + " " + f5 + " " + f6);
    }

    std::string w;
    if (ss >> w && w == "moves") {
        while (ss >> w) {
            if (!apply_uci_move(pos, w)) {
                std::cout << "info string warning: illegal/unknown move " << w << "\n";
                break;
            }
        }
    }
}

static void handle_eval(Board& pos)
{
    try {
        initialise_eval();
        std::cout << "info string eval cp: " << eval::evaluate(pos) << "\n";
    } catch (...) {
        std::cout << "info string eval failed\n";
    }
}

static void handle_evaldiag(Board& pos)
{
    try {
        initialise_eval();
        eval::debug_dump(pos);
    } catch (const std::exception& e) {
        std::cout << "info string evaldiag error: " << e.what() << "\n";
    }
}

static void handle_go(const std::string& line, const Board& pos)
{ // Supported: depth N | movetime X | wtime/btime[/winc/binc[/movestogo]]
    int depth = -1, movetime = -1;
    long wtime = -1, btime = -1, winc = 0, binc = 0;
    int movestogo = 0; // 0 means unknown

    {
        std::istringstream ss(line);
        std::string tok;
        ss >> tok; // "go"
        while (ss >> tok) {
            if (tok == "depth") {
                ss >> depth;
            } else if (tok == "movetime") {
                ss >> movetime;
            } else if (tok == "wtime") {
                ss >> wtime;
            } else if (tok == "btime") {
                ss >> btime;
            } else if (tok == "winc") {
                ss >> winc;
            } else if (tok == "binc") {
                ss >> binc;
            } else if (tok == "movestogo") {
                ss >> movestogo;
            }
            // ignore: nodes/infinite/ponder/searchmoves for now
        }
    }

    auto side = pos.side_to_move;
    long time_left = (side == WHITE ? wtime : btime);
    long inc = (side == WHITE ? winc : binc);

    auto clamp_ms = [](long x) { return x < 0 ? 0 : x; };

    long soft_ms = 0, hard_ms = 0;
    int maxDepth = (depth > 0 ? depth : 99);

    if (movetime >= 0) {
        // “move by” budget
        long t = clamp_ms(movetime - move_overhead_ms);
        soft_ms = t;
        hard_ms = std::max<long>(t - move_overhead_ms / 2, t); // same or a hair smaller
    } else if (time_left >= 0) {
        // Generic per-move split:
        //   base = time_left - overhead
        //   mtg  = advertised movestogo or assume ~40 moves remaining
        //   soft ~ base/(mtg+3) + 0.6*inc
        //   hard clamps to min(base*0.7, base - overhead)
        long base = clamp_ms(time_left - move_overhead_ms);
        int mtg = movestogo > 0 ? movestogo : 40;

        // soft: small slice + some of the increment
        long soft = (long)std::llround((double)base / (mtg + 6) + 0.60 * (double)inc);

        // cap soft to ≤ 25% of remaining bank, and set a small floor
        long soft_cap = base / 4; // 25%
        soft = std::max(5L, std::min(soft, soft_cap));

        // hard: just above soft, with an absolute 30% bank ceiling
        long hard = soft + std::max(5L, static_cast<long>(move_overhead_ms / 2));
        long hard_bank_cap = (long)(base * 0.30);
        hard = std::min(hard, hard_bank_cap);

        // never exceed base - overhead
        hard = std::min(hard, clamp_ms(base - move_overhead_ms));

        // sanity: soft <= hard
        soft = std::min(soft, std::max(soft, hard - 1));

        // ensure sanity
        soft_ms = soft;
        hard_ms = hard;
    } else if (depth > 0) {
        // pure fixed depth
        soft_ms = hard_ms = 0;
    } else {
        // fallback when nothing is given
        maxDepth = 10;
    }

    Board tmp = pos; // search on a copy
    Move bm;
    if (soft_ms > 0 || hard_ms > 0)
        bm = search_best_move_timed(tmp, maxDepth, (int)soft_ms, (int)hard_ms);
    else
        bm = search_best_move(tmp, maxDepth > 0 ? maxDepth : 10);

    std::string u = bm ? move_to_uci(bm) : "0000";
    std::cout << "bestmove " << u << "\n";
}

int main()
{
    // detaches cin/cout from C stdio. Speeds up IO a bit.
    std::ios::sync_with_stdio(false);

    // Flushes after every insertion.
    std::cout << std::unitbuf;

    // Ties cin to cout (before any cin read, cout is flushed).
    std::cin.tie(&std::cout);

    uci_print_id();
    Board pos = Board::startpos();

    // force eval to be initialised, as I never use other NNUE files.
    initialise_eval();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit")
            break;

        if (line == "uci") {
            handle_uci_command();
            continue;
        }

        if (line.rfind("setoption", 0) == 0) {
            handle_setoption(line);
            continue;
        }

        if (line.rfind("isready", 0) == 0) {
            handle_is_ready();
            continue;
        }

        if (line.rfind("ucinewgame", 0) == 0) {
            pos = Board::startpos();
            tt_clear();
            continue;
        }

        if (line.rfind("position", 0) == 0) {
            handle_position(line, pos);
            continue;
        }

        if (line.rfind("evaldiag", 0) == 0) {
            handle_evaldiag(pos);
            continue;
        }

        if (line.rfind("eval", 0) == 0) {
            handle_eval(pos);
        }

        if (line.rfind("go", 0) == 0) {
            handle_go(line, pos);
            continue;
        }
    }
    return 0;
}
