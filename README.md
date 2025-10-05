<div align="center">

# Chesster

</div>

A tiny **UCI** chess engine with an NNUE evaluator.

* **Weights**: from the Bullet chess trainer -> [jw1912/bullet](https://github.com/jw1912/bullet)
    
* **Testing**: head‑to‑head vs **Stockfish** using cutechess-cli -> [cutechess/cutechess](https://github.com/cutechess/cutechess)

* **Strength**: in quick runs with Unbalanced Human Openings (UHO), Chesster reached an **~2000 Elo** which is **stronger than ~99.5% of humans**  (rough estimate; not a formal rating).

---

## Build (CMake)

If your repo includes a `CMakePresets.json` with a `release` preset:

```bash
cmake --preset release
cmake --build --preset release
```

If you don’t use presets:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

---

## Run

```bash
./build/release/chesster
```

Chesster speaks **UCI**. You can use it in a GUI or from the command line:

```text
uci
isready
position startpos
go movetime 1000
```

---

## NNUE Weights

Chesster expects an NNUE network file. You have a few options:

1. **Default path** (repo-relative):

   * `src/eval/weights/current/raw.bin`
2. **Environment variable** (directory or file):

   * `CHESSTER_NET` → e.g. `export CHESSTER_NET=/path/to/net` (it will also check `/path/to/net/raw.bin`)
3. **UCI option**:

   * `setoption name EvalFile value /absolute/or/relative/path/to/net`

> The data used for training the weights came from this [dataset](https://huggingface.co/datasets/official-stockfish/master-binpacks/tree/main). This [trainer](https://github.com/jw1912/bullet) was used to calculate the weights. 
---

## Quick Testing (cutechess-cli)

Example skeleton for running matches:

```bash
cutechess-cli \
  -engine cmd=./build/release/chesster name=Chesster \
  -engine cmd=stockfish name=Stockfish \
  -each tc=40/0.4 proto=uci \
  -openings file=UHO.pgn format=pgn order=random \
  -games 100 -repeat -concurrency 4 \
  -pgnout results.pgn
```

* Uses **UHO** openings for variety and balance.
* Adjust time control, games, and concurrency for your machine.

---

## Notes

* UCI options supported include `EvalFile` (to point at the NNUE net) and `MoveOverhead`.
* This README is intentionally brief; peek into `src/` for details.

## Future Extensions
A **LOT** of improvements can be made, but this project was primarily to learn a little bit of C++ and have fun with a game I love.  

Some of these improvements/extensions include:
* **Stronger Pruning and Search**: add Null-Move Pruning + Late Move Reductions + Futility/Razoring, with a small check extension. Should cut big nodes in search tree, and predicted (perhaps) semi-large elo gain. 
* **Transposition Table Rework**: Switch to clustered TT (4-way buckets) with UCI Hash size, store static eval, and improve replacement by depth/age. 
* **Faster Movegen**: Use magic bitboards and pin/check masks to generate (mostly) legal moves. Will produce higher NPS and thus greater depth searches in same time control. 
* **Multithreading**: Implement multithreaded search from the root (shared TT) and add UCI Threads as well as proper ponder support.
* **NNUE Upgrades**: Use Half-KP encoding scheme.    