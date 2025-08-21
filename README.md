CheckMate Cloud
A full-stack, distributed chess-engine platform — detailed design & build plan

1. Vision & Goals
Goal	Why it matters
Productionise a strong chess engine	Shows mastery of low-level algorithms (bitboards, MCTS, NNUE) and real-world software-engineering rigour (tests, CI, observability, DevOps).
Expose it as a low-latency API and rich Web UI	Demonstrates full-stack capability and modern cloud patterns (gRPC, WebSockets, React, AWS Fargate).
Own the entire ML pipeline (data generation → training → deployment of weights)	Proves you can run ML ops without hand-coding back-prop in C++.
Be clonable by a recruiter in < 3 commands	Empathy for fellow engineers-as-users; emphasises documentation and reproducibility.

2. System-level Scope
mathematica
Copy
Edit
                ┌─────────────────────┐
                │   React Front-End   │
                └────────▲────────────┘
                         │ WebSocket JSON
┌───────────┐    REST    ▼              gRPC         ┌─────────────────┐
│ Auth      │◄────────API-Gateway───────────────►│  Engine-Service │
│ (JWT, OIDC)│                                 ▲│  (C++ core +   │
└───────────┘                                 ││  evaluator)     │
          ▲                                   │└─────────────────┘
          │                                   │
          │               Redis Streams       │ Batched eval reqs
          └──────────Scheduler / Queue────────┘
                               ▲
                               │ .bin records
                               │
              ┌───────────────┐│
              │ Data-Gen CLI  │┘
              └───────────────┘
                    │
                    ▼
              train.bin (zstd) ───►  Python Trainer  ───►  best.nnue
                                            ▲
                                            │ weights file
                                            │
 CMake / CI ──────►  Engine loads weights ◄─┘
3. Component Walk-through
3.1 Engine Core (src/engine)
Language: C++20

Sub-modules:

Bitboard utilities – 0-x88 or magic-bitboard generation.

Move-generator – legal moves in ≤ 2 µs on modern CPUs.

MCTS / UCI driver – receives search params, maintains transposition table.

External contract: exposes Value evaluate(const Position&) and load_weights(path) via a lightweight adaptor so search stays NN-agnostic.

3.2 NNUE Evaluator (src/eval)
Half-KP feature extractor written in C++ with incremental update cache.

Weights loader reads 16-bit quantised arrays from best.nnue.

Forward pass: two dense layers (768→512 ReLU, 512→1) with bias folding to minimise per-node math. AVX2 intrinsics.

3.3 Data-Generation CLI (scripts/gen_data.cpp)
Instrument self-play or analyse positions from public PGN pools (e.g., Lichess Elite).

Writes binary records: [768×int16 features][int16 target_cp].

Compresses with zstd to data/train.bin.

3.4 Training Pipeline (scripts/train_nnue.py)
PyTorch + nnue-pytorch – GPU optional.

DataLoader streams from .bin directly (zero copy).

Loss: MSE or CrossEntropy on WDL buckets.

TensorBoard logger; checkpoints every epoch.

3.5 Weight Converter (scripts/convert_to_nnue.py)
Loads the best PyTorch checkpoint.

Quantises params to int16 scale factors, serialises to Stockfish‐compatible header.

Saves as nets/best.nnue.

3.6 Engine Service (services/engine-api)
Build: CMake target linking core + forward pass.

Wrapper:

gRPC – Evaluate(BatchPositions) → BatchScores (for bulk).

REST – /move (single PV stream) via FastAPI C++/Python bridge or C API exported to Python.

WebSocket – upgrades for PV streaming.

Binary packaging: multi-stage Docker (builder → scratch).

3.7 API-Gateway (services/api-gateway – FastAPI)
Handles JWT validation, CORS, rate-limiting.

Aggregates batch evaluation requests before forwarding to service (reduces chatty traffic).

Returns SSE or WS streams to UI.

3.8 Scheduler & Queue
Redis Streams (or AWS SQS if fully cloud) holds “analyse this FEN” jobs.

Worker pool pods scale via KEDA watching stream length.

3.9 Front-End (ui/)
Vite + React + TypeScript.

Components:

Board (react-chessboard).

AnalysisPanel with depth/score progress bar.

HeatMapLayer.

Auth pages (Login/Signup).

State mgmt: React-Query; sockets auto-invalidate queries.

3.10 Observability Stack
Prometheus – scrapes /metrics from engine & gateway.

Grafana – dashboards: NPS, request latency, job backlog.

Jaeger/OTel – traces for multi-hop analysis requests.

Alertmanager – Slack webhook if p95 latency > 300 ms.

3.11 CI/CD
GitHub Actions matrix:

ubuntu-latest, windows-latest, macos-13 – build & test.

Docker image build & push on main.

deploy job invokes AWS CDK or Terraform Cloud.

Coverage uploaded to Codecov; CodeQL security scan.

3.12 Documentation Site (docs/)
MkDocs Material auto-build on every push to docs/ branch.

Pages: Overview, Build Instructions, API Reference (Swagger re-render), Training Guide, Runbook.

4. Development Phases & Milestones
Phase	Deliverable	Key Tickets
P0 – Bootstrap (Week 1)	Repo scaffold, CMake builds empty placeholders	#1 Project setup, #2 CI skeleton
P1 – Baseline Engine (Week 2-3)	Perft numbers match Stockfish; unit tests pass	#3 Bitboard port, #4 Search loop, #5 GTest harness
P2 – Data Gen + Training (Week 4-5)	train.bin produced; best.nnue exported; engine loads weights and gains ≥ +50 Elo vs. material eval	#6 Data writer, #7 Python dataset, #8 Training script, #9 Converter
P3 – Engine Micro-service (Week 6-7)	gRPC endpoint Evaluate reachable; Docker image on GHCR	#10 gRPC proto, #11 REST bridge, #12 Multi-arch Docker
P4 – API & UI MVP (Week 8-9)	React board plays vs. engine in browser	#13 API-Gateway rate limiting, #14 WS streaming, #15 React build, #16 Netlify preview
P5 – Infra & SRE (Week 10)	Prometheus dashboard, slack alerts; docker compose up starts full stack locally	#17 Helm charts, #18 OTel trace, #19 Grafana
P6 – Distributed Eval (Week 11-12)	Redis batch queue, 4 worker pods, 2.5× throughput	#20 Batch protocol, #21 KEDA autoscaler, #22 Bench report
P7 – Polish & Docs (Week 13)	MkDocs site live, Loom demo video linked	#23 Glossary, #24 Runbook, #25 Demo script
Stretch	GPU self-play, Ray cluster training, mobile PWA	#26-#30

5. Data- & Control-flow Narrative
Player opens UI – Browser pulls React bundle from CloudFront; fetches /profile (JWT refresh).

User requests analysis – Sends WebSocket message {fen, time_ms}.

API-gateway packages a batch of such requests every 10 ms → pushes to Redis stream chess.eval.

Worker pod pops up to 64 positions, runs feature extractor, calls C++ evaluate_batch, replies scores.

Gateway emits incremental PV/score frames back on original WS channel to each subscriber.

Prometheus exporter inside worker counts evaluations_total and latency_ms_bucket{le="20"}.

Training cycle (nightly GitHub CRON): self-play container generates 10 M positions → uploads to S3; GitHub runner on a spot GPU triggers train_nnue.py; on success, pushes best.nnue artefact to release/ in repo; CI redeploys engine pods with new weights, canarying 10 % traffic first.