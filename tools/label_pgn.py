import argparse, chess.pgn, chess.engine
from math import copysign


def result_to_float(res):
    return 1.0 if res == "1-0" else 0.0 if res == "0-1" else 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgn")
    ap.add_argument("--out", required=True)
    ap.add_argument("--games", type=int, default=200)  # cap number of games
    ap.add_argument("--nodes", type=int, default=800)  # per-position SF nodes
    ap.add_argument("--stride", type=int, default=4)  # eval every N plies
    ap.add_argument("--minply", type=int, default=8)  # skip opening plies
    ap.add_argument("--sf", default="/usr/games/stockfish")
    args = ap.parse_args()

    eng = chess.engine.SimpleEngine.popen_uci(args.sf)
    eng.configure({"Threads": 1})  # keep small for a smoke test

    out = open(args.out, "w", encoding="utf-8")
    with open(args.pgn, "r", encoding="utf-8", errors="ignore") as f:
        gcount = 0
        while gcount < args.games:
            game = chess.pgn.read_game(f)
            if game is None:
                break
            res = game.headers.get("Result", "*")
            if res not in ("1-0", "0-1", "1/2-1/2"):
                continue
            resf = result_to_float(res)

            board = game.board()
            ply = 0
            for mv in game.mainline_moves():
                board.push(mv)
                ply += 1
                if ply < args.minply or ply % args.stride:  # thin sampling
                    continue

                info = eng.analyse(board, chess.engine.Limit(nodes=args.nodes))
                sc = info["score"].pov(chess.WHITE)

                if sc.is_mate():
                    cp = int(copysign(20000, sc.mate()))
                else:
                    cp = sc.score(mate_score=20000)
                out.write(f"{board.fen()} | {cp} | {resf}\n")

            gcount += 1

    out.close()
    eng.quit()


if __name__ == "__main__":
    main()
