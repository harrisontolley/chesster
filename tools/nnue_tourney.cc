// OLD CODE THAT WAS USED FOR TOURAMENTS OF NNUE WEIGHTS
// NOW USE CUTECHESS CLI DIRECTLY AGAINST STOCKFISH AT SPECIFIC ELOS

#include "engine/board.hh"
#include "engine/fen.hh"
#include "engine/move.hh"
#include "engine/move_do.hh"
#include "engine/movegen.hh"
#include "engine/search.hh"
#include "engine/util.hh"
#include "eval/eval.hh"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace engine;

struct Options {
    std::string nets_dir;
    int games_per_pair = 2; // total games per pair (will be split equally per color)
    int movetime_ms = 200;  // per-move time budget (if >0, used)
    int depth = 0;          // fixed depth (if >0, used when movetime_ms <= 0)
    int max_plies = 300;    // ply cap -> draw
    std::string csv_out;    // optional CSV path
};

struct NetEntry {
    std::string name; // pretty name for tables (e.g., checkpoint folder name)
    std::string path; // path to quantised.bin/raw.bin (or a single *.bin file)
};

enum class Result : int { WhiteWin, BlackWin, Draw };

struct ScoreRow {
    int id = -1;
    std::string name;
    int games = 0;
    int wins = 0;
    int losses = 0;
    int draws = 0;
    double points() const
    {
        return wins + 0.5 * draws;
    }
};

static void print_usage(const char* argv0)
{
    std::cerr << "Usage: " << argv0
              << " --nets <dir-or-file> [--games N] [--movetime ms | --depth D] [--plies N] [--csv out.csv]\n";
}

static std::vector<NetEntry> discover_nets(const std::string& root)
{
    std::vector<NetEntry> nets;

    auto push_net = [&](const fs::path& file, const std::string& display) {
        nets.push_back(NetEntry{display, file.string()});
    };

    fs::path p(root);
    if (!fs::exists(p))
        return nets;

    // Single file case
    if (fs::is_regular_file(p)) {
        std::string name = p.stem().string();
        if (name == "quantised" || name == "raw")
            name = p.parent_path().filename().string();
        push_net(p, name);
        return nets;
    }

    // include any subdir containing raw.bin/quantised.bin; also allow *.bin files directly in root
    for (auto& entry : fs::directory_iterator(p)) {
        if (entry.is_directory()) {
            fs::path r = entry.path() / "raw.bin";
            fs::path q = entry.path() / "quantised.bin";

            // TODO CHANGE TO PREFER QUANTISED WHEN WEIGHT INGESTER BUGS ARE FIXED
            if (fs::exists(r))
                push_net(r, entry.path().filename().string()); // prefer raw.bin
            else if (fs::exists(q))
                push_net(q, entry.path().filename().string()); // fallback to quantised.bin

        } else if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".bin") {
                std::string leaf = entry.path().filename().string();
                if (leaf == "weights.bin")
                    continue; // optimiser_state/weights.bin (not the net)
                std::string nm = entry.path().stem().string();
                push_net(entry.path(), nm);
            }
        }
    }

    return nets;
}

static Result
play_game(const std::string& white_net, const std::string& black_net, int movetime_ms, int depth, int ply_cap)
{
    Board pos = Board::startpos();

    auto search_one = [&](const std::string& net_path) -> Move {
        if (!eval::load_weights(net_path.c_str())) {
            std::cerr << "[ERROR] Failed to load net: " << net_path << "\n";
            return Move{}; // null move -> forfeit
        }

        tt_clear();

        Board tmp = pos; // search works on a copy

        if (movetime_ms > 0) {
            // soft==hard for simplicity
            return search_best_move_timed(tmp, /*maxDepth*/ 99, movetime_ms, movetime_ms);
        } else {
            int d = depth > 0 ? depth : 10;
            return search_best_move(tmp, d);
        }
    };

    for (int ply = 0; ply < ply_cap; ++ply) {
        const bool stm_white = (pos.side_to_move == WHITE);
        const std::string& np = stm_white ? white_net : black_net;
        Move m = search_one(np);

        if (!m) {
            if (is_checkmate(pos)) {
                return stm_white ? Result::BlackWin : Result::WhiteWin;
            } else if (is_stalemate(pos)) {
                return Result::Draw;
            }
        }

        // Apply
        Undo u;
        make_move(pos, m, u);

        if (is_checkmate(pos)) {
            return stm_white ? Result::WhiteWin : Result::BlackWin;
        } else if (is_stalemate(pos)) {
            return Result::Draw;
        }
    }

    // Ply cap hit => draw
    return Result::Draw;
}

int main(int argc, char** argv)
{
    Options opt;

    // --- parse CLI ---
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        auto need = [&](int i) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                std::exit(2);
            }
            return std::string(argv[i + 1]);
        };

        if (a == "--nets")
            opt.nets_dir = need(i++);
        else if (a == "--games")
            opt.games_per_pair = std::stoi(need(i++));
        else if (a == "--movetime")
            opt.movetime_ms = std::stoi(need(i++));
        else if (a == "--depth")
            opt.depth = std::stoi(need(i++));
        else if (a == "--plies")
            opt.max_plies = std::stoi(need(i++));
        else if (a == "--csv")
            opt.csv_out = need(i++);
        else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (opt.nets_dir.empty()) {
        print_usage(argv[0]);
        return 2;
    }
    if (opt.movetime_ms <= 0 && opt.depth <= 0) {
        std::cerr << "[INFO] Neither --movetime nor --depth set. Defaulting to --movetime 200 ms.\n";
        opt.movetime_ms = 200;
    }

    auto nets = discover_nets(opt.nets_dir);
    if (nets.size() < 2) {
        std::cerr << "No networks found (need at least 2). Looked in: " << opt.nets_dir << "\n";
        return 1;
    }

    std::cout << "Discovered " << nets.size() << " networks:\n";
    for (size_t i = 0; i < nets.size(); ++i) {
        std::cout << "  [" << i << "] " << nets[i].name << "  <-  " << nets[i].path << "\n";
    }

    // Prepare score table
    std::vector<ScoreRow> table(nets.size());
    for (size_t i = 0; i < nets.size(); ++i) {
        table[i].id = (int)i;
        table[i].name = nets[i].name;
    }

    const int games_each_color = std::max(1, opt.games_per_pair / 2);

    // Round-robin
    for (size_t i = 0; i < nets.size(); ++i) {
        for (size_t j = i + 1; j < nets.size(); ++j) {
            const auto& A = nets[i];
            const auto& B = nets[j];
            std::cout << "\n=== Pairing: " << A.name << " vs " << B.name << " ===\n";

            for (int r = 0; r < games_each_color; ++r) {
                // A as White
                auto res1 = play_game(A.path, B.path, opt.movetime_ms, opt.depth, opt.max_plies);
                if (res1 == Result::WhiteWin) {
                    table[i].wins++;
                    table[i].games++;
                    table[j].losses++;
                    table[j].games++;
                    std::cout << "Game W:" << A.name << " 1-0\n";
                } else if (res1 == Result::BlackWin) {
                    table[i].losses++;
                    table[i].games++;
                    table[j].wins++;
                    table[j].games++;
                    std::cout << "Game W:" << A.name << " 0-1\n";
                } else {
                    table[i].draws++;
                    table[i].games++;
                    table[j].draws++;
                    table[j].games++;
                    std::cout << "Game W:" << A.name << " 1/2-1/2\n";
                }

                // B as White
                auto res2 = play_game(B.path, A.path, opt.movetime_ms, opt.depth, opt.max_plies);
                if (res2 == Result::WhiteWin) {
                    table[j].wins++;
                    table[j].games++;
                    table[i].losses++;
                    table[i].games++;
                    std::cout << "Game W:" << B.name << " 1-0\n";
                } else if (res2 == Result::BlackWin) {
                    table[j].losses++;
                    table[j].games++;
                    table[i].wins++;
                    table[i].games++;
                    std::cout << "Game W:" << B.name << " 0-1\n";
                } else {
                    table[j].draws++;
                    table[j].games++;
                    table[i].draws++;
                    table[i].games++;
                    std::cout << "Game W:" << B.name << " 1/2-1/2\n";
                }
            }
        }
    }

    // Sort standings by points, then wins
    std::vector<int> order(table.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double pa = table[a].points();
        const double pb = table[b].points();
        if (pa != pb)
            return pa > pb;
        if (table[a].wins != table[b].wins)
            return table[a].wins > table[b].wins;
        return table[a].name < table[b].name;
    });

    std::cout << "\n===== FINAL STANDINGS =====\n";
    std::cout << std::left << std::setw(24) << "Network" << std::right << std::setw(8) << "GP" << std::setw(8) << "W"
              << std::setw(8) << "D" << std::setw(8) << "L" << std::setw(10) << "Pts" << "\n";
    for (int idx : order) {
        const auto& r = table[idx];
        std::cout << std::left << std::setw(24) << r.name << std::right << std::setw(8) << r.games << std::setw(8)
                  << r.wins << std::setw(8) << r.draws << std::setw(8) << r.losses << std::setw(10) << std::fixed
                  << std::setprecision(1) << r.points() << "\n";
    }

    if (!opt.csv_out.empty()) {
        std::ofstream out(opt.csv_out);
        if (!out) {
            std::cerr << "[WARN] Failed to open CSV for write: " << opt.csv_out << "\n";
        } else {
            out << "rank,network,games,wins,draws,losses,points\n";
            for (size_t rank = 0; rank < order.size(); ++rank) {
                const auto& r = table[order[rank]];
                out << (rank + 1) << "," << r.name << "," << r.games << "," << r.wins << "," << r.draws << ","
                    << r.losses << "," << std::fixed << std::setprecision(1) << r.points() << "\n";
            }
            std::cout << "[CSV] Wrote: " << opt.csv_out << "\n";
        }
    }

    return 0;
}
