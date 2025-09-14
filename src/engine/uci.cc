// --- src/engine/uci.cc ---
#include "../eval/eval.hh"
#include "board.hh"
#include "fen.hh"
#include "move.hh"
#include "move_do.hh"
#include "movegen.hh"
#include "perft.hh"
#include "search.cc"

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

static std::string eval_file_path;   // from UCI setoption
static std::string last_loaded_path; // actually loaded file
static bool eval_initialised = false;

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

int main()
{
    std::ios::sync_with_stdio(false);
    std::cout << std::unitbuf;
    std::cin.tie(&std::cout);

    uci_print_id();
    // Advertise UCI options
    std::cout << "option name EvalFile type string default (use setoption or CHESSTER_NET/raw.bin)\n";
    std::cout << "uciok\n";

    Board pos = Board::startpos();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit")
            break;

        if (line == "uci") {
            uci_print_id();
            std::cout << "option name EvalFile type string default (use setoption or CHESSTER_NET/raw.bin)\n";
            std::cout << "uciok\n";
            continue;
        }

        if (line.rfind("setoption", 0) == 0) {
            // setoption name EvalFile value /path/to/checkpoint_or_raw.bin
            std::istringstream ss(line);
            std::string w, name, key, value;
            ss >> w;    // setoption
            ss >> w;    // name
            ss >> name; // EvalFile or others
            if (name == "EvalFile") {
                // consume "value" and the rest as path (can contain spaces)
                ss >> w; // value
                std::getline(ss, value);
                // trim leading spaces
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                    value.erase(value.begin());
                eval_file_path = value;
                // force re-load next isready
                // (simplest: clear the flag by re-calling load on next isready)
            }
            continue;
        }

        if (line.rfind("isready", 0) == 0) {
            // Load eval lazily here (so setoption has happened)
            try {
                initialise_eval();
            } catch (const std::exception& e) {
                std::cout << "info string eval init error: " << e.what() << "\n";
            }
            std::cout << "readyok\n";
            continue;
        }

        if (line.rfind("ucinewgame", 0) == 0) {
            pos = Board::startpos();
            continue;
        }

        if (line.rfind("position", 0) == 0) {
            std::istringstream ss(line);
            std::string word;
            ss >> word; // "position"

            std::string sub;
            if (!(ss >> sub))
                continue;

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
            continue;
        }

        if (line.rfind("evaldiag", 0) == 0) {
            try {
                initialise_eval();
                eval::debug_dump(pos);
            } catch (const std::exception& e) {
                std::cout << "info string evaldiag error: " << e.what() << "\n";
            }
            continue;
        }

        if (line.rfind("go", 0) == 0) {
            // Modes:
            //   go perft depth N
            //   go divide depth N
            //   go depth N
            if (line.find("perft") != std::string::npos) {
                int depth = 1;
                if (auto dpos = line.find("depth"); dpos != std::string::npos) {
                    std::istringstream dd(line.substr(dpos + 5));
                    dd >> depth;
                }
                Board tmp = pos;
                auto nodes = perft(tmp, depth);
                std::cout << "info string perft " << depth << " nodes " << nodes << "\n";
                std::cout << "bestmove 0000\n";
            } else if (line.find("divide") != std::string::npos) {
                int depth = 1;
                if (auto dpos = line.find("depth"); dpos != std::string::npos) {
                    std::istringstream dd(line.substr(dpos + 5));
                    dd >> depth;
                }
                Board tmp = pos;
                auto parts = perft_divide(tmp, depth);
                std::uint64_t total = 0;
                for (auto& kv : parts) {
                    std::cout << move_to_uci(kv.first) << ": " << kv.second << "\n";
                    total += kv.second;
                }
                std::cout << "Total: " << total << "\n";
                std::cout << "bestmove 0000\n";
            } else {
                int depth = 5; // default if not specified
                if (auto dpos = line.find("depth"); dpos != std::string::npos) {
                    std::istringstream dd(line.substr(dpos + 5));
                    dd >> depth;
                }
                Board tmp = pos; // search works on a copy
                Move bm = search_best_move(tmp, depth);
                std::string u = bm ? move_to_uci(bm) : "0000";
                std::cout << "bestmove " << u << "\n";
            }
            continue;
        }

        if (line.rfind("eval", 0) == 0) {
            try {
                initialise_eval();
                std::cout << "info string eval cp: " << eval::evaluate(pos) << "\n";
            } catch (...) {
                std::cout << "info string eval failed\n";
            }
        }
    }
    return 0;
}
