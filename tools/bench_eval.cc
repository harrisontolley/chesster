// tools/bench_eval.cc
#include "engine/board.hh"
#include "engine/fen.hh"
#include "engine/move.hh"
#include "engine/move_do.hh"
#include "engine/movegen.hh"
#include "eval/eval.hh"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace engine;

static void usage(const char* argv0)
{
    std::cerr << "Usage:\n"
                 "  "
              << argv0
              << " --net PATH [--fens FILE] [--n N] [--loops L] [--warmup W] [--seed S]\n"
                 "\n"
                 "Notes:\n"
                 "  PATH can be a checkpoint directory (containing raw.bin) or a raw.bin file.\n"
                 "  If --fens is omitted, positions are generated via random playouts.\n";
}

static bool read_fens(const std::string& path, std::vector<Board>& out)
{
    std::ifstream in(path);
    if (!in)
        return false;

    std::string line;
    out.clear();
    out.reserve(10000);

    while (std::getline(in, line)) {
        // strip after a '|' or '#' if present
        auto bar = line.find('|');
        if (bar != std::string::npos)
            line = line.substr(0, bar);
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);

        // trim
        auto l = line.find_first_not_of(" \t\r\n");
        auto r = line.find_last_not_of(" \t\r\n");
        if (l == std::string::npos)
            continue;
        line = line.substr(l, r - l + 1);

        // take first 6 tokens (FEN core)
        std::istringstream ss(line);
        std::string f1, f2, f3, f4, f5, f6;
        if (!(ss >> f1 >> f2 >> f3 >> f4 >> f5 >> f6))
            continue;

        try {
            out.push_back(from_fen(f1 + " " + f2 + " " + f3 + " " + f4 + " " + f5 + " " + f6));
        } catch (...) {
            // skip invalid
        }
    }
    return true;
}

static std::vector<Board> random_positions(std::size_t n, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> plies_dist(6, 40); // how mixed the positions are

    std::vector<Board> out;
    out.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        Board b = Board::startpos();
        int plies = plies_dist(rng);
        for (int p = 0; p < plies; ++p) {
            auto leg = generate_legal_moves(b);
            if (leg.empty())
                break;
            std::uniform_int_distribution<std::size_t> pick(0, leg.size() - 1);
            Undo u;
            make_move(b, leg[pick(rng)], u);
        }
        out.push_back(b);
    }
    return out;
}

int main(int argc, char** argv)
{
    std::string net_path;
    std::string fen_file;
    std::size_t N = 50000; // positions
    int LOOPS = 3;         // timed passes over the set
    int WARMUP = 1;        // warmup passes
    std::uint64_t SEED = 1;

    // --- parse args (dumb but effective) ---
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << what << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--net")
            net_path = need("--net");
        else if (a == "--fens")
            fen_file = need("--fens");
        else if (a == "--n")
            N = std::stoull(need("--n"));
        else if (a == "--loops")
            LOOPS = std::stoi(need("--loops"));
        else if (a == "--warmup")
            WARMUP = std::stoi(need("--warmup"));
        else if (a == "--seed")
            SEED = std::stoull(need("--seed"));
        else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown arg: " << a << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    if (net_path.empty()) {
        usage(argv[0]);
        return 1;
    }

    // --- load network ---
    try {
        if (!eval::load_weights(net_path.c_str())) {
            std::cerr << "Failed to load NNUE from: " << net_path << "\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading NNUE: " << e.what() << "\n";
        return 2;
    }

    // --- build positions (not timed) ---
    std::vector<Board> positions;
    if (!fen_file.empty()) {
        if (!read_fens(fen_file, positions) || positions.empty()) {
            std::cerr << "Failed to read any FENs from: " << fen_file << "\n";
            return 3;
        }
        if (positions.size() > N)
            positions.resize(N);
        if (positions.size() < N) {
            // repeat until reach N
            std::vector<Board> copy = positions;
            while (positions.size() < N) {
                positions.push_back(copy[positions.size() % copy.size()]);
            }
        }
    } else {
        positions = random_positions(N, SEED);
    }

    std::cerr << "Prepared " << positions.size() << " positions. Warmup=" << WARMUP << ", Loops=" << LOOPS << "\n";

    // --- warmup ---
    volatile long long sink = 0; // prevent over-optimisation
    for (int w = 0; w < WARMUP; ++w) {
        for (const auto& b : positions)
            sink += eval::evaluate(b);
    }

    // --- timed loops ---
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (int l = 0; l < LOOPS; ++l) {
        for (const auto& b : positions)
            sink += eval::evaluate(b);
    }
    auto t1 = clock::now();
    (void)sink; // keep the compiler honest

    const std::uint64_t evals = static_cast<std::uint64_t>(positions.size()) * static_cast<std::uint64_t>(LOOPS);
    std::chrono::duration<double> dt = t1 - t0;
    const double secs = dt.count();
    const double eps = evals / secs;
    const double ns_per = (secs * 1e9) / evals;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Evals: " << evals << " | Time: " << secs << " s" << " | Throughput: " << eps << " pos/s"
              << " | Latency: " << ns_per << " ns/eval\n";

    return 0;
}
